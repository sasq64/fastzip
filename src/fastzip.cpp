#include "fastzip.h"
#include "sign.h"
#include "igzip/c_code/igzip_lib.h"
#include "ziparchive.h"
#include "zipstream.h"
#include "zipformat.h"
#include "inflate.h"
#include "utils.h"

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <chrono>
#include <system_error>
#include <mutex>
#include "mingw.mutex.h"
#include <thread>
#include "mingw.thread.h"
#include <condition_variable>
#include "mingw.condition_variable.h"

#else

#include <thread>
#include <mutex>
#include <condition_variable>

#endif

#include <unistd.h>

#include <assert.h>

#include <string>
#include <cstdio>
#include <deque>
#include <vector>

#include <openssl/sha.h>

using namespace std;
using namespace asn1;


const static int SHA_LEN = 20;

int64_t iz_deflate(int level, char *tgt, char *src, unsigned long tgtsize, unsigned long srcsize,
    int earlyOut);
uint32_t crc32_fast(const void *data, size_t length, uint32_t previousCrc32 = 0);

static int store_compressed(FILE *fp, int inSize, uint8_t *target, uint8_t *sha)
{
    uint8_t *fileData = target;
    fread(fileData, 1, inSize, fp);

    int total = 0;
    if (sha)
    {
        uint8_t buf[65536];
        const int bufSize = sizeof(buf);

        mz_stream stream;
        memset(&stream, 0, sizeof(stream));
        int rc = mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS);
        
		stream.next_in = fileData;
        stream.avail_in = inSize;

        stream.next_out = buf;
        stream.avail_out = bufSize;

        SHA_CTX context;
        SHA_Init(&context);

        while (stream.avail_in > 0)
        {
            stream.next_out = buf;
            stream.avail_out = bufSize;
            mz_inflate(&stream, MZ_SYNC_FLUSH);
            if (sha)
            {
                SHA1_Update(&context, buf, bufSize - stream.avail_out);
            }
            total += (bufSize - stream.avail_out);
        }
        
		mz_inflate(&stream, MZ_FINISH);

        SHA1_Final(sha, &context);
    }
    return total;
}

enum PackResult
{
    RC_FAILED = -1,
    RC_COMPRESSED = 0,
    RC_STORED = 1
};

static PackResult store_uncompressed(FILE *fp, int inSize, uint8_t *out, size_t *outSize,
    uint32_t *checksum, uint8_t *sha)
{
    int size = fread(out, 1, inSize, fp);

    if (sha)
    {
        SHA_CTX context;
        SHA_Init(&context);
        SHA1_Update(&context, out, size);
        SHA1_Final(sha, &context);
    }

    if (checksum)
    {
        *checksum = crc32_fast(out, size);
    }
    *outSize = size;

    return RC_STORED;
}

#ifdef WITH_INTEL

