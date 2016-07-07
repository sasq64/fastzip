#ifndef INFLATE_H
#define INFLATE_H

typedef unsigned long mz_ulong;
typedef void *(*mz_alloc_func)(void *opaque, size_t items, size_t size);
typedef void (*mz_free_func)(void *opaque, void *address);
typedef void *(*mz_realloc_func)(void *opaque, void *address, size_t items, size_t size);

// Compression/decompression stream struct.
typedef struct mz_stream_s
{
    const unsigned char *next_in; // pointer to next byte to read
    unsigned int avail_in;        // number of bytes available at next_in
    mz_ulong total_in;            // total number of bytes consumed so far

    unsigned char *next_out; // pointer to next byte to write
    unsigned int avail_out;  // number of bytes that can be written to next_out
    mz_ulong total_out;      // total number of bytes produced so far

    char *msg;                       // error msg (unused)
    struct mz_internal_state *state; // internal state, allocated by zalloc/zfree

    mz_alloc_func zalloc; // optional heap allocation function (defaults to malloc)
    mz_free_func zfree;   // optional heap free function (defaults to free)
    void *opaque;         // heap alloc function user pointer

    int data_type;     // data_type (unused)
    mz_ulong adler;    // adler32 of the source or uncompressed data
    mz_ulong reserved; // not used
} mz_stream;

// Window bits
#define MZ_DEFAULT_WINDOW_BITS 15

typedef mz_stream *mz_streamp;
int mz_inflateInit(mz_streamp pStream);
int mz_inflateInit2(mz_streamp pStream, int window_bits);
// Decompresses the input stream to the output, consuming only as much of the input as needed, and
// writing as much to the output as possible.
// Parameters:
//   pStream is the stream to read from and write to. You must initialize/update the next_in,
//   avail_in, next_out, and avail_out members.
//   flush may be MZ_NO_FLUSH, MZ_SYNC_FLUSH, or MZ_FINISH.
//   On the first call, if flush is MZ_FINISH it's assumed the input and output buffers are both
//   sized large enough to decompress the entire stream in a single call (this is slightly faster).
//   MZ_FINISH implies that there are no more source bytes available beside what's already in the
//   input buffer, and that the output buffer is large enough to hold the rest of the decompressed
//   data.
// Return values:
//   MZ_OK on success. Either more input is needed but not available, and/or there's more output to
//   be written but the output buffer is full.
//   MZ_STREAM_END if all needed input has been consumed and all output bytes have been written. For
//   zlib streams, the adler-32 of the decompressed data has also been verified.
//   MZ_STREAM_ERROR if the stream is bogus.
//   MZ_DATA_ERROR if the deflate stream is invalid.
//   MZ_PARAM_ERROR if one of the parameters is invalid.
//   MZ_BUF_ERROR if no forward progress is possible because the input buffer is empty but the
//   inflater needs more input to continue, or if the output buffer is not large enough. Call
//   mz_inflate() again
//   with more input data, or with more room in the output buffer (except when using single call
//   decompression, described above).
int mz_inflate(mz_streamp pStream, int flush);

// Deinitializes a decompressor.
int mz_inflateEnd(mz_streamp pStream);
enum
{
    MZ_NO_FLUSH = 0,
    MZ_PARTIAL_FLUSH = 1,
    MZ_SYNC_FLUSH = 2,
    MZ_FULL_FLUSH = 3,
    MZ_FINISH = 4,
    MZ_BLOCK = 5
};
// Return status codes. MZ_PARAM_ERROR is non-standard.
enum
{
    MZ_OK = 0,
    MZ_STREAM_END = 1,
    MZ_NEED_DICT = 2,
    MZ_ERRNO = -1,
    MZ_STREAM_ERROR = -2,
    MZ_DATA_ERROR = -3,
    MZ_MEM_ERROR = -4,
    MZ_BUF_ERROR = -5,
    MZ_VERSION_ERROR = -6,
    MZ_PARAM_ERROR = -10000
};


#endif // INFLATE_H
