#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

/* Things that affect compression rate/CPU

   HASH_ASSOC: bigger numbers => better compression

   DO_LITERALIZE: whether literalize can be > 1. (improves compression
   speed on incompressible material)

   MAX_LITERALIZE: How many bytes (at limit) will be literalized at
   once, without checking for matches.

   the rate at which literalize grows.

   HASH_EVERY_N_BYTES. The frequency with which we call
   c5_update_history. Low values (down to 1) give better compression,
   mostly. Power of two. Note that this

   DO_HASH_LITERALS: Should we hash the middle of incompressible
   chunks? This seems like a lose/lose.
  */

#define DO_LITERALIZE 1

#define HASH_EVERY_N_BYTES 32

#define MAX_LITERALIZE 32

#define DO_HASH_LITERALS 0

// must be power of 2, not to exceed 2^16 (due to hash function limitations)
// snappy uses HISTORY_SIZE_BITS 14

#define HISTORY_SIZE_BITS 14
#define HISTORY_SIZE (1<<HISTORY_SIZE_BITS)

#define HISTORY_ASSOC 1

typedef uint32_t bits_t;

//////////////////////////////////////////////////////////////

static inline void
memcpy64 (uint8_t *dest, const uint8_t *src)
{
    if (sizeof(void*) == 8) {
        memcpy(dest, src, 8);
    } else {
        memcpy(dest, src, 4);
        memcpy(dest+4, src+4, 4);
    }
}


#define ZLO_BITS 4
#define ZHI_BITS (8-ZLO_BITS)
#define ZLO_MASK ((1 << ZLO_BITS) - 1)
#define ZHI_MASK (0xff ^ ZLO_MASK)
#define ZHI_MAX ((1 << ZHI_BITS) - 1)

/* stats


Use 0b0 to represent 1, and 0b1 + 8bits to represent longer: 2.76 bits

use 0b00-0b10 to represent lengths 1-3, with 0b11 + 8 bits for longer: 2.56 bits

Use 0bxxxx for lengths 1-15, with 0b1111 + 8 bits for longer: 4.24 bits

Given similarity of first two options, we opt for the first once since it's faster.

log2(literallen): count

0:   10032100 (    78.050)
1:    1946600 (    15.145)
2:     543000 (     4.225)
3:      97800 (     0.761)
4:     112800 (     0.878)
5:      55700 (     0.433)
6:      29000 (     0.226)
7:      16700 (     0.130)
8:       8200 (     0.064)
9:       6300 (     0.049)
10:       3300 (     0.026)
11:       1100 (     0.009)
12:        900 (     0.007)

log2(copy_len): count (%)

0:          0 (     0.000)
1:   17110800 (    30.709)
2:   21054900 (    37.787)
3:   12826800 (    23.020) <---- This is Beautiful for our current 0b0000-0b1110 encoding.
4:    1915000 (     3.437)
5:    1297300 (     2.328)
6:     441200 (     0.792)
7:     881800 (     1.583)
8:      97500 (     0.175)
9:      56400 (     0.101)
10:      28300 (     0.051)
11:       6200 (     0.011)
12:        900 (     0.002)
13:        300 (     0.001)
14:       1300 (     0.002)
15:        500 (     0.001)
16:        600 (     0.001)
17:          0 (     0.000)
18:        100 (     0.000)
19:          0 (     0.000)
20:          0 (     0.000)
21:          0 (     0.000)
22:          0 (     0.000)
23:          0 (     0.000)
24:          0 (     0.000)
25:          0 (     0.000)
26:          0 (     0.000)
27:          0 (     0.000)
28:          0 (     0.000)
29:          0 (     0.000)
30:          0 (     0.000)
31:          0 (     0.000)

log2(copy_ago): count

0:    2149200 (     3.857)
1:     203700 (     0.366)
2:     725400 (     1.302)
3:    2146400 (     3.852)
4:    1911100 (     3.430)
5:    3152500 (     5.658)
6:    3804200 (     6.827)
7:    4981900 (     8.941)
8:    5438200 (     9.760)
9:    6036900 (    10.834)
10:    6078400 (    10.909)
11:   10448000 (    18.751) <-- we encode agos up to (4+7=11) bits with the tag plus one varint byte
12:    2644500 (     4.746) <-- these spill into a second varint byte
13:    2253600 (     4.044)
14:    1552800 (     2.787)
15:     981100 (     1.761)
16:     574000 (     1.030)
17:     359400 (     0.645)
18:     154800 (     0.278)
19:      83800 (     0.150)
20:      35300 (     0.063)
21:       5700 (     0.010)
22:          0 (     0.000)
23:          0 (     0.000)
24:          0 (     0.000)
25:          0 (     0.000)
26:          0 (     0.000)
27:          0 (     0.000)
28:          0 (     0.000)
29:          0 (     0.000)
30:          0 (     0.000)
31:          0 (     0.000)

 */

