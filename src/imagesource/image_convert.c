#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "image_u32.h"
#include "image_u8x3.h"
#include "image_convert.h"

////////////////////////////////////////////
// Guide to interpreting pixel formats
//
// U32  : byte-ordered R, G, B, A  (see image_u32.h)
//
// BGRA : byte-ordered B, G, R, A  (as used in 32BGRA on MacOS/iOS)
//
// RGBA : Format is byte-ordered, R, G, B, A.
//
// RGB / RGB24 / u8x3 : Format is byte-ordered, R, G, B
//
//
static inline int
clamp (int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return v;
}

// byte-order B, G, R, A ==> R, G, B, A
static image_u32_t *
convert_bgra_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);

    int width = im->width;
    int height = im->height;
    int stride = im->stride;

    uint8_t *out = (uint8_t*) im->buf;
    uint8_t *in  = (uint8_t*) frmd->data;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {

            int b = in[4*(y*width+x) + 0];
            int g = in[4*(y*width+x) + 1];
            int r = in[4*(y*width+x) + 2];
            int a = in[4*(y*width+x) + 3];

            out[4*(y*stride+x) + 0] = r;
            out[4*(y*stride+x) + 1] = g;
            out[4*(y*stride+x) + 2] = b;
            out[4*(y*stride+x) + 3] = a;
        }
    }

    return im;
}

// byte-order R, G, B, A ==> R, G, B, A... i.e., a copy.
static image_u32_t *
convert_rgba_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);

    int width = im->width;
    int height = im->height;
    int stride = im->stride;

    uint32_t *in = (uint32_t*) frmd->data;
    uint32_t *out = (uint32_t*) im->buf;

    for (int y = 0; y < height; y++)
        memcpy (&out[y*stride], &in[y*width], 4*width);

    return im;
}

// byte-order R, G, B, ==> R, G, B, 0xff
static image_u32_t *
convert_rgb24_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);

    int width = im->width;
    int height = im->height;
    int stride = im->stride;

    uint8_t *out = (uint8_t*) im->buf;
    uint8_t *in  = (uint8_t*) frmd->data;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {

            int r = in[3*(y*width+x) + 0];
            int g = in[3*(y*width+x) + 1];
            int b = in[3*(y*width+x) + 2];

            out[4*(y*stride+x) + 0] = r;
            out[4*(y*stride+x) + 1] = g;
            out[4*(y*stride+x) + 2] = b;
            out[4*(y*stride+x) + 3] = 0xff;
        }
    }

    return im;
}

static image_u32_t *
convert_yu12_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);

    int width = im->width;
    int height = im->height;
    int stride = im->stride;
    uint8_t *out = (uint8_t*) im->buf;
    uint8_t *in = (uint8_t*) frmd->data;

    assert (frmd->datalen == width*height + width*height / 4 + width*height / 4);

    // assumes three separate image planes:
    // 1) Y intensity at full resolution, (width*height bytes)
    // 2) Cb chroma data at half resolution (width/2 * height/2 bytes)
    // 2) Cr chroma data at half resolution (width/2 * height/2 bytes)
    uint8_t *ys  = &in[0];
    uint8_t *cbs = &in[width*height];
    uint8_t *crs = &in[width*height + width*height/4];

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {

            int yy = ys[y*width + x];
            int cr = crs[(y/2)*(width/2) + (x/2)];
            int cb = cbs[(y/2)*(width/2) + (x/2)];

            int r = clamp((int) ((298.082*yy + 408.583*cr) / 256) - 222.921);
            int g = clamp((int) ((298.082*yy - 100.291*cb - 208.120*cr) / 256) + 135.576);
            int b = clamp((int) ((298.082*yy + 516.412*cb) / 256) - 276.836);

            out[4*(y*stride+x) + 0] = r;
            out[4*(y*stride+x) + 1] = g;
            out[4*(y*stride+x) + 2] = b;
            out[4*(y*stride+x) + 3] = 0xff;
        }
    }
    return im;
}