static PackResult intel_deflate(FILE *fp, size_t inSize, uint8_t *buffer, size_t *outSize,
    uint32_t *checksum, uint8_t *sha, int earlyOut)
{
    LZ_Stream2 stream __attribute__((aligned(16)));
    const int IN_SIZE = 1024 * 32;

    uint8_t *fileData = buffer + *outSize - inSize;
    if (fread(fileData, 1, inSize, fp) != inSize)
        return RC_FAILED;

    uint8_t *inBuf = fileData;

    if (sha)
    {
        SHA_CTX context;
        SHA_Init(&context);
        SHA1_Update(&context, fileData, inSize);
        SHA1_Final(sha, &context);
    }

    memset(&stream, 0, sizeof(stream));

    init_stream(&stream);

    stream.avail_out = *outSize;
    stream.next_out = buffer;

    int inBytes = inSize;

    do
    {
        stream.avail_in = IN_SIZE > inBytes ? inBytes : IN_SIZE;

        stream.next_in = inBuf;
        inBuf += stream.avail_in;
        inBytes -= stream.avail_in;

        stream.end_of_stream = (inBytes == 0);

        fast_lz(&stream);

        assert(stream.avail_in == 0);

        uint64_t readBytes = inSize - inBytes;
        uint64_t writtenBytes = *outSize - stream.avail_out;
        if (readBytes)
        {
            int percent = (int)(writtenBytes * 100 / readBytes);

            // printf("DELTA: %d\n", fileData - stream.next_out);
            if (earlyOut && (stream.next_out + IN_SIZE >= fileData))
            {
                // printf("Ratio at overwrite: %d%%\n", percent);
                if (percent >= earlyOut)
                {
                    *outSize = inSize;
                    memcpy(buffer, fileData, inSize);
                    if (checksum)
                    {
                        *checksum = crc32_fast(buffer, inSize);
                    }
                    return RC_STORED;
                }
                earlyOut = 0;
            }

            if (stream.next_out + IN_SIZE >= inBuf)
            {
                return RC_FAILED;
            }
        }
    }
    while (stream.end_of_stream == 0);

    if (checksum)
    {
        *checksum = get_checksum(&stream);
    }

    *outSize = *outSize - stream.avail_out;

    int percent = (int)(*outSize * 100 / inSize);
    if (earlyOut && (buffer + *outSize < fileData))
    {
        if (percent >= earlyOut)
        {
            *outSize = inSize;
            memcpy(buffer, fileData, inSize);
            if (checksum)
            {
                *checksum = crc32_fast(buffer, inSize);
            }
            return RC_STORED;
        }
    }

    return RC_COMPRESSED;
}

#endif

static PackResult infozip_deflate(int packLevel, FILE *fp, int inSize, uint8_t *buffer,
    size_t *outSize, uint32_t *checksum, uint8_t *sha, int earlyOut)
{
    uint8_t *fileData = buffer + *outSize - inSize;
    if ((int)fread(fileData, 1, inSize, fp) != inSize)
        return RC_FAILED;

    if (sha)
    {
        SHA_CTX context;
        SHA_Init(&context);
        SHA1_Update(&context, fileData, inSize);
        SHA1_Final(sha, &context);
    }

    if (checksum)
    {
        *checksum = crc32_fast(fileData, inSize);
    }

    int64_t compSize =
        iz_deflate(packLevel, (char*)buffer, (char*)fileData, *outSize, inSize, earlyOut);
    if (compSize == -1)
        return RC_FAILED;

    if (compSize == -2)
    {
        *outSize = inSize;
        memmove(buffer, fileData, inSize);
        return RC_STORED;
    }

    *outSize = compSize;

    return RC_COMPRESSED;
}

void Fastzip::packZipData(FILE *fp, int size, PackFormat inFormat, PackFormat outFormat,
    uint8_t *sha, ZipEntry &target)
{
    size_t outSize = size + size / 4 + 1024 * 64;

    uint8_t *outBuf = new uint8_t[outSize];
    target.store = false;

    if (size == 0)
    {
        store_uncompressed(fp, 0, outBuf, &outSize, &target.crc, sha);
        target.data = outBuf;
        target.store = true;
        target.dataSize = 0;
        return;
    }

    if (inFormat == UNCOMPRESSED)
    {
        auto startPos = ftell_x(fp);
        PackResult state;

        if (outFormat >= ZIP1_COMPRESSED && outFormat <= ZIP9_COMPRESSED)
            state =
                infozip_deflate(outFormat, fp, size, outBuf, &outSize, &target.crc, sha, earlyOut);
#ifdef WITH_INTEL
        else if (outFormat == INTEL_COMPRESSED)
            state = intel_deflate(fp, size, outBuf, &outSize, &target.crc, sha, earlyOut);
#endif
        else
            state = store_uncompressed(fp, size, outBuf, &outSize, &target.crc, sha);

        if (state == RC_FAILED)
        {
            warning(string("Compression failed! Storing '") + target.name + "' as a fallback");
            fseek_x(fp, startPos, SEEK_SET);
            state = store_uncompressed(fp, size, outBuf, &outSize, &target.crc, sha);
        }
        if (state == RC_STORED)
            target.store = true;
        target.originalSize = size;
    }
    else if (inFormat > 0 && outFormat == COMPRESSED)
    {
        // Keep format, just inflate to calculate sha if necessary
        store_compressed(fp, size, outBuf, sha);
        outSize = size;
    }
    else
    {
        // Unpacking is not supported
        warning(string("Unpacking not supported, storing '") + target.name +
            "' with original compression");
        target.originalSize = store_compressed(fp, size, outBuf, sha);
        outSize = size;
    }

    target.data = outBuf;
    target.dataSize = outSize;
}

