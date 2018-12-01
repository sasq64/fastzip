#include "ziparchive.h"
#include "utils.h"
#include "zipformat.h"

#include "file.h"

#include <cassert>
#include <ctime>
#include <sys/stat.h>

ZipArchive::ZipArchive(const std::string& fileName, int numFiles, int strLen)
    : f{fileName, File::WRITE}
{
    static_assert(sizeof(LocalEntry) == 30);
    static_assert(sizeof(CentralDirEntry) == 46);

    entries = std::make_unique<uint8_t[]>(
        strLen + numFiles * (sizeof(CentralDirEntry) + sizeof(Extra64)));
    entryPtr = entries.get();
    entryCount = 0;
}

void ZipArchive::addFile(const std::string& fileName, bool store,
                         uint64_t compSize, uint64_t uncompSize, time_t ts,
                         uint32_t crc, uint16_t flags)
{
    ZipEntry ze;
    ze.data = nullptr;
    ze.name = fileName;
    ze.store = store;
    ze.dataSize = compSize;
    ze.originalSize = uncompSize;
    ze.timeStamp = ts;
    ze.crc = crc;
    ze.flags = flags;
    ze.gid = 0;
    ze.uid = 0;
    add(ze);
}

void ZipArchive::add(const ZipEntry& entry)
{
    static LocalEntry head = {0x04034b50, 10, 0, 0, 0, 0, 0, 0, 0, 0};
    static Extra64 extra64 = {0x1, 28, 0, 0, 0, 0};
    static const uint8_t zeroes[] = {0, 0, 0, 0};
    const struct tm* lt = localtime(&entry.timeStamp);
    // date:   YYYYYYYM MMMDDDDD
    // time:   HHHHHMMM MMMSSSSS
    uint32_t msdos_ts = ((lt->tm_year - 80) << 25) | ((lt->tm_mon + 1) << 21) |
                        (lt->tm_mday << 16) | (lt->tm_hour << 11) |
                        (lt->tm_min << 5) | (lt->tm_sec >> 1);

    lastHeader = f.tell(); // ftell_x(fp);

    int fl = entry.name.length();
    auto* e = reinterpret_cast<CentralDirEntry*>(entryPtr);

    bool ext64 = force64;
    if (!ext64)
        ext64 = (entry.dataSize > 0xfffffffe ||
                 entry.originalSize > 0xfffffffe || lastHeader > 0xfffffffeL);

    memset(e, 0, sizeof(CentralDirEntry));
    e->sig = 0x02014b50;
    e->v1 = 20;
    e->method = entry.store ? 0 : 8;
    e->crc = entry.crc;
    e->compSize = ext64 ? 0xffffffff : entry.dataSize;
    e->uncompSize = ext64 ? 0xffffffff : entry.originalSize;
    e->nameLen = fl;
    e->offset = ext64 ? 0xffffffff : lastHeader;
    e->dateTime = msdos_ts;
    e->attr1 = entry.flags << 16;

    entryPtr += sizeof(CentralDirEntry);
    memcpy(entryPtr, entry.name.c_str(), fl);
    entryPtr += fl;

    // ext64 = true;

    if (ext64) {
        e->v1 = 45;
        e->exLen += sizeof(Extra64);
        extra64.compSize = entry.dataSize;
        extra64.uncompSize = entry.originalSize;
        extra64.offset = lastHeader;
        memcpy(entryPtr, &extra64, sizeof(Extra64));
        entryPtr += sizeof(Extra64);
    }

    entryCount++;

    // printf("Added %d files in %ld bytes\n", entryCount, entryPtr - entries);

    // lastHeader 30 bytes
    head.method = entry.store ? 0 : 8;
    head.crc = entry.crc;
    head.v1 = 20;
    head.compSize = ext64 ? 0xffffffff : entry.dataSize;
    head.uncompSize = ext64 ? 0xffffffff : entry.originalSize;
    head.dateTime = msdos_ts;
    head.nameLen = fl;
    head.exLen = 0;

    if (entry.store && zipAlign)
        head.exLen = 4 - (lastHeader + fl + sizeof(LocalEntry)) % 4;

    if (ext64) {
        head.exLen = sizeof(Extra64);
        head.v1 = 45;
    }

    write(head);
    write(entry.name);

    if (ext64) {
        write(extra64);
    } else if (head.exLen > 0) {
        write(zeroes, head.exLen);
    }
    if (entry.data)
        write(entry.data.get(), entry.dataSize);
}

void ZipArchive::close()
{
    auto startCD = f.tell(); // tell_x(fp);

    write(entries.get(), entryPtr - entries.get());
    auto sizeCD = f.tell() - startCD;

    auto endCD = f.tell();

    bool end64 = force64;
    if (!end64) {
        if (entryCount > 0xfffe || startCD > 0xfffffffeL)
            end64 = true;
    }

    if (end64) {
        write<EndOfCentralDir64>({0x06064b50, 44, 0x031e, 45, 0, 0, entryCount,
                                  entryCount, (int64_t)sizeCD,
                                  (int64_t)startCD});
        // Locator
        write<uint32_t>(0x07064b50);
        write<uint32_t>(0);
        write<uint64_t>(endCD);
        write<uint32_t>(1);
    }
    write<uint32_t>(0x06054b50);
    write<uint16_t>(0);
    write<uint16_t>(0);
    write<uint16_t>(end64 ? 0xffff : entryCount);
    write<uint16_t>(end64 ? 0xffff : entryCount);
    write<uint32_t>(sizeCD);
    write<uint32_t>(end64 ? 0xffffffff : startCD);
    write<uint16_t>(0);

    f.close();
}
