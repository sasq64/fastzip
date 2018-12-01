/* Put zip.h first as when using 64-bit file environment in unix ctype.h
   defines off_t and then while other files are using an 8-byte off_t this
   file gets a 4-byte off_t.  Once zip.h sets the large file defines can
   then include ctype.h and get 8-byte off_t.  8/14/04 EG */
#include "zip.h"
#include <ctype.h>

/* ===========================================================================
 * Constants
 */

#define MAX_BITS 15
/* All codes must not exceed MAX_BITS bits */

#define MAX_BL_BITS 7
/* Bit length codes must not exceed MAX_BL_BITS bits */

#define LENGTH_CODES 29
/* number of length codes, not counting the special END_BLOCK code */

#define LITERALS 256
/* number of literal bytes 0..255 */

#define END_BLOCK 256
/* end of block literal code */

#define L_CODES (LITERALS + 1 + LENGTH_CODES)
/* number of Literal or Length codes, including the END_BLOCK code */

#define D_CODES 30
/* number of distance codes */

#define BL_CODES 19
/* number of codes used to transfer the bit lengths */

#ifdef SMALL_MEM
#    define HASH_BITS 13 /* Number of bits used to hash strings */
#endif
#ifdef MEDIUM_MEM
#    define HASH_BITS 14
#endif
#ifndef HASH_BITS
#    define HASH_BITS 15
/* For portability to 16 bit machines, do not use values above 15. */
#endif

#define HASH_SIZE (unsigned)(1 << HASH_BITS)
#define HASH_MASK (HASH_SIZE - 1)
#define WMASK (WSIZE - 1)
/* HASH_SIZE and WSIZE must be powers of two */

#define NIL 0
/* Tail of hash chains */

#define FAST 4
#define SLOW 2
/* speed options for the general purpose bit flag */

#ifndef TOO_FAR
#    define TOO_FAR 4096
#endif
/* Matches of length 3 are discarded if their distance exceeds TOO_FAR */

#if (defined(ASMV) && defined(DYN_ALLOC))
   error: DYN_ALLOC not yet supported in match.S or match32.asm
#endif

#ifdef MEMORY16
#    define MAXSEG_64K
#endif

#undef local
#define local

struct IZDeflate
{

    int near extra_lbits[LENGTH_CODES] /* extra bits for each length code */
        = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

    int near extra_dbits[D_CODES] /* extra bits for each distance code */
        = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    int near extra_blbits[BL_CODES] /* extra bits for each bit length code */
        = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 7};

#define STORED_BLOCK 0
#define STATIC_TREES 1
#define DYN_TREES 2
    /* The three kinds of block type */

#ifndef LIT_BUFSIZE
#    ifdef SMALL_MEM
#        define LIT_BUFSIZE 0x2000
#    else
#        ifdef MEDIUM_MEM
#            define LIT_BUFSIZE 0x4000
#        else
#            define LIT_BUFSIZE 0x8000
#        endif
#    endif
#endif
#define DIST_BUFSIZE LIT_BUFSIZE
    /* Sizes of match buffers for literals/lengths and distances.  There are
     * 4 reasons for limiting LIT_BUFSIZE to 64K:
     *   - frequencies can be kept in 16 bit counters
     *   - if compression is not successful for the first block, all input data
     * is still in the window so we can still emit a stored block even when
     * input comes from standard input.  (This can also be done for all blocks
     * if LIT_BUFSIZE is not greater than 32K.)
     *   - if compression is not successful for a file smaller than 64K, we can
     *     even emit a stored file instead of a stored block (saving 5 bytes).
     *   - creating new Huffman trees less frequently may not provide fast
     *     adaptation to changes in the input data statistics. (Take for
     *     example a binary file with poorly compressible code followed by
     *     a highly compressible string table.) Smaller buffer sizes give
     *     fast adaptation but have of course the overhead of transmitting trees
     *     more frequently.
     *   - I can't count above 4
     * The current code is general and allows DIST_BUFSIZE < LIT_BUFSIZE (to
     * save memory at the expense of compression). Some optimizations would be
     * possible if we rely on DIST_BUFSIZE == LIT_BUFSIZE.
     */

#define REP_3_6 16
    /* repeat previous bit length 3-6 times (2 bits of repeat count) */

#define REPZ_3_10 17
    /* repeat a zero length 3-10 times  (3 bits of repeat count) */

#define REPZ_11_138 18
    /* repeat a zero length 11-138 times  (7 bits of repeat count) */

    /* ===========================================================================
     * data
     */

    /* Data structure describing a single value and its code string. */
    typedef struct ct_data
    {
        union
        {
            ush freq; /* frequency count */
            ush code; /* bit string */
        } fc;
        union
        {
            ush dad; /* father node in Huffman tree */
            ush len; /* length of bit string */
        } dl;
    } ct_data;

