#include "asn.h"

namespace asn1 {
using namespace std;

void dumpDER(MemBuffer mb, int level, int offset)
{
    char spaces[] =
        "                                                                     ";
    spaces[level * 4] = 0;
    while (!mb.done()) {
        unsigned int offs = mb.pos() + offset;
        auto p = mb.readDERHead();
        int o = mb.pos();
        MemBuffer mb2 = mb.readBuffer(p.second);
        printf("%04x : %s%02x %04x : ", offs, spaces, p.first, p.second);

        if (p.first == 0x13 || p.first == 0x17) {
            printf("\"%s\" \n", mb2.readString(p.second).c_str());
        } else if (p.first == 0x02) {
            unsigned v = 0;
            switch (p.second) {
            case 1:
                v = mb2.read<uint8_t>();
                break;
            case 2:
                v = mb2.read<uint16_t>();
                break;
            case 4:
                v = mb2.read<uint32_t>();
                break;
            }
            printf("%x\n", v);
        } else if (p.first == 03 || p.first == 4 || p.first == 6) {
            for (int i = 0; i < p.second; i++)
                printf("%02x ", mb2.read<uint8_t>());
            printf("\n");
        } else if (p.first >= 0x30) {
            printf("\n");
            dumpDER(mb2, level + 1, o);
        } else if (p.first == 5)
            printf("NULL\n");
        else
            printf("<data>\n");
    }
}

void dumpDER(const vector<DER>& v, int level)
{
    char spaces[] =
        "                                                                     ";
    spaces[level * 4] = 0;
    for (auto& der : v) {
        printf("%s%02x : %04x : ", spaces, der.tag, (int)der.data.size());

        if (der.tag == 0x13 || der.tag == 0x17) {
            printf("\"%s\" \n", der.text.c_str());
        } else if (der.tag == 0x02) {
            printf("%llx\n", (unsigned long long)der.value);
        } else if (der.tag == 03 || der.tag == 4 || der.tag == 6) {
            for (auto b : der.data)
                printf("%02x ", b);
            printf("\n");
        } else if (der.tag >= 0x30) {
            printf("\n");
            dumpDER(der.children, level + 1);
        } else if (der.tag == 5)
            printf("NULL\n");
        else
            printf("<data>\n");
    }
}

void dumpDER(DER& root)
{
    vector<DER> d{root};
    dumpDER(d, 0);
}

void readDER(const vector<uint8_t>& data, vector<DER>& v)
{
    MemBuffer mb{data};
    while (!mb.done()) {
        auto p = mb.readDERHead();
        MemBuffer mb2 = mb.readBuffer(p.second);

        DER target;

        target.tag = p.first;
        target.data = mb2.buffer();
        target.value = 0;

        if (p.first == 0x13 || p.first == 0x17) {
            target.text = mb2.readString(p.second);
        } else if (p.first == 0x02) {
            switch (p.second) {
            case 1:
                target.value = mb2.read<uint8_t>();
                break;
            case 2:
                target.value = mb2.read<uint16_t>();
                break;
            case 4:
                target.value = mb2.read<uint32_t>();
                break;
            }
        } else if (p.first == 03 || p.first == 4 || p.first == 6) {
        } else if (p.first >= 0x30) {
            readDER(mb2.buffer(), target.children);
        } else if (p.first == 5) {
        }
        v.push_back(target);
    }
}

DER readDER(const vector<uint8_t>& data)
{
    vector<DER> d;
    readDER(data, d);
    return d[0];
}

typedef std::vector<uint8_t> vec8;

void pushHead(vec8& vec, uint8_t tag, uint32_t len)
{
    vec.push_back(tag);
    if (len <= 0x7f)
        vec.push_back(len);
    else {
        if (len <= 0xff) {
            vec.push_back(0x81);
            vec.push_back(len);
        } else if (len <= 0xffff) {
            vec.push_back(0x82);
            vec.push_back(len >> 8);
            vec.push_back(len & 0xff);
        }
    }
}

void mkSEQ(vec8&) {}

vec8 mkINT(uint64_t val)
{
    vec8 out;
    uint8_t temp[8];
    pushHead(out, 0x2, 0);
    if (val == 0) {
        out.push_back(0);
    } else {
        int i = 8;
        while (val) {
            temp[--i] = (val & 0xff);
            val >>= 8;
        }
        for (; i < 8; i++)
            out.push_back(temp[i]);
    }

    out[1] = (uint8_t)out.size() - 2;

    return out;
}

vec8 mkBIN(uint8_t tag, const vec8& v)
{
    vec8 out;
    pushHead(out, tag, v.size());
    out.insert(out.end(), v.begin(), v.end());
    return out;
}

vec8 mkSTR(uint8_t tag, const std::string& text)
{
    vec8 out;
    pushHead(out, tag, text.size());
    out.insert(out.end(), text.begin(), text.end());
    return out;
}

vec8 mkNIL()
{
    return vec8{0x5, 0x00};
}
} // namespace asn1
