#pragma once
#include <cstdint>
#include <ctime>

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
enum
{
    LocalEntry_SIG = 0x04034b50,
    CentralDirEntry_SIG = 0x02014b50,
    EndOfCD64_SIG = 0x06064b50,
    EndOfCD64Locator_SIG = 0x07064b50,
    EndOfCD_SIG = 0x06054b50,
};

#ifdef _WIN32
#    define PACK
#    pragma pack(1)
#else
#    define PACK __attribute__((packed))
#endif

struct PACK LocalEntry
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

struct PACK CentralDirEntry
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

struct PACK Extra64
{
    uint16_t id;
    uint16_t size;
    int64_t uncompSize;
    int64_t compSize;
    int64_t offset;
    uint32_t disk;
};

struct PACK EndOfCentralDir
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

struct PACK EndOfCentralDir64
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

struct PACK UnixExtra
{
    uint32_t Atime;
    uint32_t Mtime;
    int16_t Uid;
    int16_t Gid;
    uint8_t var[1];
};

struct PACK Unix2Extra
{
    uint8_t ver;
    uint8_t ulen;
    int32_t UID;
    uint8_t glen;
    int32_t GID;
};

struct PACK Zip64Extra
{
    int64_t uncompSize;
    int64_t compSize;
    int64_t offset;
    uint32_t disk;
};

struct PACK Extra
{
    uint16_t id;
    uint16_t size;
    union PACK
    {
        UnixExtra unix;
        Unix2Extra unix2;
        Zip64Extra zip64;
        uint8_t data[0xffff];
    };
};

#ifdef _WIN32
#    pragma pack()
#endif