#define Freq fc.freq
#define Code fc.code
#define Dad dl.dad
#define Len dl.len

#define HEAP_SIZE (2 * L_CODES + 1)
    /* maximum heap size */

    ct_data near dyn_ltree[HEAP_SIZE];       /* literal and length tree */
    ct_data near dyn_dtree[2 * D_CODES + 1]; /* distance tree */

    ct_data near static_ltree[L_CODES + 2];
    /* The static literal tree. Since the bit lengths are imposed, there is no
     * need for the L_CODES extra codes used during heap construction. However
     * The codes 286 and 287 are needed to build a canonical tree (see ct_init
     * below).
     */

    ct_data near static_dtree[D_CODES];
    /* The static distance tree. (Actually a trivial tree since all codes use
     * 5 bits.)
     */

    ct_data near bl_tree[2 * BL_CODES + 1];
    /* Huffman tree for the bit lengths */

    typedef struct tree_desc
    {
        ct_data near* dyn_tree;    /* the dynamic tree */
        ct_data near* static_tree; /* corresponding static tree or NULL */
        int near* extra_bits;      /* extra bits for each code or NULL */
        int extra_base;            /* base index for extra_bits */
        int elems;                 /* max number of elements in the tree */
        int max_length;            /* max bit length for the codes */
        int max_code;              /* largest code with non zero frequency */
    } tree_desc;

    tree_desc near l_desc = {NULL,    NULL,     NULL, LITERALS + 1,
                             L_CODES, MAX_BITS, 0};
    tree_desc near d_desc = {NULL, NULL, NULL, 0, D_CODES, MAX_BITS, 0};
    tree_desc near bl_desc = {NULL, NULL, NULL, 0, BL_CODES, MAX_BL_BITS, 0};

    ush near bl_count[MAX_BITS + 1];
    /* number of codes at each bit length for an optimal tree */

    uch near bl_order[BL_CODES] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                   11, 4,  12, 3, 13, 2, 14, 1, 15};
    /* The lengths of the bit length codes are sent in order of decreasing
     * probability, to avoid transmitting the lengths for unused bit length
     * codes.
     */

    int near heap[2 * L_CODES + 1]; /* heap used to build the Huffman trees */
    int heap_len;                   /* number of elements in the heap */
    int heap_max;                   /* element of largest frequency */
    /* The sons of heap[n] are heap[2*n] and heap[2*n+1]. heap[0] is not used.
     * The same heap array is used to build all trees.
     */

    uch near depth[2 * L_CODES + 1];
    /* Depth of each subtree used as tie breaker for trees of equal frequency */

    uch length_code[MAX_MATCH - MIN_MATCH + 1];
    /* length code for each normalized match length (0 == MIN_MATCH) */

    uch dist_code[512];
    /* distance codes. The first 256 values correspond to the distances
     * 3 .. 258, the last 256 values correspond to the top 8 bits of
     * the 15 bit distances.
     */

    int near base_length[LENGTH_CODES];
    /* First normalized length for each code (0 = MIN_MATCH) */

    int near base_dist[D_CODES];
    /* First normalized distance for each code (0 = distance of 1) */

#ifndef DYN_ALLOC
    uch far l_buf[LIT_BUFSIZE];  /* buffer for literals/lengths */
    ush far d_buf[DIST_BUFSIZE]; /* buffer for distances */
#else
    uch far* l_buf;
    ush far* d_buf;
#endif

    uch near flag_buf[(LIT_BUFSIZE / 8)];
    /* flag_buf is a bit array distinguishing literals from lengths in
     * l_buf, and thus indicating the presence or absence of a distance.
     */

    unsigned last_lit;   /* running index in l_buf */
    unsigned last_dist;  /* running index in d_buf */
    unsigned last_flags; /* running index in flag_buf */
    uch flags;           /* current flags not yet saved in flag_buf */
    uch flag_bit;        /* current bit used in flags */
    /* bits are filled in flags starting at bit 0 (least significant).
     * Note: these flags are overkill in the current code since we don't
     * take advantage of DIST_BUFSIZE == LIT_BUFSIZE.
     */

    ulg opt_len;    /* bit length of current block with optimal trees */
    ulg static_len; /* bit length of current block with static trees */

    /* zip64 support 08/29/2003 R.Nausedat */
    /* now all file sizes and offsets are zoff_t 7/24/04 EG */
    uzoff_t cmpr_bytelen; /* total byte length of compressed file */
    ulg cmpr_len_bits;    /* number of bits past 'cmpr_bytelen' */

#ifdef DEBUG
    uzoff_t input_len; /* total byte length of input file */
