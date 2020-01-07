#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_u32.h"
#include "pnm.h"

#define DEFAULT_ALIGNMENT 8

image_u32_t *
image_u32_create (int width, int height)
{
    return image_u32_create_alignment(width, height, DEFAULT_ALIGNMENT);
}

// alignment specified in units of uint32
image_u32_t *
image_u32_create_alignment (int width, int height, int alignment)
{
    image_u32_t *im = calloc (1, sizeof(*im));

    im->width  = width;
    im->height = height;
    im->stride = width;

    if ((im->stride % alignment) != 0)
        im->stride += alignment - (im->stride % alignment);

    im->buf = (uint32_t*) calloc(1, im->height*im->stride*sizeof(uint32_t));

    return im;
}


image_u32_t *
image_u32_copy (const image_u32_t *im)
{
    image_u32_t *out = image_u32_create (im->width, im->height);
    memcpy (out->buf, im->buf, im->height*im->stride*sizeof(uint32_t));
    return out;
}

void
image_u32_destroy (image_u32_t *im)
{
    if (im == NULL)
        return;

    free(im->buf);
    free(im);
}

////////////////////////////////////////////////////////////
// PNM file i/o

// Create an RGBA image from PNM
// TODO Refactor this to load u32 and convert to u32 using existing function
image_u32_t *
image_u32_create_from_pnm (const char *path)
{
    pnm_t *pnm = pnm_create_from_file(path);
    if (pnm == NULL)
        return NULL;

    image_u32_t *im = image_u32_create(pnm->width, pnm->height);

    switch (pnm->format) {
        case 5: {
            for (int y = 0; y < im->height; y++) {
                for (int x = 0; x < im->width; x++) {
                    uint8_t gray = pnm->buf[y*im->width + x];
                    im->buf[y*im->stride + x] = 0xff000000 | (gray << 16) | (gray << 8) | (gray << 0);
                }
            }

            pnm_destroy(pnm);
            return im;
        }

        case 6: {
            // Gray conversion for RGB is gray = (r + g + g + b)/4
            for (int y = 0; y < im->height; y++) {
                for (int x = 0; x < im->width; x++) {
                    uint8_t a = 0xff;
                    uint8_t r = pnm->buf[y*im->width*3 + 3*x];
                    uint8_t g = pnm->buf[y*im->width*3 + 3*x+1];
                    uint8_t b = pnm->buf[y*im->width*3 + 3*x+2];

                    im->buf[y*im->stride + x] = (a & 0xff) << 24 | (b & 0xff) << 16 | (g & 0xff) << 8 | r;
                }
            }

            pnm_destroy(pnm);
            return im;
        }
    }

    pnm_destroy(pnm);
    return NULL;
}

int
image_u32_write_pnm (const image_u32_t *im, const char *path)
{
    FILE *f = fopen(path, "wb");
    int res = 0;

    if (f == NULL) {
        res = -1;
        goto finish;
    }

    // Only outputs to RGB
    fprintf(f, "P6\n%d %d\n255\n", im->width, im->height);

    for (int y = 0; y < im->height; y++) {
        for (int x = 0; x < im->width; x++) {
            uint32_t abgr = im->buf[y*im->stride + x];
            uint8_t r = (uint8_t)((abgr >> 0) & 0xff);
            uint8_t g = (uint8_t)((abgr >> 8) & 0xff);
            uint8_t b = (uint8_t)((abgr >> 16) & 0xff);

            fwrite(&r, 1, 1, f);
            fwrite(&g, 1, 1, f);
            fwrite(&b, 1, 1, f);
        }
    }
finish:
    if (f != NULL)
        fclose(f);

    return res;
}

////////////////////////////////////////////////////////////
// Conversion

