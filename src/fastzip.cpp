#include "fastzip.h"

#ifdef WITH_INTEL
#    include "igzip/c_code/igzip_lib.h"
#endif

#include "file.h"
#include "inflate.h"
#include "sign.h"
#include "utils.h"
#include "ziparchive.h"
#include "zipformat.h"
#include "zipstream.h"

#include <condition_variable>
#include <mutex>
#include <thread>

#include <cassert>

#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include <openssl/sha.h>

using std::string;
using std::vector;

static constexpr int SHA_LEN = 20;

int64_t iz_deflate(int level, char* tgt, char* src, unsigned long tgtsize,
                   unsigned long srcsize);
uint32_t crc32_fast(const void* data, size_t length,
                    uint32_t previousCrc32 = 0);

static int store_compressed(File& f, int inSize, uint8_t* target, uint8_t* sha)
{
    uint8_t* fileData = target;
    f.Read(fileData, inSize);

    int total = 0;
    if (sha) {
        uint8_t buf[65536];
        const int bufSize = sizeof(buf);

        mz_stream stream;
        memset(&stream, 0, sizeof(stream));
        if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
            throw fastzip_exception("Could not init miniz for compression");

        stream.next_in = fileData;
        stream.avail_in = inSize;

        SHA_CTX context;
        SHA1_Init(&context);

        while (stream.avail_in > 0) {
            stream.next_out = buf;
            stream.avail_out = bufSize;
            mz_inflate(&stream, MZ_SYNC_FLUSH);
            SHA1_Update(&context, buf, bufSize - stream.avail_out);
            total += (bufSize - stream.avail_out);
        }

        mz_inflate(&stream, MZ_FINISH);

        SHA1_Final(sha, &context);
    }
    return total;
}

enum class PackResult
{
    FAILED = -1,
    COMPRESSED = 0,
    STORED = 1
};

static PackResult store_uncompressed(File& f, int inSize, uint8_t* out,
                                     size_t* outSize, uint32_t* checksum,
                                     uint8_t* sha)
{
    auto const size = f.Read(out, inSize);

    if (sha) {
        SHA_CTX context;
        SHA1_Init(&context);
        SHA1_Update(&context, out, size);
        SHA1_Final(sha, &context);
    }

    if (checksum) {
        *checksum = crc32_fast(out, size);
    }
    *outSize = size;

    return PackResult::STORED;
}

#ifdef WITH_INTEL

static PackResult intel_deflate(File& f, size_t inSize, uint8_t* buffer,
                                size_t* outSize, uint32_t* checksum,
                                uint8_t* sha, int earlyOut)
{
    LZ_Stream2 stream __attribute__((aligned(16)));
    const int IN_SIZE = 1024 * 32;

    uint8_t* fileData = buffer + *outSize - inSize;
    if (f.Read(fileData, inSize) != inSize)
        return PackResult::FAILED;

    uint8_t* inBuf = fileData;

    if (sha) {
        SHA_CTX context;
        SHA1_Init(&context);
        SHA1_Update(&context, fileData, inSize);
        SHA1_Final(sha, &context);
    }

    memset(&stream, 0, sizeof(stream));

    init_stream(&stream);

    stream.avail_out = *outSize;
    stream.next_out = buffer;

    int inBytes = inSize;

    do {
        stream.avail_in = IN_SIZE > inBytes ? inBytes : IN_SIZE;

        stream.next_in = inBuf;
        inBuf += stream.avail_in;
        inBytes -= stream.avail_in;

        stream.end_of_stream = (inBytes == 0);

        fast_lz(&stream);

        assert(stream.avail_in == 0);

        uint64_t readBytes = inSize - inBytes;
        uint64_t writtenBytes = *outSize - stream.avail_out;
        if (readBytes) {
            auto percent = (int)(writtenBytes * 100 / readBytes);

            // printf("DELTA: %d\n", fileData - stream.next_out);
            if (earlyOut && (stream.next_out + IN_SIZE >= fileData)) {
                // printf("Ratio at overwrite: %d%%\n", percent);
                if (percent >= earlyOut) {
                    *outSize = inSize;
                    memcpy(buffer, fileData, inSize);
                    if (checksum) {
                        *checksum = crc32_fast(buffer, inSize);
                    }
                    return PackResult::STORED;
                }
                earlyOut = 0;
            }

            if (stream.next_out + IN_SIZE >= inBuf) {
                return PackResult::FAILED;
            }
        }
    } while (stream.end_of_stream == 0);

    if (checksum)
        *checksum = get_checksum(&stream);

    *outSize = *outSize - stream.avail_out;

    return PackResult::COMPRESSED;
}