static image_u32_t *
convert_yuyv_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);

    int width = im->width;
    int height = im->height;
    int stride = im->stride;

    int sstride = width*2;
    uint8_t *yuyv = (uint8_t*)(frmd->data);
    uint8_t *out = (uint8_t*) im->buf;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width / 2; x++) {

            int y1 = yuyv[y*sstride + 4*x+0]&0xff;
            int u  = yuyv[y*sstride + 4*x+1]&0xff;
            int y2 = yuyv[y*sstride + 4*x+2]&0xff;
            int v  = yuyv[y*sstride + 4*x+3]&0xff;

            int cb = ((u-128) * 454)>>8;
            int cr = ((v-128) * 359)>>8;
            int cg = ((v-128) * 183 + (u-128) * 88)>>8;

            int r, g, b;
            r = clamp(y1 + cr);
            b = clamp(y1 + cb);
            g = clamp(y1 - cg);

            out[4*(y*stride+2*x) + 0] = r;
            out[4*(y*stride+2*x) + 1] = g;
            out[4*(y*stride+2*x) + 2] = b;
            out[4*(y*stride+2*x) + 3] = 0xff;

            r = clamp(y2 + cr);
            b = clamp(y2 + cb);
            g = clamp(y2 - cg);

            out[4*(y*stride+2*x+1) + 0] = r;
            out[4*(y*stride+2*x+1) + 1] = g;
            out[4*(y*stride+2*x+1) + 2] = b;
            out[4*(y*stride+2*x+1) + 3] = 0xff;
        }
    }
    return im;
}

static image_u8x3_t *
convert_yuyv_to_u8x3 (image_source_data_t *frmd)
{
    image_u8x3_t *im = image_u8x3_create (frmd->ifmt.width,
                                          frmd->ifmt.height);

    int width = im->width;
    int height = im->height;
    int stride = im->stride;

    int sstride = width*2;
    uint8_t *yuyv = (uint8_t*)(frmd->data);
    uint8_t *out = (uint8_t*) im->buf;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width / 2; x++) {

            int y1 = yuyv[y*sstride + 4*x+0]&0xff;
            int u  = yuyv[y*sstride + 4*x+1]&0xff;
            int y2 = yuyv[y*sstride + 4*x+2]&0xff;
            int v  = yuyv[y*sstride + 4*x+3]&0xff;

            int cb = ((u-128) * 454)>>8;
            int cr = ((v-128) * 359)>>8;
            int cg = ((v-128) * 183 + (u-128) * 88)>>8;

            int r, g, b;
            r = clamp(y1 + cr);
            b = clamp(y1 + cb);
            g = clamp(y1 - cg);

            out[y*stride+6*x + 0] = r;
            out[y*stride+6*x + 1] = g;
            out[y*stride+6*x + 2] = b;

            r = clamp(y2 + cr);
            b = clamp(y2 + cb);
            g = clamp(y2 - cg);

            out[y*stride+6*x + 3] = r;
            out[y*stride+6*x + 4] = g;
            out[y*stride+6*x + 5] = b;
        }
    }
    return im;
}

static image_u32_t *
debayer_rggb_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);

    uint8_t *in = (uint8_t*) frmd->data;
    uint8_t *out = (uint8_t*) im->buf;

    int width = im->width;
    int height = im->height;
    int stride = im->stride;

    // loop over each 2x2 bayer block and compute the pixel values for each element.
    for (int y = 0; y < height; y+=2) {
        for (int x = 0; x < width; x+=2) {

            int r = 0, g = 0, b = 0;

            // compute indices into bayer pattern for the nine 2x2 blocks we'll use.
            int X00 = (y-2)*width+(x-2);
            int X01 = (y-2)*width+(x+0);
            int X02 = (y-2)*width+(x+2);
            int X10 = (y+0)*width+(x-2);
            int X11 = (y+0)*width+(x+0);
            int X12 = (y+0)*width+(x+2);
            int X20 = (y+2)*width+(x-2);
            int X21 = (y+2)*width+(x+0);
            int X22 = (y+2)*width+(x+2);

            // handle the edges of the screen.
            if (y < 2) {
                X00 += 2*width;
                X01 += 2*width;
                X02 += 2*width;
            }
            if (y+2 >= height) {
                X20 -= 2*width;
                X21 -= 2*width;
                X22 -= 2*width;
            }
            if (x < 2) {
                X00 += 2;
                X10 += 2;
                X20 += 2;
            }
            if (x+2 >= width) {
                X02 -= 2;
                X12 -= 2;
                X22 -= 2;
            }

            int idx = 4 * (y*stride + x);

            // top left pixel (R)
            r = (in[X11]);
            g = ((in[X01+width])+(in[X10+1])+(in[X11+1])+(in[X11+width])) / 4;
            b = ((in[X00+width+1])+(in[X10+width+1])+(in[X10+width+1])+(in[X11+width+1])) / 4;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;
            out[idx+3] = 0xff;

            // top right pixel (G)
            r = ((in[X11])+(in[X12])) / 2;
            g = (in[X11+1]);
            b = ((in[X01+width+1])+(in[X11+width+1])) / 2;
            out[idx+4] = r;
            out[idx+5] = g;
            out[idx+6] = b;
            out[idx+7] = 0xff;

            // bottom left pixel (G)
            r = ((in[X11])+(in[X21])) / 2;
            g = (in[X11+width]);
            b = ((in[X10+width+1])+(in[X11+width+1])) / 2;
            idx += 4*stride;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;
            out[idx+3] = 0xff;

            // bottom right pixel (B)
            r = ((in[X11])+(in[X12])+(in[X21])+(in[X22])) / 4;
            g = ((in[X11+1])+(in[X11+width])+(in[X12+width])+(in[X21+1]))/ 4;
            b = (in[X11+width+1]);
            out[idx+4] = r;
            out[idx+5] = g;
            out[idx+6] = b;
            out[idx+7] = 0xff;
        }
    }

    return im;
}

