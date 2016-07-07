#ifndef ZIPARCHIVE_H
#define ZIPARCHIVE_H

#include "utils.h"

#include <cassert>
#include <time.h>

/*
    local file header signature     4 bytes  (0x04034b50)
      version needed to extract       2 bytes
      general purpose bit flag        2 bytes

      compression method              2 bytes
      last mod file time              2 bytes
      last mod file date              2 bytes
      crc-32                          4 bytes
      compressed size                 4 bytes
      uncompressed size               4 bytes
      file name length                2 bytes
      extra field length              2 bytes

      file name (variable size)
      extra field (variable size)

    //date:   YYYYYYYM MMMDDDDD
    //time:   HHHHHMMM MMMSSSSS

     central file header signature   4 bytes  (0x02014b50)
        version made by                 2 bytes
        version needed to extract       2 bytes
        general purpose bit flag        2 bytes
        compression method              2 bytes
        last mod file time              2 bytes
        last mod file date              2 bytes
        crc-32                          4 bytes
        compressed size                 4 bytes
        uncompressed size               4 bytes
        file name length                2 bytes
        extra field length              2 bytes
        file comment length             2 bytes
        disk number start               2 bytes
        internal file attributes        2 bytes
        external file attributes        4 bytes
        relative offset of local header 4 bytes
*/

struct __attribute__((packed)) LocalEntry
{
    uint32_t sig;
    uint16_t v1;
    uint16_t bits;
    uint16_t method;
    uint32_t dateTime;
    uint32_t crc;
    uint32_t compSize;
    uint32_t uncompSize;
    uint16_t nameLen;
    uint16_t exLen;
};

struct __attribute__((packed)) CentralDirEntry
{
    uint32_t sig;
    uint16_t v0;
    uint16_t v1;
    uint16_t bits;
    uint16_t method;
    uint32_t dateTime;
    uint32_t crc;
    uint32_t compSize;
    uint32_t uncompSize;
    uint16_t nameLen;
    uint16_t exLen;
    uint16_t commLen;
    uint16_t diskNo;
    uint16_t attr0;
    uint32_t attr1;
    uint32_t offset;
};

struct __attribute__((packed)) Extra64
{
    uint16_t id;
    uint16_t size;
    uint64_t uncompSize;
    uint64_t compSize;
    uint64_t offset;
    uint32_t disk;
};

struct ZipEntry
{
    std::string name;
    bool store;
    uint8_t *data;
    int dataSize;
    int originalSize;
    uint32_t crc;
    uint32_t timeStamp;
    // uint8_t sha[SHA_LEN];
};

class ZipStream
{
public:
    struct Entry
    {
        Entry(const std::string &name, uint32_t offset) : name(name), offset(offset) {}
        std::string name;
        uint32_t offset;
    };

    std::vector<Entry> entries;

    ZipStream(const std::string &zipName) : zipName(zipName)
    {
        fp = fopen(zipName.c_str(), "rb");
        uint32_t id = 0;
        // TODO: Read entire 64K+22 bytes when we need to skip comment
        fseek_x(fp, -22 + 5, SEEK_END);
        while (id != 0x06054b50)
        {
            fseek_x(fp, -5, SEEK_CUR);
            id = read<uint32_t>();
        }
        fseek_x(fp, 4, SEEK_CUR);
        int entryCount = read<uint16_t>();
        fseek_x(fp, 2, SEEK_CUR);
        auto cdSize = read<uint32_t>();
        auto cdOffset = read<uint32_t>();
        // printf("%d entries at offset %x\n", entryCount, cdOffset);
        fseek_x(fp, cdOffset, SEEK_SET);
        char fileName[2048];
        CentralDirEntry cd;
        for (int i = 0; i < entryCount; i++)
        {
            fread(&cd, 1, sizeof(CentralDirEntry), fp);
            int rc =
                fread(&fileName, 1,
                    cd.nameLen > sizeof(fileName) - 1 ? sizeof(fileName) - 1 : cd.nameLen, fp);
            fileName[rc] = 0;
            fseek_x(fp, cd.nameLen - rc, SEEK_CUR);
            fseek_x(fp, cd.exLen + cd.commLen, SEEK_CUR);
            // printf("%d: %s\n", i, fileName);
            entries.emplace_back(fileName, cd.offset + 0);
        }
    }

    int size() const { return entries.size(); }
    Entry getEntry(int i) { return entries[i]; }
    const Entry& getEntry(int i) const { return entries[i]; }

    template<typename T> T read()
    {
        T t;
        fread(&t, 1, sizeof(T), fp);
        return t;
    }

	FILE *copyFP() const {
		return fopen(zipName.c_str(), "rb");
		//return fdopen(dup(fileno(fp)), "r");
	}

private:
	std::string zipName;
    FILE *fp;
};

class ZipArchive
{
public:
    ZipArchive(const std::string &fileName, int numFiles = 0, int strLen = 0)
    {
        assert(sizeof(LocalEntry) == 30);
        assert(sizeof(CentralDirEntry) == 46);
        fp = fopen(fileName.c_str(), "wb");

        // printf("Reserving space for %d files in %d bytes = %ld total\n", numFiles, strLen, strLen
        // + numFiles * sizeof(CentralDirEntry));

        entries = new uint8_t[strLen + numFiles * (sizeof(CentralDirEntry) + sizeof(Extra64))];
        entryPtr = entries;
        entryCount = 0;
    }

