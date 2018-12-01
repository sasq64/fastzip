#pragma once

#include "file.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>

namespace asn1 {
// Routines for ASN.1 reading and creation

class MemBuffer
{
public:
    MemBuffer(const std::string& fileName)
    {
        struct stat ss;
        if (stat(fileName.c_str(), &ss) >= 0 && ss.st_size > 0) {
            data.resize(ss.st_size);
            auto f = File{fileName};
            f.Read(&data[0], ss.st_size);
            readpos = 0;
        }
    }

    MemBuffer(const std::vector<uint8_t>& vec) : data(vec) { readpos = 0; }

    void reset() { readpos = 0; }

    MemBuffer(int size = 0) : data(size) { readpos = 0; }

    std::vector<uint8_t>& buffer() { return data; }

    ~MemBuffer() {}

    template <typename T> T read()
    {
        T t;
        uint8_t* p = (uint8_t*)&t;
        for (unsigned i = 0; i < sizeof(T); i++)
            p[i] = data[readpos + sizeof(T) - 1 - i];
        readpos += sizeof(T);
        return t;
    }

    std::string readString(int len = -1)
    {
        if (len == -1)
            len = read<uint16_t>();
        char s[1024]; // TODO: Very bad!
        read(s, len);
        s[len] = 0;
        return std::string(s);
    }

    std::pair<int, int> readDERHead()
    {
        int id = read<uint8_t>();
        int len = read<uint8_t>();
        if (len & 0x80) {
            int l = len & 0x7f;
            len = 0;
            while (l--) {
                len <<= 8;
                len |= read<uint8_t>();
            }
        }
        return std::make_pair(id, len);
    }

    MemBuffer readDER()
    {
        auto p = readDERHead();
        return readBuffer(p.second);
    }

    MemBuffer& skipDER()
    {
        auto p = readDERHead();
        seek(p.second);
        return *this;
    }

    MemBuffer readBuffer(int size)
    {
        MemBuffer buf(size);
        if (size > 0)
            read(&buf.buffer()[0], size);
        return buf;
    }

    template <typename T> int read(T* target, int size)
    {
        int l = (sizeof(T) * size);
        memcpy((void*)target, &data[readpos], l);
        readpos += l;
        return l;
    }

    bool done() { return (readpos >= data.size()); }

    void seek(int s) { readpos += s; }

    auto pos() { return readpos; }
    auto size() { return data.size(); }

private:
    std::vector<uint8_t> data;
    uint64_t readpos;
};

struct DER
{
    uint8_t tag;
    std::vector<DER> children;
    std::vector<uint8_t> data;
    std::string text;
    uint64_t value;
    auto size() { return children.size(); }
    DER& operator[](const int& i) { return children[i]; }
    auto begin() { return children.begin(); }
    auto end() { return children.end(); }
};

void dumpDER(DER& root);

void dumpDER(MemBuffer mb, int level = 0, int offset = 0);
void readDER(MemBuffer mb, std::vector<DER>& v);
DER readDER(const std::vector<uint8_t>& data);

void pushHead(std::vector<uint8_t>& vec, uint8_t tag, uint32_t len);

void mkSEQ(std::vector<uint8_t>& v);

template <typename V0, typename... V>
void mkSEQ(std::vector<uint8_t>& v, const V0& v0, const V&... va)
{
    v.insert(v.end(), v0.begin(), v0.end());
    mkSEQ(v, va...);
}

template <typename... V> std::vector<uint8_t> mkSEQ(uint8_t tag, const V&... v)
{
    std::vector<uint8_t> vec2;
    mkSEQ(vec2, v...);

    std::vector<uint8_t> vec;
    pushHead(vec, tag, vec2.size());
    vec.insert(vec.end(), vec2.begin(), vec2.end());
    return vec;
}

std::vector<uint8_t> mkINT(uint64_t val);
std::vector<uint8_t> mkBIN(uint8_t tag, const std::vector<uint8_t>& v);
std::vector<uint8_t> mkSTR(uint8_t tag, const std::string& text);
std::vector<uint8_t> mkNIL();
} // namespace asn1
