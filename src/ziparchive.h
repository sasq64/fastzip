#ifndef ZIPARCHIVE_H
#define ZIPARCHIVE_H

#include "utils.h"

#include <cassert>
#include <time.h>
#include <sys/stat.h>
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
    int64_t uncompSize;
    int64_t compSize;
    int64_t offset;
    uint32_t disk;
};

struct __attribute__((packed)) EndOfCentralDir
{
	uint32_t id;
	uint16_t disk;
	uint16_t cddisk;
	uint16_t diskentries;
	uint16_t entries;
	uint32_t cdsize;
	uint32_t cdoffset;
	uint16_t commlen;
};

struct __attribute__((packed)) EndOfCentralDir64
{
	uint32_t id;
	uint64_t size;
	uint16_t v0;
	uint16_t v1;
	uint32_t disk;
	uint32_t cddisk;
	uint64_t diskentries;
	uint64_t entries;
	int64_t cdsize;
	int64_t cdoffset;
};

struct __attribute__((packed)) UnixExtra
{
	uint32_t Atime;
	uint32_t Mtime;
	int16_t Uid;
	int16_t Gid;
	uint8_t var[0];
};

struct __attribute__((packed)) Unix2Extra
{
	uint8_t ver;
	uint8_t ulen;
	int32_t UID;
	uint8_t glen;
	int32_t GID;
};

struct __attribute__((packed)) Zip64Extra
{
    int64_t uncompSize;
    int64_t compSize;
    int64_t offset;
    uint32_t disk;
};

struct __attribute__((packed)) Extra
{
	uint16_t id;
	uint16_t size;
	union __attribute__((packed)) {
		UnixExtra unix;
		Unix2Extra unix2;
		Zip64Extra zip64;
		uint8_t data[0xffff];
	};
};


struct ZipEntry
{
    std::string name;
    bool store;
    uint8_t *data;
    uint64_t dataSize;
    uint64_t originalSize;
    uint32_t crc;
    time_t timeStamp;
	uint16_t flags;
	int uid;
	int gid;
    // uint8_t sha[SHA_LEN];
};

struct zip_exception
{
	zip_exception(const std::string &msg) : msg(msg) {}
	std::string msg;
};

inline void error(const std::string &msg)
{
	fprintf(stderr, "**Error: %s", (msg + "\n").c_str());
	exit(1);
}

class ZipStream
{
public:
    struct Entry
    {
        Entry(const std::string &name, int64_t offset, uint16_t flags) : name(name), offset(offset), flags(flags), data(nullptr) {}
        std::string name;
        int64_t offset;
		uint16_t flags;
		void *data;
    };

    std::vector<Entry> entries;
	char* comment = nullptr;

    ZipStream(const std::string &zipName) : zipName(zipName)
    {
        fp = fopen(zipName.c_str(), "rb");
		if (!fp)
			return;
        uint32_t id = 0;
        fseek_x(fp, -22 + 5, SEEK_END);
        while (id != 0x06054b50)
        {
            fseek_x(fp, -5, SEEK_CUR);
            id = read<uint32_t>();
        }
		auto start = ftell_x(fp);
        fseek_x(fp, 4, SEEK_CUR);
        int64_t entryCount = read<uint16_t>();
        fseek_x(fp, 2, SEEK_CUR);
        auto cdSize = read<uint32_t>();
        int64_t cdOffset = read<uint32_t>();
		auto commentLen = read<uint16_t>();
		if (commentLen > 0)
		{
			comment = new char [commentLen+1];
			fread(comment, 1, commentLen, fp);
			comment[commentLen] = 0;
		}

		if(entryCount == 0xffff || cdOffset == 0xffffffff)
		{
			fseek_x(fp, start - 6*4, SEEK_SET);
			auto id = read<uint32_t>();
			if(id != 0x07064b50)
			{
				error("Illegal 64bit format");
			}
			fseek_x(fp, 4, SEEK_CUR);
			auto cdStart = read<int64_t>();
			fseek_x(fp, cdStart, SEEK_SET);
			auto eocd64 = read<EndOfCentralDir64>();

			cdOffset = eocd64.cdoffset;	
			entryCount = eocd64.entries;
		}

		entries.reserve(entryCount);

        fseek_x(fp, cdOffset, SEEK_SET);
        char fileName[65536];
        CentralDirEntry cd;
        for (auto i = 0L; i < entryCount; i++)
        {
            fread(&cd, 1, sizeof(CentralDirEntry), fp);
            int rc = fread(&fileName, 1, cd.nameLen , fp);
            fileName[rc] = 0;
            fseek_x(fp, cd.nameLen - rc, SEEK_CUR);
			int64_t offset = cd.offset;
			int exLen = cd.exLen;
			Extra extra;
			while(exLen > 0)
			{
				fread(&extra, 1, 4, fp);
				fread(extra.data, 1, extra.size, fp);
				if(extra.id == 0x01)
				{
					offset = extra.zip64.offset;
				}
				exLen -= (extra.size+4);
			}

            fseek_x(fp, cd.commLen, SEEK_CUR);
            entries.emplace_back(fileName, offset + 0, cd.attr1 >> 16);
        }
    }