struct uc5_state
{
    const uint8_t *in;
    int inlen;
    int inpos;

    uint8_t *out;
    int outpos;

    bits_t bits;
    int bits_left;
};

static inline int
uc5_bit (struct uc5_state *state)
{
    if (state->bits_left == 0) {
        state->bits = 0; // unnecessary?

        // load in MSB first.
        for (int i = 0; i < sizeof(bits_t); i++) {
            state->bits <<= 8;
            state->bits |= state->in[state->inpos++];
        }
        state->bits_left = 8*sizeof(bits_t);
    }

    int bit = state->bits & 1;
    state->bits >>= 1;
    state->bits_left--;

    return bit;
}

static inline int
uc5_bits (struct uc5_state *state, int nbits)
{
    if (state->bits_left >= nbits) {
        int v = state->bits & (( 1 << nbits) - 1);
        state->bits >>= nbits;
        state->bits_left -= nbits;
        return v;
    }

    int v = 0;
    // LSB first
    for (int i = 0; i < nbits; i++)
        v |= (uc5_bit(state) << i);

    return v;
}

static inline uint32_t
uc5_varint (const uint8_t *in, int *inpos)
{
    uint32_t v = 0;
    int shift = 0;

    while (1) {
        uint8_t a = in[*inpos];
        (*inpos)++;
        v = v | ((a & 0x7f) << shift);

        if ((a & 0x80) == 0)
            break;

        shift += 7;
    }

    return v;
}

static inline void
uc5_literal (struct uc5_state *state)
{
    uint32_t len;

    len = uc5_bits(state, 2) + 1;
    if (len == 4)
        len = uc5_varint(state->in, &state->inpos) + 3;

    for (int i = 0; i < len; i += 8)
        memcpy64(&state->out[state->outpos + i], &state->in[state->inpos + i]);
    state->inpos += len;
    state->outpos += len;
}

// must handle len = 0 correctly
static inline void
uc5_copy (struct uc5_state *state)
{
    uint32_t len, ago;

    //////////////////////////
    // 7 6 5 4 3 2 1 0
    // --len-- --ago--
    //        <------- = COPY_BIT_SHIFT
    uint8_t z = state->in[state->inpos++];

    if ((z & ZHI_MASK) == ZHI_MASK)
        len = uc5_varint(state->in, &state->inpos) + 15;
    else
        len = (z >> ZLO_BITS) + 1;

    ago = (uc5_varint(state->in, &state->inpos) << ZLO_BITS) + (z & ZLO_MASK);

    uint32_t offset = state->outpos - ago;

    if (ago >= 8) {
        for (int i = 0; i < len; i += 8)
            memcpy64(&state->out[state->outpos + i], &state->out[offset + i]);
        state->outpos += len;
        return;
    }

    // key idea:
    //
    // We can interpret *every* copy as being a repeating sequence of
    // period 'ago'.  If we copy 8 bytes at a type, move over the
    // buffer beginning at ago eight units, wrapping around once we
    // get to outpos. In other words, 64 bit transfer i moves
    // transfers from offset + (8i) % ago.
    //
    // For small values of "ago", we must fill in the buffer
    // byte-by-byte so that when we do a 64 bit load, we load the
    // correct pattern. Once the pattern has been extended far enough
    // forward, we can switch to 64 bit copies.
    if (len >= 10) {

        state->out[state->outpos+0] = state->out[offset+0];
        state->out[state->outpos+1] = state->out[offset+1];

        memcpy  (&state->out[state->outpos+2], &state->out[offset + (2 % ago)], 2);

        memcpy  (&state->out[state->outpos+4], &state->out[offset + (4 % ago)], 4);

        int doffset = 8 % ago;

        for (int i = 0; i < len; i += 8) {
            memcpy64(&state->out[state->outpos + i], &state->out[offset]);
            offset += doffset;
        }

        state->outpos += len;
        return;
    }

    for (int i = 0; i < len; i++)
        state->out[state->outpos++] = state->out[offset++];
}

uint32_t
uc5_length (const uint8_t *_in, int _inlen)
{
    uint32_t v = 0;

    v = (_in[0] << 24);
    v |= (_in[1] << 16);
    v |= (_in[2] << 8);
    v |= (_in[3] << 0);

    return v;
}

