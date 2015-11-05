#include "deflate.h"
#include <cstdio>
#include <cassert>
#include <cstdint>

void error(ZCONST char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

void flush_outbuf(char *o_buf, unsigned *o_idx)
{
    printf("FLUSH?? %p %p\n", o_buf, o_idx);
    // Not sure
}

// Random access to output data?
int seekable()
{
    return 1;
}

struct BufData
{
    char *in_buf;
    unsigned in_size;
    unsigned in_offset;
    IZDeflate *zid;
    int earlyOut;
    bool nopack;
    bool fail;
};

local unsigned mem_read(void *handle, char *b, unsigned bsize)
{
    // printf("Getting %d bytes\n", bsize);

    BufData *bufdata = (BufData*)handle;

    char *wp = bufdata->zid->out_buf + bufdata->zid->out_offset;
    char *rp = bufdata->in_buf + bufdata->in_offset;

    // This will normally never trigger
    if ((wp + 32768 > rp) && (bufdata->zid->out_buf < bufdata->in_buf))
    {
        bufdata->fail = true;
        return 0;
    }

    if (bufdata->earlyOut && (wp < rp) && (wp + 65536 >= bufdata->in_buf))
    {
        int ratio = (uint64_t)(bufdata->zid->out_offset + 1) * 100 / (bufdata->in_offset + 1);
        // printf("RATIO %d%% at overwrite position\n", ratio);
        if (ratio >= bufdata->earlyOut)
        {
            bufdata->nopack = true;
            return 0;
        }
        bufdata->earlyOut = 0;
    }

    // printf("WRITE: %p START %p READ: %p -- DELTA %d\n", wp, bufdata->in_buf, rp, (int)(rp - wp));

    if (bufdata->in_offset < bufdata->in_size)
    {
        ulg block_size = bufdata->in_size - bufdata->in_offset;
        if (block_size > (ulg)bsize)
            block_size = (ulg)bsize;
        memcpy(b, bufdata->in_buf + bufdata->in_offset, (unsigned)block_size);
        bufdata->in_offset += (unsigned)block_size;
        return (unsigned)block_size;
    }
    else
    {
        return 0; /* end of input */
    }
}

// INTERFACE

int64_t iz_deflate(int level, char *tgt, char *src, ulg tgtsize, ulg srcsize, int earlyOut)
{
    ush att = (ush)UNKNOWN;
    ush flags = 0;
    ulg crc;
    unsigned long out_total = 0;
    int method = DEFLATE;

    IZDeflate zid;
    BufData buf;
    buf.in_buf = src;
    buf.in_size = (unsigned)srcsize;
    buf.in_offset = 0;
    buf.zid = &zid;
    buf.nopack = buf.fail = false;
    buf.earlyOut = earlyOut;

    zid.read_buf = mem_read;
    zid.read_handle = &buf;
    zid.window_size = 0L;
    zid.level = level;
    memset(zid.window, 0, sizeof(zid.window));

    char save = src[0];

    zid.bi_init(tgt, (unsigned)(tgtsize), FALSE);
    zid.ct_init(&att, &method);
    zid.lm_init((zid.level != 0 ? zid.level : 1), &flags);
    out_total += (unsigned)zid.deflate();

    if (buf.fail)
        return -1;

    char *wp = tgt + out_total;
    char *rp = src + srcsize;

    int ratio = (uint64_t)(zid.out_offset + 1) * 100 / (buf.in_offset + 1);
    if (buf.nopack || (buf.earlyOut && (ratio >= earlyOut) && (wp < src)))
    {
        // printf("RATIO %d%% at end\n", ratio);
        assert(save == src[0]);
        return -2; // Early out pack cancel
    }

    zid.window_size = 0L; /* was updated by lm_init() */

    return out_total;
}
