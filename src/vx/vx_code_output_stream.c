#include "vx_code_output_stream.h"
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

// checks whether there is an additional 'remaining' bytes free past 'pos'
static void _ensure_space(vx_code_output_stream_t * codes, int remaining)
{
    int newlen = codes->len;

    while (remaining + codes->pos > newlen)
        newlen *= 2;

    if (newlen != codes->len) {
        codes->data = realloc(codes->data, newlen);
        codes->len = newlen;
    }
}

static void _write_uint8(vx_code_output_stream_t * codes, uint8_t val)
{
    _ensure_space(codes,sizeof(uint8_t));

    codes->data[codes->pos++] = val;
}

static void _write_uint32(vx_code_output_stream_t * codes, uint32_t v)
{
    _ensure_space(codes,sizeof(uint32_t));

    codes->data[codes->pos++] = (v>>24)&0xff;
    codes->data[codes->pos++] = (v>>16)&0xff;
    codes->data[codes->pos++] = (v>>8)&0xff;
    codes->data[codes->pos++] = (v & 0xff);
}

static void _write_uint64(vx_code_output_stream_t * codes, uint64_t v)
{
    _ensure_space(codes,sizeof(uint64_t));

    codes->data[codes->pos++] = (v>>56)&0xff;
    codes->data[codes->pos++] = (v>>48)&0xff;
    codes->data[codes->pos++] = (v>>40)&0xff;
    codes->data[codes->pos++] = (v>>32)&0xff;
    codes->data[codes->pos++] = (v>>24)&0xff;
    codes->data[codes->pos++] = (v>>16)&0xff;
    codes->data[codes->pos++] = (v>>8)&0xff;
    codes->data[codes->pos++] = (v & 0xff);
}

union floatint
{
    uint32_t int_val;
    float float_val;
};

union doublelong
{
    uint64_t long_val;
    double double_val;
};

static void _write_float(vx_code_output_stream_t * codes, float val)
{
    union floatint fi;
    fi.float_val = val;
    codes->write_uint32(codes, fi.int_val);
}

static void _write_double(vx_code_output_stream_t * codes, double val)
{
    union doublelong dl;
    dl.double_val = val;
    codes->write_uint64(codes, dl.long_val);
}

static void _write_str (vx_code_output_stream_t * codes, const char *  str)
{
    int slen = strlen(str);
    _ensure_space(codes,slen+1);
    memcpy(codes->data+codes->pos, str, slen+1);
    codes->pos+=slen+1;
}

static void _write_bytes (vx_code_output_stream_t * codes, const uint8_t *  data, int datalen)
{
    _ensure_space(codes,datalen);
    memcpy(codes->data+codes->pos, data, datalen);
    codes->pos+=datalen;
}

vx_code_output_stream_t * vx_code_output_stream_create(int startlen)
{
    // all fields 0/NULL
    vx_code_output_stream_t * codes = calloc(sizeof(vx_code_output_stream_t), 1);
    codes->write_uint8 = _write_uint8;
    codes->write_uint32 = _write_uint32;
    codes->write_uint64 = _write_uint64;
    codes->write_float = _write_float;
    codes->write_double = _write_double;
    codes->write_str = _write_str;
    codes->write_bytes = _write_bytes;

    assert(startlen != 0);
    // set initial allocation
    codes->len = startlen;
    codes->data = malloc(startlen);

    return codes;
}

void vx_code_output_stream_destroy(vx_code_output_stream_t * codes)
{
    free(codes->data);
    free(codes);
}
