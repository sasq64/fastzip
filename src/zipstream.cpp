#include "zipstream.h"

#include "utils.h"
#include "zipformat.h"
#include <cassert>
#include <sys/stat.h>
#include <ctime>

ZipStream::ZipStream(const std::string& zipName) : zipName(zipName), f{zipName}
{
    uint32_t id = 0;
    //auto* fp = f.filePointer();
    f.seek(-22 + 5, SEEK_END);
    int counter = 64 * 1024 + 8;
    while (id != EndOfCD_SIG && counter > 0) {
        f.seek(-5, SEEK_CUR);
        id = f.Read<uint32_t>();
        counter--;
    }
    if (counter <= 0) {
        f.close();
        return;
    }

    auto start = f.tell();
    f.seek(4, SEEK_CUR);
    int64_t entryCount = f.Read<uint16_t>();
    f.seek(2, SEEK_CUR);
    /* auto cdSize = */ f.Read<uint32_t>();
    int64_t cdOffset = f.Read<uint32_t>();
    auto commentLen = f.Read<uint16_t>();
    if (commentLen > 0) {
        comment = new char[commentLen + 1];
        f.Read(comment, commentLen);
        comment[commentLen] = 0;
    }

    if (entryCount == 0xffff || cdOffset == 0xffffffff) {
        f.seek(start - 6 * 4, SEEK_SET);
        auto id = f.Read<uint32_t>();
        if (id != EndOfCD64Locator_SIG) {
            return;
        }
        f.seek(4, SEEK_CUR);
        auto cdStart = f.Read<int64_t>();
        f.seek(cdStart, SEEK_SET);
        auto eocd64 = f.Read<EndOfCentralDir64>();

        cdOffset = eocd64.cdoffset;
        entryCount = eocd64.entries;
    }

    entries.reserve(entryCount);

    f.seek(cdOffset, SEEK_SET);
    char fileName[65536];
    for (auto i = 0L; i < entryCount; i++) {
        auto cd = f.Read<CentralDirEntry>();
        auto rc = f.Read(&fileName, cd.nameLen);
        fileName[rc] = 0;
        f.seek(cd.nameLen - rc, SEEK_CUR);
        int64_t offset = cd.offset;
        int exLen = cd.exLen;
        Extra extra {};
        while (exLen > 0) {
            f.Read((uint8_t*)&extra, 4);
            f.Read(extra.data, extra.size);
            if (extra.id == 0x01) {
                offset = extra.zip64.offset;
            } else if(extra.id == 0x7875) {
				int i = 1;
				auto uidSize = extra.data[i++];
				int32_t uid = 0;
				while(uidSize > 0) {
					uid <<= 8;
					uid |= extra.data[i++];
					uidSize--;
				}
				auto gidSize = extra.data[i++];
				int32_t gid = 0;
				while(gidSize > 0) {
					gid <<= 8;
					gid |= extra.data[i++];
					gidSize--;
				}
				// TODO: Flip 
				//printf("UID %x GID %x\n", uid, gid);
			} else if(extra.id == 0x5455) {
				// TODO: Read timestamps
			} else
                printf("**Warning: Ignoring extra block %04x\n", extra.id);

            exLen -= (extra.size + 4);
        }

        f.seek(cd.commLen, SEEK_CUR);
        auto flags = cd.attr1 >> 16;
        if ((flags & S_IFREG) == 0) // Some archives have broken attributes
            flags = 0;
        entries.emplace_back(fileName, offset + 0, flags);
    }
}

ZipStream::~ZipStream()
{
    delete[] comment;
}
