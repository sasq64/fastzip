#include "zipstream.h"

#include "utils.h"
#include "zipformat.h"
#include <cassert>
#include <ctime>
#include <sys/stat.h>

ZipStream::ZipStream(const std::string& zipName) : zipName_(zipName), f_{zipName}
{
    uint32_t id = 0;
    // Find CD by scanning backwards from end
    f_.seek(-22 + 5, SEEK_END);
    int counter = 64 * 1024 + 8;
    while (id != EndOfCD_SIG && counter > 0) {
        f_.seek(-5, SEEK_CUR);
        id = f_.Read<uint32_t>();
        counter--;
    }
    if (counter <= 0) {
        f_.close();
        return;
    }

    auto start = f_.tell();
    f_.seek(4, SEEK_CUR);
    int64_t entryCount = f_.Read<uint16_t>();
    f_.seek(2, SEEK_CUR);
    /* auto cdSize = */ f_.Read<uint32_t>();
    int64_t cdOffset = f_.Read<uint32_t>();
    auto commentLen = f_.Read<uint16_t>();
    if (commentLen > 0) {
        comment_ = std::make_unique<char[]>(commentLen + 1);
        f_.Read(comment_.get(), commentLen);
        comment_[commentLen] = 0;
    }

    if (entryCount == 0xffff || cdOffset == 0xffffffff) {
        // Find zip64 data
        f_.seek(start - 6 * 4, SEEK_SET);
        id = f_.Read<uint32_t>();
        if (id != EndOfCD64Locator_SIG) {
            return;
        }
        f_.seek(4, SEEK_CUR);
        auto cdStart = f_.Read<int64_t>();
        f_.seek(cdStart, SEEK_SET);
        auto eocd64 = f_.Read<EndOfCentralDir64>();

        cdOffset = eocd64.cdoffset;
        entryCount = eocd64.entries;
    }

    entries_.reserve(entryCount);

    f_.seek(cdOffset, SEEK_SET);
    char fileName[65536];
    for (auto i = 0L; i < entryCount; i++) {
        auto cd = f_.Read<CentralDirEntry>();
        auto rc = f_.Read(&fileName, cd.nameLen);
        fileName[rc] = 0;
        f_.seek(cd.nameLen - rc, SEEK_CUR);
        int64_t offset = cd.offset;
        int exLen = cd.exLen;
        Extra extra{};
        while (exLen > 0) {
            f_.Read((uint8_t*)&extra, 4);
            f_.Read(extra.data, extra.size);
            if (extra.id == 0x01) {
                offset = extra.zip64.offset;
            } else
                printf("Ignoring extra block %04x", extra.id);

            exLen -= (extra.size + 4);
        }

        f_.seek(cd.commLen, SEEK_CUR);
        auto flags = cd.attr1 >> 16;
        if ((flags & S_IFREG) == 0) // Some archives have broken attributes
            flags = 0;
        entries_.emplace_back(fileName, offset, flags);
    }
}