    uint8_t *entries;
    uint8_t *entryPtr;
    int entryCount;
    bool zipAlign = false;
    bool zip64 = false;

    ~ZipArchive()
    {
        if (fp)
            close();
    }

    void doAlign(bool align) { zipAlign = align; }

    template<typename T> void write(const T &t) { fwrite(&t, 1, sizeof(T), fp); }

    void write(const std::string &s) { fwrite(s.c_str(), 1, s.length(), fp); }

    void write(const uint8_t *data, uint64_t size) { fwrite(data, 1, size, fp); }

    void add(const ZipEntry &entry)
    {
        addFile(entry.name, entry.store, entry.dataSize, entry.originalSize, entry.timeStamp,
            entry.crc);
        write(entry.data, entry.dataSize);
    }

    void addFile(const std::string &fileName, bool store = false, uint64_t compSize = 0,
        uint64_t uncompSize = 0, time_t ts = 0, uint32_t crc = 0)
    {
        static LocalEntry head = {0x04034b50, 10, 0, 0, 0, 0, 0, 0, 0, 0};
        struct Extra64 extra64 = {0x1, 32, 0, 0, 0, 0};
        static const uint8_t zeroes[] = {0, 0, 0, 0};
        const struct tm *lt = localtime(&ts);
        // date:   YYYYYYYM MMMDDDDD
        // time:   HHHHHMMM MMMSSSSS
        uint32_t msdos_ts = ((lt->tm_year - 80) << 25) | ((lt->tm_mon + 1) << 21) |
            (lt->tm_mday << 16) | (lt->tm_hour << 11) | (lt->tm_min << 5) |
            (lt->tm_sec >> 1);

        lastHeader = ftell_x(fp);

        int fl = fileName.length();
        CentralDirEntry *e = (CentralDirEntry*)entryPtr;

        bool ext64 = (compSize > 0xfffffffe || uncompSize > 0xfffffffe || lastHeader > 0xfffffffe);

        memset(e, 0, sizeof(CentralDirEntry));
        e->sig = 0x02014b50;
        e->v1 = 20;
        e->method = store ? 0 : 8;
        e->crc = crc;
        e->compSize = ext64 ? 0xffffffff : compSize;
        e->uncompSize = ext64 ? 0xffffffff : uncompSize;
        e->nameLen = fl;
        e->offset = ext64 ? 0xffffffff : lastHeader;
        e->dateTime = msdos_ts;

        entryPtr += sizeof(CentralDirEntry);
        memcpy(entryPtr, fileName.c_str(), fl);
        entryPtr += fl;

        if (ext64)
        {
            e->v1 = 45;
            e->exLen = sizeof(Extra64);
            extra64.compSize = compSize;
            extra64.uncompSize = uncompSize;
            extra64.offset = lastHeader;
            memcpy(entryPtr, &extra64, sizeof(Extra64));
            entryPtr += sizeof(Extra64);
        }

        entryCount++;

        // printf("Added %d files in %ld bytes\n", entryCount, entryPtr - entries);

        // lastHeader 30 bytes
        head.method = store ? 0 : 8;
        head.crc = crc;
        head.v1 = 20;
        head.compSize = ext64 ? 0xffffffff : compSize;
        head.uncompSize = ext64 ? 0xffffffff : uncompSize;
        head.dateTime = msdos_ts;
        head.nameLen = fl;
        head.exLen = 0;

        if (store && zipAlign)
            head.exLen = 4 - (lastHeader + fl + sizeof(LocalEntry)) % 4;

        if (ext64)
        {
            head.exLen = sizeof(Extra64);
            head.v1 = 45;
        }

        write(head);
        write(fileName);

        if (ext64)
        {
            write(extra64);
        }
        else if (head.exLen > 0)
        {
            write(zeroes, head.exLen);
        }
    }

    void close()
    {
        auto startCD = ftell_x(fp);

        write(entries, entryPtr - entries);
        auto sizeCD = ftell_x(fp) - startCD;

        auto endCD = ftell_x(fp);

        if (entryCount > 0xfffe)
            zip64 = true;

        if (zip64)
        {
            write<uint32_t>(0x06064b50);
            write<uint64_t>(44);
            write<uint16_t>(0x031e); // 03 = Unix, 0x1E = version 3.0
            write<uint16_t>(45);     // 4.5 = min version for zip64
            write<uint32_t>(0);
            write<uint32_t>(0);
            write<uint64_t>(entryCount);
            write<uint64_t>(entryCount);
            write<uint64_t>(sizeCD);
            write<uint64_t>(startCD);

            // Locator
            write<uint32_t>(0x07064b50);
            write<uint32_t>(0);
            write<uint64_t>(endCD);
            write<uint32_t>(1);
        }

        write<uint32_t>(0x06054b50);
        write<uint16_t>(0);
        write<uint16_t>(0);
        write<uint16_t>(zip64 ? 0xffff : entryCount);
        write<uint16_t>(zip64 ? 0xffff : entryCount);
        write<uint32_t>(sizeCD);
        write<uint32_t>(startCD);

        write<uint16_t>(0);

        fclose(fp);
        fp = nullptr;

        delete[] entries;
    }

    FILE *getFile() { return fp; }

private:
    FILE *fp;
    uint64_t lastHeader;
};

#endif // ZIPARCHIVE_H