static image_u32_t *
debayer_rggb16_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);

    uint8_t *in = (uint8_t*) frmd->data;
    uint8_t *out = (uint8_t*) im->buf;

    int width = im->width;
    int height = im->height;
    int stride = im->stride;

    // loop over each 2x2 bayer block and compute the pixel values for each element.
    for (int y = 0; y < height; y+=2) {
        for (int x = 0; x < width; x+=2) {

            int r = 0, g = 0, b = 0;

            // compute indices into bayer pattern for the nine 2x2 blocks we'll use.
            int X00 = (y-2)*width+(x-2);
            int X01 = (y-2)*width+(x+0);
            int X02 = (y-2)*width+(x+2);
            int X10 = (y+0)*width+(x-2);
            int X11 = (y+0)*width+(x+0);
            int X12 = (y+0)*width+(x+2);
            int X20 = (y+2)*width+(x-2);
            int X21 = (y+2)*width+(x+0);
            int X22 = (y+2)*width+(x+2);

            // handle the edges of the screen.
            if (y < 2) {
                X00 += 2*width;
                X01 += 2*width;
                X02 += 2*width;
            }
            if (y+2 >= height) {
                X20 -= 2*width;
                X21 -= 2*width;
                X22 -= 2*width;
            }
            if (x < 2) {
                X00 += 2;
                X10 += 2;
                X20 += 2;
            }
            if (x+2 >= width) {
                X02 -= 2;
                X12 -= 2;
                X22 -= 2;
            }

            int idx = 4 * (y*stride + x);

            // top left pixel (R)
            r = ((in[2*X11]));
            g = ((in[2*(X01+width)])+(in[2*(X10+1)])+(in[2*(X11+1)])+(in[2*(X11+width)])) / 4;
            b = ((in[2*(X00+width+1)])+(in[2*(X10+width+1)])+(in[2*(X10+width+1)])+(in[2*(X11+width+1)])) / 4;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;
            out[idx+3] = 0xff;

            // top right pixel (G)
            r = ((in[2*X11])+(in[2*X12])) / 2;
            g = (in[2*(X11+1)]);
            b = ((in[2*(X01+width+1)])+(in[2*(X11+width+1)])) / 2;
            out[idx+4] = r;
            out[idx+5] = g;
            out[idx+6] = b;
            out[idx+7] = 0xff;

            // bottom left pixel (G)
            r = ((in[2*X11])+(in[2*X21])) / 2;
            g = (in[2*(X11+width)]);
            b = ((in[2*(X10+width+1)])+(in[2*(X11+width+1)])) / 2;
            idx += 4*stride;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;
            out[idx+3] = 0xff;

            // bottom right pixel (B)
            r = ((in[2*X11])+(in[2*X12])+(in[2*X21])+(in[2*X22])) / 4;
            g = ((in[2*(X11+1)])+(in[2*(X11+width)])+(in[2*(X12+width)])+(in[2*(X21+1)]))/ 4;
            b = (in[2*(X11+width+1)]);
            out[idx+4] = r;
            out[idx+5] = g;
            out[idx+6] = b;
            out[idx+7] = 0xff;
        }
    }

    return im;
}