#endif

static PackResult infozip_deflate(int packLevel, File& f, int inSize,
                                  uint8_t* buffer, size_t* outSize,
                                  uint32_t* checksum, uint8_t* sha)
{
    uint8_t* fileData = buffer + *outSize - inSize;
    if ((int)f.Read(fileData, inSize) != inSize)
        return PackResult::FAILED;

    if (sha) {
        SHA_CTX context;
        SHA1_Init(&context);
        SHA1_Update(&context, fileData, inSize);
        SHA1_Final(sha, &context);
    }

    if (checksum) {
        *checksum = crc32_fast(fileData, inSize);
    }

    int64_t compSize =
        iz_deflate(packLevel, (char*)buffer, (char*)fileData, *outSize, inSize);
    if (compSize == -1)
        return PackResult::FAILED;

    if (compSize == -2) {
        *outSize = inSize << 3;
        // memmove(buffer, fileData, inSize);
        return PackResult::STORED;
    }

    *outSize = compSize;

    return PackResult::COMPRESSED;
}

void Fastzip::packZipData(File& f, int size, PackFormat inFormat,
                          PackFormat outFormat, uint8_t* sha, ZipEntry& target)
{
    // Maximum size for deflate + space for buffer
    size_t outSize = size + (size / 16383 + 1) * 5 + 64 * 1024;
    auto outBuf = std::make_unique<uint8_t[]>(outSize);
    // auto outBuf = std::make_unique<uint8_t[]>(outSize);
    target.store = false;

    if (size == 0) {
        store_uncompressed(f, 0, outBuf.get(), &outSize, &target.crc, sha);
        target.data = std::move(outBuf);
        target.store = true;
        target.dataSize = 0;
        return;
    }

    if (inFormat == UNCOMPRESSED) {
        auto startPos = f.tell();
        PackResult state;

        if (outFormat >= ZIP1_COMPRESSED && outFormat <= ZIP9_COMPRESSED) {
            state = infozip_deflate(outFormat, f, size, outBuf.get(), &outSize,
                                    &target.crc, sha);
            outSize = (outSize + 7) >> 3;
        }
#ifdef WITH_INTEL
        else if (outFormat == INTEL_COMPRESSED)
            state = intel_deflate(f, size, outBuf.get(), &outSize, &target.crc,
                                  sha, earlyOut);
#endif
        else
            state = store_uncompressed(f, size, outBuf.get(), &outSize,
                                       &target.crc, sha);

        if (state == PackResult::FAILED) {
            warning(string("Compression failed! Storing '") + target.name +
                    "' as a fallback");
            f.seek(startPos);
            state = store_uncompressed(f, size, outBuf.get(), &outSize,
                                       &target.crc, sha);
        }
        if (state == PackResult::STORED)
            target.store = true;
        target.originalSize = size;
    } else if (inFormat > 0 && outFormat == COMPRESSED) {
        // Keep format, just inflate to calculate sha if necessary
        store_compressed(f, size, outBuf.get(), sha);
        outSize = size;
    } else {
        // Unpacking is not supported
        warning(string("Unpacking not supported, storing '") + target.name +
                "' with original compression");
        target.originalSize = store_compressed(f, size, outBuf.get(), sha);
        outSize = size;
    }

    target.data = std::move(outBuf);
    target.dataSize = outSize;
}

