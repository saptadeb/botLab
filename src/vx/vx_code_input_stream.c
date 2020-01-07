#include "vx_code_input_stream.h"
#include <malloc.h>
#include <assert.h>

// XXX Make all these functions static
#define ENSURE_SPACE(stream, size) assert(stream->len >= stream->pos + size)

uint8_t  vx_read_uint8(vx_code_input_stream_t * stream)
{
    ENSURE_SPACE(stream, 1);

    uint8_t out = stream->data[stream->pos];
    stream->pos += 1;
    return out;
}

uint32_t vx_read_uint32(vx_code_input_stream_t * stream)
{
    ENSURE_SPACE(stream, 4);


    uint32_t out = ((stream->data[stream->pos+0] << 24) +
                    (stream->data[stream->pos+1] << 16) +
                    (stream->data[stream->pos+2] << 8) +
                    (stream->data[stream->pos+3]     ));
    stream->pos += 4;

    return out;
}


uint64_t vx_read_uint64(vx_code_input_stream_t * stream)
{
    ENSURE_SPACE(stream, 8);

    uint64_t a = vx_read_uint32(stream);
    uint64_t b = vx_read_uint32(stream);

    uint64_t result = (a<<32) + (b&0xffffffff);
    return result;
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

float vx_read_float(vx_code_input_stream_t * stream)
{
    ENSURE_SPACE(stream, 4);

    union floatint fi;

    fi.int_val = stream->read_uint32(stream);
    return fi.float_val;
}

double vx_read_double(vx_code_input_stream_t * stream)
{
    ENSURE_SPACE(stream, 4);

    union doublelong dl;

    dl.long_val = stream->read_uint64(stream);
    return dl.double_val;
}


// Returns a string reference which only valid as long as stream->data is valid
const char * vx_read_str(vx_code_input_stream_t * stream)
{
    int32_t remaining  = stream->len - stream->pos;

    char * str = (char*)(stream->data+stream->pos);
    int32_t str_size = strnlen(str, remaining);
    assert(remaining != str_size);  // Ensure there's a '\0' terminator

    stream->pos += str_size + 1; // +1 to account for null terminator
    return str;
}

const uint8_t * vx_read_bytes (vx_code_input_stream_t * stream, int datalen)
{
    ENSURE_SPACE(stream, datalen);

    const uint8_t * ret_data = stream->data + stream->pos;

    stream->pos += datalen;
    return ret_data;
}


static void vx_code_reset(vx_code_input_stream_t *stream)
{
    stream->pos = 0;
}

vx_code_input_stream_t * vx_code_input_stream_create(const uint8_t *data, uint32_t codes_len)
{
    vx_code_input_stream_t * stream = malloc(sizeof(vx_code_input_stream_t));
    stream->len = codes_len;
    stream->pos = 0;

    stream->data = malloc(sizeof(uint8_t)*stream->len);
    memcpy((uint8_t *)stream->data, data, sizeof(uint8_t)*stream->len);

    // Set function pointers
    stream->read_uint8 = vx_read_uint8;
    stream->read_uint32 = vx_read_uint32;
    stream->read_uint64 = vx_read_uint64;
    stream->read_float = vx_read_float;
    stream->read_double = vx_read_double;
    stream->read_str = vx_read_str;
    stream->read_bytes = vx_read_bytes;
    stream->reset = vx_code_reset;
    return stream;
}


void vx_code_input_stream_destroy(vx_code_input_stream_t * stream)
{
    free((uint8_t *)stream->data);
    free(stream);
}

int vx_code_input_stream_available(vx_code_input_stream_t * stream)
{
    return stream->len - stream->pos;
}