static image_u32_t *
debayer_gbrg_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);

    uint8_t *in = (uint8_t*) frmd->data;
    uint8_t *out = (uint8_t*) im->buf;

    int width = im->width;
    int height = im->height;
    int stride = im->stride;

    // Loop over each 2x2 bayer block and compute the pixel values for
    // each element
    for (int y = 0; y < height; y+=2) {
        for (int x = 0; x < width; x+=2) {
            int r = 0, g = 0, b = 0;

             // compute indices into bayer pattern for the nine 2x2 blocks we'll use.
            int X00 = (y-2)*width+(x-2);
            int X01 = (y-2)*width+(x+0);
            int X02 = (y-2)*width+(x+2);
            int X10 = (y+0)*width+(x-2);
            int X11 = (y+0)*width+(x+0);
            int X12 = (y+0)*width+(x+2);
            int X20 = (y+2)*width+(x-2);
            int X21 = (y+2)*width+(x+0);
            int X22 = (y+2)*width+(x+2);

             // handle the edges of the screen.
            if (y < 2) {
                X00 += 2*width;
                X01 += 2*width;
                X02 += 2*width;
            }
            if (y+2 >= height) {
                X20 -= 2*width;
                X21 -= 2*width;
                X22 -= 2*width;
            }
            if (x < 2) {
                X00 += 2;
                X10 += 2;
                X20 += 2;
            }
            if (x+2 >= width) {
                X02 -= 2;
                X12 -= 2;
                X22 -= 2;
            }

            int idx = 4*(y*stride+x);

            // top left pixel (G)
            r = ((in[X01+width]) + (in[X11+width])) / 2;
            g = in[X11];
            b = ((in[X10+1]) + (in[X11+1])) / 2;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;
            out[idx+3] = 0xff;

            // top right pixel (B)
            r = ((in[X01+width])+(in[X02+width])+(in[X01+width]) + (in[X12+width])) / 4;
            g = ((in[X01+width+1])+(in[X11])+(in[X12])+(in[X11+width+1])) / 4;
            b = (in[X11+1]);
            out[idx+4] = r;
            out[idx+5] = g;
            out[idx+6] = b;
            out[idx+7] = 0xff;

            // bottom left pixel (R)
            r = (in[X11+width]);
            g = ((in[X11])+(in[X10+width+1])+(in[X11+width+1])+(in[X21])) / 4;
            b = ((in[X10+1])+(in[X11+1])+(in[X20+1])+(in[X21+1])) / 4;

            idx += 4*stride;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;
            out[idx+3] = 0xff;

            // bottom right pixel (G)
            r = ((in[X11+width])+(in[X12+width])) / 2;
            g = (in[X11+width+1]);
            b = ((in[X11+1])+(in[X21+1])) / 2;
            out[idx+4] = r;
            out[idx+5] = g;
            out[idx+6] = b;
            out[idx+7] = 0xff;
        }
    }

    return im;
}

// assumes MSB, LSB byte ordering
static image_u32_t *
debayer_gbrg16_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);

    uint8_t *in = (uint8_t*) frmd->data;
    uint8_t *out = (uint8_t*) im->buf;

    int width = im->width;
    int height = im->height;
    int stride = im->stride;

    // Loop over each 2x2 bayer block and compute the pixel values for
    // each element
    for (int y = 0; y < height; y+=2) {
        for (int x = 0; x < width; x+=2) {
            int r = 0, g = 0, b = 0;

            // compute indices into bayer pattern for the nine 2x2 blocks we'll use.
            int X00 = (y-2)*width+(x-2);
            int X01 = (y-2)*width+(x+0);
            int X02 = (y-2)*width+(x+2);
            int X10 = (y+0)*width+(x-2);
            int X11 = (y+0)*width+(x+0);
            int X12 = (y+0)*width+(x+2);
            int X20 = (y+2)*width+(x-2);
            int X21 = (y+2)*width+(x+0);
            int X22 = (y+2)*width+(x+2);

            // handle the edges of the screen.
            if (y < 2) {
                X00 += 2*width;
                X01 += 2*width;
                X02 += 2*width;
            }
            if (y+2 >= height) {
                X20 -= 2*width;
                X21 -= 2*width;
                X22 -= 2*width;
            }
            if (x < 2) {
                X00 += 2;
                X10 += 2;
                X20 += 2;
            }
            if (x+2 >= width) {
                X02 -= 2;
                X12 -= 2;
                X22 -= 2;
            }

            int idx = 4*(y*stride+x);

            // top left pixel (G)
            r = ((in[2*(X01+width)]) + (in[2*(X11+width)])) / 2;
            g = in[2*X11];
            b = ((in[2*(X10+1)]) + (in[2*(X11+1)])) / 2;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;
            out[idx+3] = 0xff;

            // top right pixel (B)
            r = ((in[2*(X01+width)])+(in[2*(X02+width)])+(in[2*(X01+width)]) + (in[2*(X12+width)])) / 4;
            g = ((in[2*(X01+width+1)])+(in[2*X11])+(in[2*X12])+(in[2*(X11+width+1)])) / 4;
            b = (in[2*(X11+1)]);
            out[idx+4] = r;
            out[idx+5] = g;
            out[idx+6] = b;
            out[idx+7] = 0xff;

            // bottom left pixel (R)
            r = (in[2*(X11+width)]);
            g = ((in[2*X11])+(in[2*(X10+width+1)])+(in[2*(X11+width+1)])+(in[2*X21])) / 4;
            b = ((in[2*(X10+1)])+(in[2*(X11+1)])+(in[2*(X20+1)])+(in[2*(X21+1)])) / 4;

            idx += 4*stride;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;
            out[idx+3] = 0xff;

            // bottom right pixel (G)
            r = ((in[2*(X11+width)])+(in[2*(X12+width)])) / 2;
            g = (in[2*(X11+width+1)]);
            b = ((in[2*(X11+1)])+(in[2*(X21+1)])) / 2;
            out[idx+4] = r;
            out[idx+5] = g;
            out[idx+6] = b;
            out[idx+7] = 0xff;
        }
    }

    return im;
}