void
uc5 (const uint8_t *_in, int _inlen, uint8_t *_out, int *_outlen)
{
    struct uc5_state _state;
    struct uc5_state *state = &_state;

    if (uc5_length(_in, _inlen) == 0) {
        *_outlen = 0;
        return;
    }

    state->in = _in;
    state->inlen = _inlen;
    state->inpos = 4;
    state->out = _out;
    state->outpos = 0;
    state->bits = 0;
    state->bits_left = 0;

    state->out[state->outpos++] = state->in[state->inpos++];

    // NB: unlike c6, we don't need to encode EOF with a copy of
    // length zero, because all of our opcodes require additional
    // bytes and thus we don't have to worry about terminating while
    // there are still commands in the bit stream.
    while (state->inpos < state->inlen) {

        int bit = uc5_bit(state);

        if (bit) {
            uc5_literal(state);
            uc5_copy(state);
        } else {
            uc5_copy(state);
        }
    }

    *_outlen = state->outpos;
}

////////////////////////////////////////////////////////

struct c5_state
{
    const uint8_t *in;
    int inlen;
    uint8_t *out;

    uint32_t history[HISTORY_SIZE][HISTORY_ASSOC];
    uint32_t history_index;

    int inpos;
    int outpos;

    uint32_t outbit_pos; // where in the output stream is it?
    int outbits_left;
    bits_t outbits;

    uint32_t copy_len, copy_pos;

    uint32_t literal_pos, literal_len;

};

static inline uint32_t
c5_hash (struct c5_state *state, int inpos)
{
    // our goal is to produce ~16 bytes of "entropy" from three bytes so
    // as to support HISTORY_SIZEs up to 2^16.

    // This is snappy's hash function, which works well.
    const uint32_t k = 0x1e35a7bd;

    uint32_t *p = (uint32_t*) &state->in[inpos];
    uint32_t a = (*p);
//    uint32_t a = (state->in[inpos]<<16) | (state->in[inpos+1]<<8) | (state->in[inpos+2]);

    return (a*k) >> (32 - HISTORY_SIZE_BITS);
}

// NB: we will call this once erroneously at the beginning of the file
// before any bits are output. That's okay, since we'll overwrite them later.
static inline void
c5_bit_flush (struct c5_state *state)
{
    for (int i = 0; i < sizeof(bits_t); i++) {
        state->out[state->outbit_pos+i] = state->outbits >> (8*sizeof(bits_t) - 8);
        state->outbits <<= 8;
    }
}

static inline void
c5_bit (struct c5_state *state, uint64_t bit)
{
    assert(bit == 0 || bit == 1);

    if (state->outbits_left == 0) {
        c5_bit_flush(state);

        state->outbit_pos = state->outpos;
        state->outbits = 0;
        state->outpos += sizeof(bits_t);

        state->outbits_left = 8*sizeof(bits_t);
    }

    state->outbits |= (bit << (8*sizeof(bits_t)-state->outbits_left)); // XXXX ?? -1 missing?
    state->outbits_left--;
}

static inline void
c5_bits (struct c5_state *state, uint64_t bits, int nbits)
{
    if (state->outbits_left < nbits) {
        // output bits in LSB order
        for (int i = 0; i < nbits; i++) {
            c5_bit(state, bits & 1);
            bits >>= 1;
        }
        return;
    }

    state->outbits |= (bits << (8*sizeof(bits_t) - state->outbits_left));
    state->outbits_left -= nbits;
}

static inline void
c5_varint (uint8_t *out, int *outpos, uint32_t v)
{
  more:
    out[*outpos] = (v & 0x7f) | (v >= 0x80 ? 0x80: 0);
    (*outpos)++;
    v >>= 7;
    if (v > 0 )
        goto more;
}

static inline void
c5_update_history (struct c5_state *state, int inpos)
{
    uint32_t key = c5_hash(state, inpos);
    int idx = (state->history_index++) & (HISTORY_ASSOC - 1);

    state->history[key][idx] = inpos;
}

static inline void
c5_literal (struct c5_state *state)
{
    int c = state->literal_len - 1;
    if (c > 3)
        c = 3;

    c5_bits(state, c, 2);

    if (state->literal_len >= 4)
        c5_varint(state->out, &state->outpos, state->literal_len - 3);

    for (int i = 0; i < state->literal_len; i += 8)
        memcpy64(&state->out[state->outpos + i], &state->in[state->literal_pos + i]);
    state->outpos += state->literal_len;

    state->literal_len = 0;
}

