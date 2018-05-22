#pragma once

#include "file.h"
#include <cstdint>

struct zip_exception
{
    zip_exception(const std::string& msg) : msg(msg) {}
    std::string msg;
};

struct ZipEntry
{
    std::string name;
    bool store;
    uint8_t* data;
    uint64_t dataSize;
    uint64_t originalSize;
    uint32_t crc;
    time_t timeStamp;
    uint16_t flags;
    int uid;
    int gid;
};

class ZipArchive
{
public:
    ZipArchive(const std::string& fileName, int numFiles = 0, int strLen = 0);

    ~ZipArchive();
    void doAlign(bool align) { zipAlign = align; }
    void doForce64(bool f64) { force64 = f64; }

    void addFile(const std::string& fileName, bool store = false, uint64_t compSize = 0,
                 uint64_t uncompSize = 0, time_t ts = 0, uint32_t crc = 0,
                 uint16_t flags = 0);
    void add(const ZipEntry& entry);
    void close();
    void write(const uint8_t* data, uint64_t size) { f.Write(data, size); }

private:
    // NOTE: In case you need to support big endian you need to overload write()
    template <typename T> void write(const T& t) { f.Write(t); }
    void write(const std::string& s) { f.Write(s.c_str(), s.length()); }

    bool zipAlign = false;
    bool force64 = false;

    uint8_t* entries;
    uint8_t* entryPtr;
    uint64_t entryCount;
    File f;
    uint64_t lastHeader;
};