static image_u8x3_t *
debayer_gbrg_to_u8x3 (image_source_data_t *frmd)
{
    image_u8x3_t *im = image_u8x3_create (frmd->ifmt.width,
                                          frmd->ifmt.height);

    uint8_t *in = (uint8_t*) frmd->data;
    uint8_t *out = (uint8_t*) im->buf;

    int width = im->width;
    int height = im->height;
    int stride = im->stride;

    // Loop over each 2x2 bayer block and compute the pixel values for
    // each element
    for (int y = 0; y < height; y+=2) {
        for (int x = 0; x < width; x+=2) {
            int r = 0, g = 0, b = 0;

            // compute indices into bayer pattern for the nine 2x2 blocks we'll use.
            int X00 = (y-2)*width+(x-2);
            int X01 = (y-2)*width+(x+0);
            int X02 = (y-2)*width+(x+2);
            int X10 = (y+0)*width+(x-2);
            int X11 = (y+0)*width+(x+0);
            int X12 = (y+0)*width+(x+2);
            int X20 = (y+2)*width+(x-2);
            int X21 = (y+2)*width+(x+0);
            int X22 = (y+2)*width+(x+2);

            // handle the edges of the screen.
            if (y < 2) {
                X00 += 2*width;
                X01 += 2*width;
                X02 += 2*width;
            }
            if (y+2 >= height) {
                X20 -= 2*width;
                X21 -= 2*width;
                X22 -= 2*width;
            }
            if (x < 2) {
                X00 += 2;
                X10 += 2;
                X20 += 2;
            }
            if (x+2 >= width) {
                X02 -= 2;
                X12 -= 2;
                X22 -= 2;
            }

            int idx = y*stride + 3*x;

            // top left pixel (G)
            r = ((in[X01+width]) + (in[X11+width])) / 2;
            g = in[X11];
            b = ((in[X10+1]) + (in[X11+1])) / 2;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;

            // top right pixel (B)
            r = ((in[X01+width])+(in[X02+width])+(in[X01+width]) + (in[X12+width])) / 4;
            g = ((in[X01+width+1])+(in[X11])+(in[X12])+(in[X11+width+1])) / 4;
            b = (in[X11+1]);
            out[idx+3] = r;
            out[idx+4] = g;
            out[idx+5] = b;

            // bottom left pixel (R)
            r = (in[X11+width]);
            g = ((in[X11])+(in[X10+width+1])+(in[X11+width+1])+(in[X21])) / 4;
            b = ((in[X10+1])+(in[X11+1])+(in[X20+1])+(in[X21+1])) / 4;

            idx += stride;
            out[idx+0] = r;
            out[idx+1] = g;
            out[idx+2] = b;

            // bottom right pixel (G)
            r = ((in[X11+width])+(in[X12+width])) / 2;
            g = (in[X11+width+1]);
            b = ((in[X11+1])+(in[X21+1])) / 2;
            out[idx+3] = r;
            out[idx+4] = g;
            out[idx+5] = b;
        }
    }

    return im;
}