void Fastzip::addZip(string zipName, PackFormat packFormat)
{
    ZipStream zs {zipName};
    for (int i = 0; i < zs.size(); i++)
    {
        auto e = zs.getEntry(i);
        FileTarget ft;
        ft.source = zipName;
        ft.offset = e.offset;
        ft.target = e.name;
        ft.packFormat = packFormat;
        fileNames.push_back(ft);

        strLen += ft.target.length();
    }
}

void Fastzip::addDir(string d, PackFormat packFormat)
{
    string translateDir = "";
    auto parts = split(d, "=");
    if (parts.size() > 1)
    {
        d = parts[0];
        translateDir = parts[1];
    }

    // Set 'skipLen' to the number of chars to strip from the start of each path name
    int skipLen = 0;
    if (translateDir.length() > 0)
    {
        skipLen = d.length();
    }
    else if (junkPaths)
    {
        auto last = d.find_last_of("\\/");
        if (last != string::npos)
            skipLen = last + 1;
    }
    else
    {
        const char *fn = d.c_str();
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
    listFiles(d, [&](const string &path)
    {
        string target = path.substr(skipLen);
        PackFormat pf = packFormat;

        if (storeExts.size() > 0)
        {
            const char *ext = nullptr;
            auto dot = path.find_last_of(".");
            if (dot != string::npos)
                ext = &path.c_str()[dot + 1];
            if (ext && *ext)
            {
                for (const auto &se : storeExts)
                {
                    if (strcmp(se.c_str(), ext) == 0)
                    {
                        pf = UNCOMPRESSED;
                        break;
                    }
                }
            }
        }

        if (translateDir.length())
        {
            target = translateDir + target;
        }
        for (unsigned i = 0; i < target.length(); i++)
        {
            if (target[i] == '\\')
                target[i] = '/';
        }
        strLen += target.length();

        fileNames.emplace_back(path, target, pf);
    });
}

void Fastzip::exec()
{
    if (fileNames.size() == 0)
        throw fastzip_exception("No paths specified");
    if (fileNames.size() >= 65535)
        warning("More than 64K files, adding 64bit features.");
    if (zipfile == "")
        throw fastzip_exception("Zipfile must be specified");

    if (doSign)
    {
        if (!keyStore.load(keystoreName))
            throw fastzip_exception("Could not load keystore");
    }

    string tempFile = zipfile + ".fastzip_";

    const char *target = tempFile.c_str();
    remove(target);
    ZipArchive zipArchive(target, fileNames.size() + 5, strLen + 1024);
    zipArchive.doAlign(zipAlign);
	zipArchive.doForce64(force64);

    char *digestFile = new char[strLen + fileNames.size() * 6400];
    char *digestPtr = digestFile;

    mutex m;
    condition_variable seq_cv;
    int currentIndex = 0;
	int totalCount = fileNames.size();

    vector<thread> workerThreads(threadCount);

    for (int i = 0; i < threadCount; i++)
    {
        workerThreads[i] = thread([&]
        {
            while (true)
            {
                FileTarget fileName;
				int index;
                {
                    lock_guard<mutex> lock {m};
                    if (fileNames.size() == 0)
                        return;
                    fileName = fileNames.front();
					index = totalCount - fileNames.size();
                    fileNames.pop_front();
                }

                bool skipFile = false;
                ZipEntry entry;
                FILE *fp = nullptr;
                bool isPacked = false;
                uint32_t dataSize;

                if (doSign)
                {
                    if (fileName.target.substr(0, 8) == "META-INF")
                    {
                        skipFile = true;
                    }
                }

                entry.name = fileName.target;

                if (fileName.offset != 0xffffffff)
                {
                    fp = fopen(fileName.source.c_str(), "rb");
                    fseek_x(fp, fileName.offset, SEEK_SET);
                    LocalEntry le;
                    if (fread(&le, 1, sizeof(le), fp) != sizeof(le))
                        exit(-1);

                    dataSize = le.compSize;
                    entry.originalSize = le.uncompSize;
                    isPacked = le.method != 0;

                    entry.timeStamp = msdosToUnixTime(le.dateTime);
                    entry.crc = le.crc;
                    fseek_x(fp, le.nameLen + le.exLen, SEEK_CUR);
                }
                else
                {
                    struct stat ss;
                    if (stat(fileName.source.c_str(), &ss) == 0)
                    {
                        entry.timeStamp = ss.st_mtime;
						entry.flags = ss.st_mode;
						entry.uid = ss.st_uid;
						entry.gid = ss.st_gid;
                        dataSize = ss.st_size;
                    }
                    else
                    {
                        warning(string("Could not access " + fileName.source));
                        skipFile = true;
                    }
#ifndef _WIN32
                    if (!skipFile && (ss.st_mode & S_IFLNK) == S_IFLNK)
                    {
                        warning(string("Skipping symlink " + fileName.source));
                        skipFile = true;
                    }
#endif
                    if (!skipFile && (ss.st_mode & S_IFDIR))
                    {
                        // Add directories?
                        skipFile = true;
                    }
/*
                    if (ss.st_size >= 0x60000000)
                    {
                        warning(string("Skipping large file " + fileName.source));
                        skipFile = true;
                    }
*/
                    dataSize = entry.originalSize = ss.st_size;
                }

                if (!skipFile)
                {
                    if (!fp)
                        fp = fopen(fileName.source.c_str(), "rb");
                    if (!fp)
                    {
                        warning(string("Could not read " + fileName.source));
                        skipFile = true;
                    }
                }
                if (!skipFile)
                {
                    uint8_t sha[SHA_LEN];

                    packZipData(fp, dataSize, isPacked ? COMPRESSED : UNCOMPRESSED,
                        isPacked && fileName.packFormat > 0
                        ? COMPRESSED
                        : (PackFormat)fileName.packFormat,
                        doSign ? sha : nullptr, entry);
                    fclose(fp);

                    if (verbose)
                    {
                        int percent = 0;
                        if (entry.originalSize > 0)
                            percent = entry.dataSize * 100 / (int)entry.originalSize;
                        printf("%d %s %dKB (%s %d%%)\n", index, entry.name.c_str(),
                            (int)(entry.dataSize / 1024),
                            entry.store ? "stored" : "deflated",
                            entry.store ? 100 : percent);
                    }

                    {
                        unique_lock<mutex> lock {m};
                        if (doSeq)
                        {
                            while (index != currentIndex)
                                seq_cv.wait(lock);
                        }
                        if (doSign)
                        {
                            sprintf(digestPtr,
                                "Name: %s\015\012SHA1-Digest: %s\015\012\015\012",
                                entry.name.c_str(), base64_encode(sha, SHA_LEN).c_str());
                            while (*digestPtr)
                                digestPtr++;
                        }
                        zipArchive.add(entry);
                        currentIndex++;
                    }
                    if (doSeq)
                        seq_cv.notify_all();
                    delete [] entry.data;
                }
                else
                {
                    if (doSeq)
                    {
                        {
                            unique_lock<mutex> lock {m};
                            while (index != currentIndex)
                            {
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
    if (doSign)
    {
        lock_guard<mutex> lock {m};
		keyStore.setCurrentKey(keyName, keyPassword);
        sign(zipArchive, keyStore, digestFile);
    }

    zipArchive.close();

    remove(zipfile.c_str());
    if (rename(tempFile.c_str(), zipfile.c_str()) != 0)
        throw fastzip_exception("Could not write target file");

    delete[] digestFile;
}