/* input_len is for debugging only since we can get it by other means. */
#endif

    ush* file_type;   /* pointer to UNKNOWN, BINARY or ASCII */
    int* file_method; /* pointer to DEFLATE or STORE */

    /* ===========================================================================
     * data used by the "bit string" routines.
     */

    int flush_flg;

    unsigned bi_buf;
    /* Output buffer. bits are inserted starting at the bottom (least significant
     * bits). The width of bi_buf must be at least 16 bits.
     */

#define Buf_size (8 * 2 * sizeof(char))
    /* Number of bits used within bi_buf. (bi_buf may be implemented on
     * more than 16 bits on some systems.)
     */

    int bi_valid;

    /* Used bits in last byte of deflate block */
    int last_bits;

    /* Number of valid bits in bi_buf.  All bits above the last valid bit
     * are always zero.
     */

    char *out_buf;
    /* Current output buffer. */

    unsigned out_offset;
    /* Current offset in output buffer.
     * On 16 bit machines, the buffer is limited to 64K.
     */

    unsigned out_size;
    /* Size of current output buffer */

    //};

    ////////// DEFLATE

#if defined(MMAP) || defined(BIG_MEM)
    typedef unsigned Pos; /* must be at least 32 bits */
#else
    typedef ush Pos;
#endif
    typedef unsigned IPos;
    /* A Pos is an index in the character window. We use short instead of int to
     * save space in the various tables. IPos is used only for parameter
     * passing.
     */

#ifndef DYN_ALLOC
    uch window[2L * WSIZE];
    /* Sliding window. Input bytes are read into the second half of the window,
     * and move to the first half later to keep a dictionary of at least WSIZE
     * bytes. With this organization, matches are limited to a distance of
     * WSIZE-MAX_MATCH bytes, but this ensures that IO is always
     * performed with a length multiple of the block size. Also, it limits
     * the window size to 64K, which is quite useful on MSDOS.
     * To do: limit the window size to WSIZE+CBSZ if SMALL_MEM (the code would
     * be less efficient since the data would have to be copied WSIZE/CBSZ
     * times)
     */
    Pos prev[WSIZE];
    /* Link to older string with same hash index. To limit the size of this
     * array to 64K, this link is maintained only for the last 32K strings.
     * An index in this array is thus a window index modulo 32K.
     */
    Pos head[HASH_SIZE];
    /* Heads of the hash chains or NIL. If your compiler thinks that
     * HASH_SIZE is a dynamic value, recompile with -DDYN_ALLOC.
     */
#else
    uch far* near window = NULL;
    Pos far* near prev = NULL;
    Pos far* near head;
#endif
    ulg window_size;
    /* window size, 2*WSIZE except for MMAP or BIG_MEM, where it is the
     * input file length plus MIN_LOOKAHEAD.
     */

    long block_start;
    /* window position at the beginning of the current output block. Gets
     * negative when the window is moved backwards.
     */

    int sliding;
    /* Set to false when the input file is already in memory */

    unsigned ins_h; /* hash index of string to be inserted */

#define H_SHIFT ((HASH_BITS + MIN_MATCH - 1) / MIN_MATCH)
    /* Number of bits by which ins_h and del_h must be shifted at each
     * input step. It must be such that after MIN_MATCH steps, the oldest
     * byte no longer takes part in the hash key, that is:
     *   H_SHIFT * MIN_MATCH >= HASH_BITS
     */

    unsigned int near prev_length;
    /* Length of the best match at previous step. Matches not greater than this
     * are discarded. This is used in the lazy match evaluation.
     */

    unsigned near strstart;    /* start of string to insert */
    unsigned near match_start; /* start of matching string */
    int eofile;                /* flag set at end of input file */
    unsigned lookahead;        /* number of valid bytes ahead in window */

    unsigned near max_chain_length;
    /* To speed up deflation, hash chains are never searched beyond this length.
     * A higher limit improves compression ratio but degrades the speed.
     */

    unsigned int max_lazy_match;
/* Attempt to find a better match only when the current match is strictly
 * smaller than this value. This mechanism is used only for compression
 * levels >= 4.
 */
#define max_insert_length max_lazy_match
    /* Insert new strings in the hash table only if the match length
     * is not greater than this length. This saves time but degrades
     * compression. max_insert_length is used only for compression levels <= 3.
     */

    unsigned near good_match;
    /* Use a faster search when the previous match is longer than this */

#ifdef FULL_SEARCH
#    define nice_match MAX_MATCH
#else
    int near nice_match; /* Stop searching when current match exceeds this */