	~ZipStream()
	{
		if (fp)
			fclose(fp);
		if (comment)
			delete [] comment;
	}

	bool valid() const { return fp != nullptr; }

    int size() const { return entries.size(); }
    Entry& getEntry(int i) { return entries[i]; }
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

inline void readExtra(FILE *fp, int exLen)
{
}

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
    uint64_t entryCount;
    bool zipAlign = false;
    bool force64 = false;

    ~ZipArchive()
    {
        if (fp)
            close();
    }

    void doAlign(bool align) { zipAlign = align; }

	// NOTE: In case you need to support big endian you need to overload write()
    template<typename T> void write(const T &t) { fwrite(&t, 1, sizeof(T), fp); }

    void write(const std::string &s) { fwrite(s.c_str(), 1, s.length(), fp); }

    void write(const uint8_t *data, uint64_t size) { fwrite(data, 1, size, fp); }

    void addFile(const std::string &fileName, bool store = false, uint64_t compSize = 0,
        uint64_t uncompSize = 0, time_t ts = 0, uint32_t crc = 0, uint16_t flags = 0)
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
		add(ze);
	}
    void add(const ZipEntry &entry)
    {
        static LocalEntry head = {0x04034b50, 10, 0, 0, 0, 0, 0, 0, 0, 0};
        static Extra64 extra64 = {0x1, 28, 0, 0, 0, 0};
        static const uint8_t zeroes[] = {0, 0, 0, 0};
        const struct tm *lt = localtime(&entry.timeStamp);
        // date:   YYYYYYYM MMMDDDDD
        // time:   HHHHHMMM MMMSSSSS
        uint32_t msdos_ts = ((lt->tm_year - 80) << 25) | ((lt->tm_mon + 1) << 21) |
            (lt->tm_mday << 16) | (lt->tm_hour << 11) | (lt->tm_min << 5) |
            (lt->tm_sec >> 1);

        lastHeader = ftell_x(fp);

        int fl = entry.name.length();
        CentralDirEntry *e = (CentralDirEntry*)entryPtr;

		bool ext64 = force64;
		if(!ext64)
        	ext64 = (entry.dataSize > 0xfffffffe || entry.originalSize > 0xfffffffe || lastHeader > 0xfffffffeL);

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
		
		//ext64 = true;

        if (ext64)
        {
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

        if (ext64)
        {
            head.exLen = sizeof(Extra64);
            head.v1 = 45;
        }

        write(head);
        write(entry.name);

        if (ext64)
        {
            write(extra64);
        }
        else if (head.exLen > 0)
        {
            write(zeroes, head.exLen);
        }
		if (entry.data)
	        write(entry.data, entry.dataSize);
    }

    void close()
    {
        auto startCD = ftell_x(fp);

        write(entries, entryPtr - entries);
        auto sizeCD = ftell_x(fp) - startCD;

        auto endCD = ftell_x(fp);

		bool end64 = force64;
		if(!end64)
		{
		   	if(entryCount > 0xfffe || startCD > 0xfffffffeL)
	            end64 = true;
		}

		if(end64)
		{
			EndOfCentralDir64 eod64 = {
				0x06064b50, 44, 0x031e, 45, 0, 0, entryCount, entryCount, sizeCD, startCD
			};
			write(eod64);
            // Locator
            write<uint32_t>(0x07064b50);
            write<uint32_t>(0);
            write<uint64_t>(endCD);
            write<uint32_t>(1);
		}
/*
        if (end64)
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
*/
        write<uint32_t>(0x06054b50);
        write<uint16_t>(0);
        write<uint16_t>(0);
        write<uint16_t>(end64 ? 0xffff : entryCount);
        write<uint16_t>(end64 ? 0xffff : entryCount);
        write<uint32_t>(sizeCD);
        write<uint32_t>(end64 ? 0xffffffff : startCD);

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