void Fastzip::addZip(const fs::path& zipName, PackFormat format)
{
    for (auto const& entry : ZipStream{zipName}) {
        fileNames.emplace_back(zipName, entry.name, format, entry.offset);
        strLen += entry.name.length();
    }
}

void Fastzip::addDir(const PathAlias& dirName, PackFormat format)
{
    const string& d = dirName.diskPath;

    // Set 'skipLen' to the number of chars to strip from the start of each path
    // name
    int skipLen = 0;
    if (dirName.aliasTo.length() > 0) {
        skipLen = d.length();
    } else if (junkPaths) {
        auto last = d.find_last_of("\\/");
        if (last != string::npos)
            skipLen = last + 1;
    } else {
        const char* fn = d.c_str();
        // Skip DOS drive letter
        if (fn[1] == ':')
            fn += 2;

        // Skip path prefixes such as '/' or '../../'
        while (*fn == '/' || *fn == '\\' || *fn == '.')
            fn++;
        while (fn > d.c_str() && fn[-1] == '.')
            fn--;
        skipLen = fn - d.c_str();
    }

    // Recursivly add all files to fileNames
    listFiles(d, [&](const string& path) {
        string target = path.substr(skipLen);
        PackFormat pf = format;

        if (!storeExts.empty()) {
            const char* ext = nullptr;
            auto dot = path.find_last_of('.');
            if (dot != string::npos)
                ext = &path.c_str()[dot + 1];
            if (ext && *ext) {
                for (const auto& se : storeExts) {
                    if (strcmp(se.c_str(), ext) == 0) {
                        pf = UNCOMPRESSED;
                        break;
                    }
                }
            }
        }

        if (dirName.aliasTo.length()) {
            target = dirName.aliasTo + target;
        }
        for (auto& t : target)
            if (t == '\\')
                t = '/';
        strLen += target.length();

        fileNames.emplace_back(path, target, pf);
    });
}