#endif

    /* Values for max_lazy_match, good_match, nice_match and max_chain_length,
     * depending on the desired pack level (0..9). The values given below have
     * been tuned to exclude worst case performance for pathological files.
     * Better values may be found for specific files.
     */

    typedef struct config
    {
        ush good_length; /* reduce lazy search above this match length */
        ush max_lazy; /* do not perform lazy search above this match length */
        ush nice_length; /* quit search above this match length */
        ush max_chain;
    } config;

    config configuration_table[10] = {
        /*      good lazy nice chain */
        /* 0 */ {0, 0, 0, 0}, /* store only */
        /* 1 */ {4, 4, 8, 4}, /* maximum speed, no lazy matches */
        /* 2 */ {4, 5, 16, 8},
        /* 3 */ {4, 6, 32, 32},

        /* 4 */ {4, 4, 16, 16}, /* lazy matches */
        /* 5 */ {8, 16, 32, 32},
        /* 6 */ {8, 16, 128, 128},
        /* 7 */ {8, 32, 128, 256},
        /* 8 */ {32, 128, 258, 1024},
        /* 9 */ {32, 258, 258, 4096}}; /* maximum compression */

    /* Note: the deflate() code requires max_lazy >= MIN_MATCH and max_chain >=
     * 4 For deflate_fast() (levels <= 3) good is ignored and lazy has a
     * different meaning.
     */

#define EQUAL 0
    /* result of memcmp for equal strings */

    /* ===========================================================================
     *  Prototypes for functions.
     */

    local void fill_window(void);

    local uzoff_t deflate_fast(void); /* now use uzoff_t 7/24/04 EG */

    int longest_match(IPos cur_match);
#if defined(ASMV) && !defined(RISCOS)
    void match_init(void); /* asm code initialization */
#endif

//#ifdef DEBUG
// local  void check_match(IPos start, IPos match, int length));
//#endif

/* ===========================================================================
 * Update a hash value with the given input byte
 * IN  assertion: all calls to to UPDATE_HASH are made with consecutive
 *    input characters, so that a running hash key can be computed from the
 *    previous key instead of complete recalculation each time.
 */
#define UPDATE_HASH(h, c) (h = (((h) << H_SHIFT) ^ (c)) & HASH_MASK)

/* ===========================================================================
 * Insert string s in the dictionary and set match_head to the previous head
 * of the hash chain (the most recent string with same hash key). Return
 * the previous length of the hash chain.
 * IN  assertion: all calls to to INSERT_STRING are made with consecutive
 *    input characters and the first MIN_MATCH bytes of s are valid
 *    (except for the last MIN_MATCH-1 bytes of the input file).
 */
#define INSERT_STRING(s, match_head)                                           \
    (UPDATE_HASH(ins_h, window[(s) + (MIN_MATCH - 1)]),                        \
     prev[(s)&WMASK] = match_head = head[ins_h], head[ins_h] = (s))

    //////

    void init_desc();

    void ct_init(ush* attr, int* method);
    uzoff_t flush_block(char* buf, ulg stored_len, int eof);
    void bi_init(char* tgt_buf, unsigned tgt_size, int flsh_allowed);
    int ct_tally(int dist, int lc);

    void init_block(void);
    void pqdownheap(ct_data near* tree, int k);
    void gen_bitlen(tree_desc near* desc);
    void gen_codes(ct_data near* tree, int max_code);
    void build_tree(tree_desc near* desc);
    void scan_tree(ct_data near* tree, int max_code);
    void send_tree(ct_data near* tree, int max_code);
    int build_bl_tree(void);
    void send_all_trees(int lcodes, int dcodes, int blcodes);
    void compress_block(ct_data near* ltree, ct_data near* dtree);
    void set_file_type(void);
#if (!defined(ASMV) || !defined(RISCOS))
    void send_bits(int value, int length);
    unsigned bi_reverse(unsigned code, int len);
#endif
    void bi_windup(void);
    void copy_block(char* buf, unsigned len, int header);

    void lm_init(int, ush*);
    void lm_free(void);

    uzoff_t deflate(void);
    // int longest_match(IPos cur_match);

    // Set to function read more data
    unsigned (*read_buf)(void* handle, char* buf, unsigned size);

    zoff_t dot_size = 0;  /* if not 0 then display dots every size buffers */
    zoff_t dot_count = 0; /* if dot_size not 0 counts buffers */
    /* status 10/30/04 */
    int display_globaldots =
        0;           /* display dots for archive instead of for each file */
    int verbose = 1; /* Report oddities in zip file structure */
    int noisy = 1;   /* False for quiet operation */
    int mesg_line_started = 0; /* 1=started writing a line to mesg */
    char* key = NULL;          /* Scramble password or NULL */
    FILE* mesg = NULL;         /* Where informational output goes */
    int level = 1;             /* Compression level */
    int use_descriptors = 0;   /* use data descriptors (extended headings) */

    void* read_handle;
};