// needs to be able to handle copy_len 0, which can occur at end of file.
static inline void
c5_copy (struct c5_state *state)
{
    uint32_t ago = state->inpos - state->copy_pos;

    // put the length in the upper 4 bits (using the value 15 to
    // denote an overflow), and put the four lowest bits of 'ago'
    // into the low 4 bits.
    uint8_t z = (ago & ZLO_MASK);

    // if copy_len == 0, this will wrap under to a big number.
    if ((state->copy_len-1) < ZHI_MAX) {
        z |= ((state->copy_len-1) << ZLO_BITS);
        state->out[state->outpos++] = z;
    } else {
        z |= ZHI_MASK;
        state->out[state->outpos++] = z;
        c5_varint(state->out, &state->outpos, state->copy_len - 15);
    }

    c5_varint(state->out, &state->outpos, ago >> ZLO_BITS);
}

void
c5 (const uint8_t *_in, int _inlen, uint8_t *_out, int *_outlen)
{
    struct c5_state _state;
    struct c5_state *state = &_state;

    state->in = _in;
    state->inlen = _inlen;
    state->out = _out;
    state->inpos = 0;
    state->outpos = 0;
    state->outbits = 0;
    state->outbit_pos = 5; // bits will appear at offset 1
    state->outbits_left = 0;
    state->literal_len = 0;
    state->copy_pos = 0;

    memset(state->history, 0, sizeof(state->history));

    state->out[state->outpos++] = (_inlen >> 24) & 0xff;
    state->out[state->outpos++] = (_inlen >> 16) & 0xff;
    state->out[state->outpos++] = (_inlen >> 8) & 0xff;
    state->out[state->outpos++] = (_inlen >> 0) & 0xff;

    c5_update_history(state, state->inpos);
    state->out[state->outpos++] = state->in[state->inpos++];

    while (state->inpos < state->inlen) {
        state->copy_len = 0;

        // find a copy
        if (1) {
            // this function will read ahead two bytes, which can go
            // past inlen.  but require padding (so the read won't
            // segfault), and it doesn't matter for the correctness of
            // the output stream whether we search from a random
            // location.
            uint32_t key = c5_hash(state, state->inpos);

            for (int i = 0; i < HISTORY_ASSOC; i++) {

                uint32_t this_copy_pos = state->history[key][i];

                // our hash table could have a forward reference due to our
                // "early" hash table population (see below).
                if (this_copy_pos >= state->inpos)
                    continue;

                uint32_t this_copy_len = 0;

                uint32_t max_copy_len = state->inlen - state->inpos;

                while (this_copy_len + 4 < max_copy_len &&
                       *((uint32_t*) &state->in[this_copy_pos + this_copy_len]) == *((uint32_t*) &state->in[state->inpos + this_copy_len])) {

                    // This may seem a bit weird; we're indexing our
                    // cache while we're still searching.  The reason
                    // is simple: we're already striding over the
                    // bytes here, whereas updating the cache after
                    // finding the match would involve a second pass
                    // over the input. Also note that it's always
                    // legal for us to "pollute" the cache with
                    // forward references; we'll filter those out.
                    if ((this_copy_len & (HASH_EVERY_N_BYTES - 1)) == 0)
                        c5_update_history(state, state->inpos + this_copy_len);

                    this_copy_len+=4;
                }

                // see if we can grow the copy just a little bit more
                while (this_copy_len < max_copy_len &&
                       state->in[this_copy_pos + this_copy_len] == state->in[state->inpos + this_copy_len]) {

                    if ((this_copy_len & (HASH_EVERY_N_BYTES - 1)) == 0)
                        c5_update_history(state, state->inpos + this_copy_len);

                    this_copy_len++;
                }

                if (this_copy_len > state->copy_len) {
                    state->copy_len = this_copy_len;
                    state->copy_pos = this_copy_pos;
                }
            }
        }

//        uint32_t ago = state->inpos - state->copy_pos;

        // quickly check the basic threshold, then lazily check the more accurate thresholds
//        if (state->copy_len >= 3 && state->copy_len >= (3 + (ago >= (1<<11)) + (ago >= (1 << 18)))) {
        if (state->copy_len >= 4) {
            if (state->literal_len > 0) {
                c5_bit(state, 1);
                c5_literal(state);
                c5_copy(state);
            } else {
                c5_bit(state, 0);
                c5_copy(state);
            }

            state->inpos += state->copy_len;

        } else {

            // how many bytes to serialize in "one go"?
            uint32_t literalize = 1;

            if (DO_LITERALIZE) {
                literalize = 1 + (state->literal_len >> 3);
                if (literalize > MAX_LITERALIZE)
                    literalize = MAX_LITERALIZE;

                if (state->inpos + literalize >= state->inlen)
                    literalize = state->inlen - state->inpos;
            }

            if (state->literal_len == 0)
                state->literal_pos = state->inpos;

            // update history at just the current offset (handles the
            // literalize==1 case).  For other offsets, don't bother
            // updating: we're in "skip over incompressible material"
            // mode, and because it's incompressible, there's no point
            // hashing it.
            c5_update_history(state, state->inpos);

            if (DO_HASH_LITERALS) {
                for (uint32_t i = HASH_EVERY_N_BYTES; i < literalize; i += HASH_EVERY_N_BYTES) {
                    c5_update_history(state, state->literal_pos + state->literal_len + i);
                }
            }

            state->inpos += literalize;
            state->literal_len += literalize;
        }
    }

    if (state->literal_len > 0) {
        c5_bit(state, 1);
        c5_literal(state);
        state->copy_len = 0;
        c5_copy(state);
    }
    *_outlen = state->outpos;

    c5_bit_flush(state);
}