void Fastzip::exec()
{
    using std::condition_variable;
    using std::lock_guard;
    using std::mutex;
    using std::thread;
    using std::unique_lock;

    std::error_code ec;

    if (fileNames.empty())
        throw fastzip_exception("No paths specified");
    if (fileNames.size() >= 65535)
        warning("More than 64K files, adding 64bit features.");
    if (zipfile == "")
        throw fastzip_exception("Zipfile must be specified");

    if (doSign) {
        if (!keyStore.load(keystoreName))
            throw fastzip_exception("Could not load keystore");
    }

    const fs::path tempFile = fs::path(zipfile.string() + ".fastzip_");
    fs::remove(tempFile, ec);
    ZipArchive zipArchive(tempFile.c_str(), fileNames.size() + 5,
                          strLen + 1024);
    zipArchive.doAlign(zipAlign);
    zipArchive.doForce64(force64);

    auto digestFile =
        std::make_unique<char[]>(strLen + fileNames.size() * 6400);
    char* digestPtr = digestFile.get();

    mutex m;
    condition_variable seq_cv;
    int currentIndex = 0;
    const int totalCount = fileNames.size();

    vector<thread> workerThreads(threadCount);

    for (auto& workerThread : workerThreads) {
        workerThread = thread([&] {
            while (true) {
                FileTarget fileName;
                int index;
                {
                    lock_guard lock{m};

                    if (fileNames.empty())
                        return;
                    fileName = fileNames.front();
                    index = totalCount - fileNames.size();
                    fileNames.pop_front();
                }

                bool skipFile = false;
                ZipEntry entry;
                bool isPacked = false;
                uint32_t dataSize;
                File f{fileName.source};

                if (doSign) {
                    if (fileName.target.substr(0, 8) == "META-INF") {
                        skipFile = true;
                    }
                }

                entry.name = fileName.target;

                if (fileName.size != 0) {
                    f.seek(fileName.offset);
                    dataSize = fileName.size;
                } else if (fileName.offset != 0xffffffff) {
                    f.seek(fileName.offset);
                    auto le = f.Read<LocalEntry>();

                    dataSize = le.compSize;
                    entry.originalSize = le.uncompSize;
                    isPacked = le.method != 0;

                    entry.timeStamp = msdosToUnixTime(le.dateTime);
                    entry.crc = le.crc;
                    f.seek(le.nameLen + le.exLen, File::Seek::Cur);
                } else {
                    struct stat ss;
                    if (stat(fileName.source.c_str(), &ss) == 0) {
                        entry.timeStamp = ss.st_mtime;
                        entry.flags = ss.st_mode;
                        entry.uid = ss.st_uid;
                        entry.gid = ss.st_gid;
                    } else {
                        warning(string("Could not access ") +
                                fileName.source.string());
                        skipFile = true;
                    }
#ifndef _WIN32
                    if (!skipFile && (ss.st_mode & S_IFLNK) == S_IFLNK) {
                        warning(string("Skipping symlink ") +
                                fileName.source.string());
                        skipFile = true;
                    }
#endif
                    if (!skipFile && (ss.st_mode & S_IFDIR)) {
                        // Add directories?
                        skipFile = true;
                    }
                    dataSize = entry.originalSize = ss.st_size;
                }

                if (!skipFile) {
                    if (!f.canRead()) {
                        warning(string("Could not read ") +
                                fileName.source.string());
                        skipFile = true;
                    }
                }
                if (!skipFile) {
                    uint8_t sha[SHA_LEN];

                    packZipData(f, dataSize,
                                isPacked ? COMPRESSED : UNCOMPRESSED,
                                isPacked && fileName.packFormat > 0
                                    ? COMPRESSED
                                    : (PackFormat)fileName.packFormat,
                                doSign ? sha : nullptr, entry);
                    f.close();

                    if (verbose) {
                        int percent = 0;
                        if (entry.originalSize > 0)
                            percent =
                                entry.dataSize * 100 / (int)entry.originalSize;
                        printf("%d %s %dKB (%s %d%%)\n", index,
                               entry.name.c_str(), (int)(entry.dataSize / 1024),
                               entry.store ? "stored" : "deflated",
                               entry.store ? 100 : percent);
                    }

                    {
                        unique_lock lock{m};
                        if (doSeq) {
                            while (index != currentIndex)
                                seq_cv.wait(lock);
                        }
                        if (doSign) {
                            sprintf(digestPtr,
                                    "Name: %s\015\012SHA1-Digest: "
                                    "%s\015\012\015\012",
                                    entry.name.c_str(),
                                    base64_encode(sha, SHA_LEN).c_str());
                            while (*digestPtr)
                                digestPtr++;
                        }
                        zipArchive.add(entry);
                        currentIndex++;
                    }
                    if (doSeq)
                        seq_cv.notify_all();
                } else {
                    if (doSeq) {
                        {
                            unique_lock lock{m};
                            while (index != currentIndex) {
                                seq_cv.wait(lock);
                            }
                            currentIndex++;
                        }
                        seq_cv.notify_all();
                    }
                }
            }
        });
    }

    for (int i = 0; i < threadCount; i++)
        workerThreads[i].join();
    /*
        if (ftell_x(zipArchive.getFile()) > (int64_t)0xff000000)
            throw fastzip_exception("Resulting file too large");
    */
    if (doSign) {
        lock_guard lock{m};
        keyStore.setCurrentKey(keyName, keyPassword);
        sign(zipArchive, keyStore, digestFile.get());
    }

    zipArchive.close();

    remove(zipfile.c_str());
    if (rename(tempFile.c_str(), zipfile.c_str()) != 0)
        throw fastzip_exception("Could not write target file");
}
