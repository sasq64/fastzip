#include "infozip/deflate.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

void error(ZCONST char* msg)
{
    fprintf(stderr, "%s\n", msg);
}

void flush_outbuf(char* o_buf, unsigned* o_idx)
{
    printf("FLUSH?? %p %p\n", o_buf, (void*)o_idx);
    // Not sure
}

// Random access to output data?

int seekable()
{
    return 1;
}

struct BufData
{
    char* in_buf;
    unsigned in_size;
    unsigned in_offset;
    IZDeflate* zid;
    bool fail;
};

local unsigned mem_read(void* handle, char* target, unsigned size)
{
    BufData* bufdata = (BufData*)handle;

    char* wp = bufdata->zid->out_buf + bufdata->zid->out_offset;
    char* rp = bufdata->in_buf + bufdata->in_offset;

    // This will normally never trigger
    if ((wp + 32768 > rp) && (bufdata->zid->out_buf < bufdata->in_buf)) {
        bufdata->fail = true;
        return 0;
    }

    if (bufdata->in_offset >= bufdata->in_size)
        return 0;
    ulg left = bufdata->in_size - bufdata->in_offset;
    if (left < (ulg)size)
        size = left;
    memcpy(target, rp, size);
    bufdata->in_offset += size;

    return size;
}

/*

--- > bytes

|76543210|FEDCBA98|NMLKJIHG|VUTSRQPO|

load 16bit at a time from back


|VUTSRQPO|NMLKJIHG|
RS
|..VUTSRQ|PONMLKJI| x |HG|

|FEDCBA98|76543210|




Shift each byte right

Next buffer: Shift everything 3 bits to the left

*/

/*

   If prevous buffer had 3 bits left ufilled;
    Shift this buffer 3 bits left
    OR the return value into the last word of the previous buffer (possibly
   anding by 0xffffffffffffffff >> 3) first)
*/

// INTERFACE

int64_t iz_deflate(int level, char* tgt, char* src, ulg tgtsize, ulg srcsize)
{
    ush att = (ush)UNKNOWN;
    ush flags = 0;
    unsigned long out_total = 0;
    int method = DEFLATE;

    IZDeflate zid;
    BufData buf;
    buf.in_buf = src;
    buf.in_size = (unsigned)srcsize;
    buf.in_offset = 0;
    buf.zid = &zid;
    buf.fail = false;

    zid.read_buf = mem_read;
    zid.read_handle = &buf;
    zid.window_size = 0L;
    zid.level = level;
    memset(zid.window, 0, sizeof(zid.window));

    zid.bi_init(tgt, (unsigned)(tgtsize), FALSE);
    zid.ct_init(&att, &method);
    zid.lm_init((zid.level != 0 ? zid.level : 1), &flags);
    out_total = (unsigned)zid.deflate();

    if (buf.fail)
        return -1;

    if (method == STORE)
        return -2;

    // printf("Last bits: %d\n", zid.last_bits);
    return ((out_total - 1) << 3) + zid.last_bits;
}
