#include "funzip.h"
#include "inflate.h"
#include "utils.h"
#include "zipformat.h"
#include "zipstream.h"

#include <mutex>
#include <thread>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <experimental/filesystem>
#include <sys/stat.h>

#ifndef S_IFLNK
#    define S_IFLNK 0120000
#endif

namespace fs = std::experimental::filesystem;

static bool copyfile(File& fout, size_t size, File& fin)
{
    if (!fout.canWrite())
        return false;
    std::array<uint8_t, 65536 * 4> buf;
    while (size > 0) {
        auto rc = fin.Read(&buf[0], size < buf.size() ? size : buf.size());
        if (rc == 0)
            return rc;
        size -= rc;
        fout.Write(&buf[0], rc);
    }
    return true;
}

static int64_t uncompress(File& fout, int64_t inSize, File& fin)
{
    int64_t total = 0;
    std::array<uint8_t, 65536> buf;

    mz_stream stream{};
    mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS);

    auto data = std::make_unique<uint8_t[]>(inSize > 0 ? inSize : buf.size());

    if (inSize > 0) {
        fin.Read(&data[0], inSize);
        stream.next_in = &data[0];
        stream.avail_in = inSize;
    }
    int rc = MZ_OK;
    while (rc == MZ_OK) {
        if (inSize == 0 && stream.avail_in == 0) {
            stream.next_in = &data[0];
            stream.avail_in = fin.Read(&data[0], buf.size());
            if (stream.avail_in == 0)
                break;
        }
        stream.next_out = &buf[0];
        stream.avail_out = buf.size();

        rc = mz_inflate(&stream, MZ_SYNC_FLUSH);
        // Did we unpack anything?
        if (stream.avail_out == buf.size())
            return -1;
        fout.Write(&buf[0], buf.size() - stream.avail_out);
        total += (buf.size() - stream.avail_out);
    }
    if (rc < 0)
        throw funzip_exception("Inflate failed");

    mz_inflate(&stream, MZ_FINISH);

    mz_inflateEnd(&stream);

    return total;
}

void FUnzip::smartDestDir(ZipStream& zs)
{
    if (zs.size() == 1)
        return;

    destinationDir = path_basename(zipName);

    auto n = zs.getEntry(0).name;

    auto pos = n.find('/');
    if (pos == std::string::npos)
        return;
    std::string first = n.substr(0, pos);

    for (const auto& e : zs) {
        if (!startsWith(e.name, first))
            return;
    }
    destinationDir = "";
}

static inline void setMeta(const std::string& name,
                           [[maybe_unused]] uint16_t flags,
                           [[maybe_unused]] uint32_t datetime,
                           [[maybe_unused]] int uid, [[maybe_unused]] int gid,
                           [[maybe_unused]] bool link = false)
{

    if (flags) {
        fs::perms p = (fs::perms)flags;
        fs::permissions(name, p);
    }

    // printf("%s : Data %x / Flags %x\n", name.c_str(), datetime, flags);
    auto tt = msdosToUnixTime(datetime);
    // std::tm tm = *std::localtime(&tt);
    // std::cout << std::put_time(&tm, "%c%n");

    auto ft = fs::file_time_type::clock::from_time_t(tt);
    fs::last_write_time(name, ft);

#if 0
    auto timestamp = msdosToUnixTime(datetime);
    struct timeval t[2] = {{0,0}};
    t[0].tv_sec = t[1].tv_sec = timestamp;
    
    fs::file_time ft;
    fs::last_write_time(name, );

    if(link)
    {
        if(flags)
            lchmod(name.c_str(), flags);
        lutimes(name.c_str(), t);
        lchown(name.c_str(), uid, gid);
    }
    else
    {
        if(flags)
            chmod(name.c_str(), flags);
        chown(name.c_str(), uid, gid);
        utimes(name.c_str(), t);
    }
#endif
}
static void readExtra(File& f, int exLen, int* uid, int* gid,
                      int64_t* compSize = nullptr,
                      int64_t* uncompSize = nullptr)
{
    Extra extra;
    while (exLen > 0) {
        f.Read((uint8_t*)&extra, 4);
        f.Read(extra.data, extra.size);
        exLen -= (extra.size + 4);
        // printf("EXTRA %x\n", extra.id);
        if (extra.id == 0x7875) {

            *uid = extra.unix2.UID;
            *gid = extra.unix2.GID;
        } else if (extra.id == 0x01) {
            if (compSize)
                *compSize = extra.zip64.compSize;
            if (uncompSize)
                *uncompSize = extra.zip64.uncompSize;
        } else if (extra.id == 0xd) {
            std::string link((char*)extra.unix.var, extra.size - 12);
            // printf("LINK:%s\n", link.c_str());
        }
    }
}