#ifdef _C5_MAIN

#include "c5.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int
main (int argc, char *argv[])
{
    int selftest = 0;

    for (int i = 1; i < argc; i++) {

        if (!strcmp("-t", argv[i])) {
            selftest = 1;
            continue;
        }

        uint8_t *inbuf;
        int inlen;

        if (1) {
            struct stat s;
            memset(&s, 0, sizeof(s));
            if (stat(argv[i], &s)) {
                perror(argv[i]);
                continue;
            }

            if (!(S_ISREG(s.st_mode))) {
                printf("Skipping %s\n", argv[i]);
                continue;
            }

            FILE *f = fopen(argv[i], "rb");
            if (f == NULL) {
                perror(argv[i]);
                continue;
            }

            fseek(f, 0, SEEK_END);
            inlen = (int) ftell(f);
            fseek(f, 0, SEEK_SET);

            inbuf = calloc(1, inlen + C5_PAD);
            if (inlen != fread(inbuf, 1, inlen, f)) {
                printf("read failed\n");
                return -1;
            }

            fclose(f);
        }

        if (selftest) {
            int outalloc = inlen * 1.1 + 16;
            uint8_t *outbuf = calloc(1, outalloc + C5_PAD);
            int outlen;
            c5(inbuf, inlen, outbuf, &outlen);
            assert(outlen <= outalloc);

            int outlen2 = uc5_length(outbuf, outlen);
            uint8_t *outbuf2 = calloc(1, outlen2 + C5_PAD);
            uc5(outbuf, outlen, outbuf2, &outlen2);

            int error = (outlen2 != inlen) || memcmp(inbuf, outbuf2, inlen);
            printf("Testing %s [%d ==> %d] %s\n", argv[i], inlen, outlen, error ? "FAIL" : "OKAY");
            if (error)
                exit(-1);

            free(outbuf);
            free(outbuf2);
            goto cleanup;
        }

        int slen = strlen(argv[i]);
        int uncompress = (slen > 3 && !strcmp(&argv[i][slen-3], ".c5"));

        if (uncompress) {
            int outlen = uc5_length(inbuf, inlen);

            uint8_t *outbuf = calloc(1, outlen + C5_PAD);

            uc5(inbuf, inlen, outbuf, &outlen);

            char outpath[strlen(argv[i])+5];
            sprintf(outpath, "%s", argv[i]);
            outpath[slen-3] = 0;

            FILE *f = fopen(outpath, "wb");
            if (f == NULL) {
                perror(outpath);
                goto cleanup;
            }

            if (outlen != fwrite(outbuf, 1, outlen, f)) {
                printf("write failed\n");
                return -1;
            }

            fclose(f);

            printf("Uncompressed %s [%d] => %s [%d]\n", argv[i], inlen, outpath, outlen);
            free(outbuf);

        } else {

            // COMPRESS
            int outalloc = inlen * 1.1 + 16;
            uint8_t *outbuf = calloc(1, outalloc + C5_PAD);
            int outlen;
            c5(inbuf, inlen, outbuf, &outlen);
            assert(outlen <= outalloc);

            char outpath[strlen(argv[i])+5];
            sprintf(outpath, "%s.c5", argv[i]);

            FILE *f = fopen(outpath, "wb");
            if (f == NULL) {
                perror(outpath);
                goto cleanup;
            }

            if (outlen != fwrite(outbuf, 1, outlen, f)) {
                printf("write failed\n");
                return -1;
            }

            fclose(f);

            printf("Compressed %s [%d] => %s [%d]\n", argv[i], inlen, outpath, outlen);
            free(outbuf);
        }

      cleanup:
        free(inbuf);
    }

}

#endif