static image_u32_t *
gray8_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);
    uint8_t *buf = (uint8_t*)(frmd->data);
    for (int y = 0; y < im->height; y++) {
        for (int x = 0; x < im->width; x++) {
            int idx = y*im->width + x;
            int gray = buf[idx];
            im->buf[y*im->stride+x] = (0xff000000) | gray << 16 | gray << 8 | gray;
        }
    }

    return im;
}

// byte-order MSB, LSB
static image_u32_t *
gray16_to_u32 (image_source_data_t *frmd)
{
    image_u32_t *im = image_u32_create (frmd->ifmt.width,
                                        frmd->ifmt.height);
    uint8_t *buf = (uint8_t*)(frmd->data);

    for (int y = 0; y < im->height; y++) {
        for (int x = 0; x < im->width; x++) {
            int idx = y*im->width + x;
            int gray = buf[2*idx];
            im->buf[y*im->stride+x] = (0xff000000) | gray << 16 | gray << 8 | gray;
        }
    }

    return im;
}

// convert to ABGR format (MSB to LSB, host byte ordering.)
image_u32_t *
image_convert_u32 (image_source_data_t *isdata)
{
    if (0==strcmp ("BAYER_GBRG", isdata->ifmt.format))
        return debayer_gbrg_to_u32 (isdata);

    else if (0==strcmp ("BAYER_GBRG16", isdata->ifmt.format))
        return debayer_gbrg16_to_u32 (isdata);

    else if (0==strcmp ("BAYER_RGGB", isdata->ifmt.format))
        return debayer_rggb_to_u32 (isdata);

    else if (0==strcmp ("BAYER_RGGB16", isdata->ifmt.format))
        return debayer_rggb16_to_u32 (isdata);

    else if (0==strcmp ("GRAY8", isdata->ifmt.format) ||  0==strcmp ("GRAY", isdata->ifmt.format))
        return gray8_to_u32 (isdata);

    else if (0==strcmp ("GRAY16", isdata->ifmt.format))
        return gray16_to_u32 (isdata);


//    } else if (!strcmp("BAYER_GRBG", isdata->ifmt.format)) {

//    } else if (!strcmp("GRAY16", isdata->ifmt->format)) {

    else if (0==strcmp ("RGB", isdata->ifmt.format))
        return convert_rgb24_to_u32 (isdata);

    else if (0==strcmp ("BGRA", isdata->ifmt.format))
        return convert_bgra_to_u32 (isdata);

    else if (0==strcmp ("RGBA", isdata->ifmt.format))
        return convert_rgba_to_u32 (isdata);

    else if (0==strcmp ("YUYV", isdata->ifmt.format))
        return convert_yuyv_to_u32 (isdata);

    else if (0==strcmp ("YU12", isdata->ifmt.format))
        return convert_yu12_to_u32 (isdata);

    else {
        printf ("ERR: Format %s not supported. (width %d, height %d, datalen %d)\n",
                isdata->ifmt.format, isdata->ifmt.width, isdata->ifmt.height, isdata->datalen);
    }

    return NULL;
}

static int image_convert_u8x3_slow_warned = 0;

image_u8x3_t *
image_convert_u8x3 (image_source_data_t *isdata)
{
    if (0==strcmp ("BAYER_GBRG", isdata->ifmt.format))
        return debayer_gbrg_to_u8x3 (isdata);
    else if (0==strcmp ("YUYV", isdata->ifmt.format))
        return convert_yuyv_to_u8x3 (isdata);

    // last resort: try the u32 conversions, then convert u32->u8x3
    image_u32_t *im32 = image_convert_u32 (isdata);
    if (im32 == NULL)
        return NULL;

    if (!image_convert_u8x3_slow_warned) {
        image_convert_u8x3_slow_warned = 1;
        printf ("WARNING: Slow image format conversion, %s->u8x3\n",
                isdata->ifmt.format);
    }

    image_u8x3_t *im = image_u8x3_create (im32->width, im32->height);
    for (int y = 0; y < im32->height; y++) {
        for (int x = 0; x < im32->width; x++) {

            uint32_t abgr = im32->buf[y*im32->stride+x];
            im->buf[y*im->stride + 3*x + 0] = abgr & 0xff;
            im->buf[y*im->stride + 3*x + 1] = (abgr>>8) & 0xff;
            im->buf[y*im->stride + 3*x + 2] = (abgr>>16) & 0xff;
        }
    }

    image_u32_destroy (im32);
    return im;
}