void FUnzip::exec()
{
    std::atomic<int> entryNum(0);
    ZipStream zs{zipName};
    if (!zs.valid())
        throw funzip_exception("Not a zip file");

    if (zs.size() == 0)
        return;

    if (listFiles) {
        for (auto const& e : zs) {
            printf("%s\n", e.name.c_str());
        }
        return;
    }

    if (destinationDir == "")
        smartDestDir(zs);
    if (destinationDir != "" &&
        destinationDir[destinationDir.size() - 1] != '/')
        destinationDir += "/";

    std::vector<int> links;
    std::vector<int> dirs;
    std::mutex lm;

    std::vector<std::thread> workerThreads(threadCount);

    for (auto& t : workerThreads) {
        t = std::thread([&zs, &entryNum, &lm, &links, &dirs, f = zs.dupFile(),
                         verbose = verbose,
                         destDir = destinationDir]() mutable {
            while (true) {
                unsigned en = entryNum++;
                if (en >= zs.size())
                    break;
                auto& e = zs.getEntry(en);
                if ((e.flags & S_IFLNK) == S_IFLNK) {
                    std::lock_guard<std::mutex> lock(lm);
                    links.push_back(en);
                    continue;
                }

                if ((e.flags & S_IFDIR) == S_IFDIR ||
                    e.name[e.name.length() - 1] == '/') {
                    std::lock_guard<std::mutex> lock(lm);
                    dirs.push_back(en);
                    if (!fileExists(e.name))
                        makedirs(e.name);
                    continue;
                }

                f.seek(e.offset);
                auto le = f.Read<LocalEntry>();

                f.seek(le.nameLen, SEEK_CUR);
                int gid = -1;
                int uid = -1;
                int64_t uncompSize = le.uncompSize;
                int64_t compSize = le.compSize;
                // Read extra fields
                readExtra(f, le.exLen, &uid, &gid, &compSize, &uncompSize);
                auto name = destDir + e.name;
                auto dname = path_directory(name);
                if (dname != "" && !fileExists(dname))
                    makedirs(dname);

                if (verbose) {
                    printf("%s\n", name.c_str());
                    fflush(stdout);
                }
                // printf("%s %x %s %s\n", fileName, a, (a & S_IFDIR)  ==
                // S_IFDIR ? "DIR" : "", (a & S_IFLNK) == S_IFLNK ? "LINK" :
                // "");

                auto fout = File{name, File::Mode::WRITE};
                if (!fout.canWrite()) {
                    // char errstr[128];
                    // strerror_r(errno, errstr, sizeof(errstr));
                    // fprintf(stderr, "**Warning: Could not write '%s' (%s)\n",
                    // name.c_str(), errstr);
                    continue;
                }
                if (le.method == 0)
                    copyfile(fout, uncompSize, f);
                else
                    uncompress(fout, compSize, f);
                fout.close();
                setMeta(name, e.flags, le.dateTime, uid, gid);
            }
        });
    }
    for (auto& t : workerThreads)
        t.join();

    char linkName[65536];
    int uid, gid;
    auto f = zs.dupFile();
    for (int i : links) {
        auto& e = zs.getEntry(i);
        f.seek(e.offset);
        auto le = f.Read<LocalEntry>();

        f.seek(le.nameLen, SEEK_CUR);
        uid = gid = -1;
        int64_t uncompSize = le.uncompSize;
        readExtra(f, le.exLen, &uid, &gid, nullptr, &uncompSize);
        f.Read(linkName, uncompSize);
        linkName[uncompSize] = 0;
        auto name = destinationDir + e.name;
        auto dname = path_directory(name);
        auto fname = path_filename(e.name);
        // int fd = open(dname.c_str(), 0);
        if (verbose)
            printf("Link %s/%s -> %s\n", dname.c_str(), fname.c_str(),
                   linkName);
        fs::create_symlink(linkName, fname);
        // symlinkat(linkName, fd, fname.c_str());
        // close(fd);
        setMeta(name, e.flags, le.dateTime, uid, gid, true);
    }
    for (int i : dirs) {
        auto& e = zs.getEntry(i);
        f.seek(e.offset);
        auto le = f.Read<LocalEntry>();

        f.seek(le.nameLen, SEEK_CUR);
        uid = gid = -1;
        readExtra(f, le.exLen, &uid, &gid);
        auto name = destinationDir + e.name;
        auto l = name.length();
        if (name[l - 1] == '/')
            name = name.substr(0, l - 1);
        setMeta(name, e.flags, le.dateTime, uid, gid);
    }

    if (zs.comment())
        puts(zs.comment());
}
