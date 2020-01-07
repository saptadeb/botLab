#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_u8.h"
#include "pnm.h"

#define DEFAULT_ALIGNMENT 24

image_u8_t *
image_u8_create (int width, int height)
{
    return image_u8_create_alignment(width, height, DEFAULT_ALIGNMENT);
}

image_u8_t *
image_u8_create_alignment (int width, int height, int alignment)
{
    image_u8_t *im = calloc(1, sizeof(*im));

    im->width  = width;
    im->height = height;
    im->stride = width;

    if ((im->stride % alignment) != 0)
        im->stride += alignment - (im->stride % alignment);

    im->buf = (uint8_t*) calloc(1, im->height*im->stride);

    return im;
}

void
image_u8_destroy (image_u8_t *im)
{
    if (!im)
        return;

    free(im->buf);
    free(im);
}

////////////////////////////////////////////////////////////
// PNM file i/o

image_u8_t *
image_u8_create_from_pnm (const char *path)
{
    pnm_t *pnm = pnm_create_from_file(path);
    if (pnm == NULL)
        return NULL;

    image_u8_t *im = image_u8_create(pnm->width, pnm->height);

    switch (pnm->format) {
        case 5: {
            for (int y = 0; y < im->height; y++)
                memcpy(&im->buf[y*im->stride], &pnm->buf[y*im->width], im->width);

            pnm_destroy(pnm);
            return im;
        }

        case 6: {
            // Gray conversion for RGB is gray = (r + g + g + b)/4
            for (int y = 0; y < im->height; y++) {
                for (int x = 0; x < im->width; x++) {
                    uint8_t gray = (pnm->buf[y*im->width*3 + 3*x+0] +    // r
                                    pnm->buf[y*im->width*3 + 3*x+1] +    // g
                                    pnm->buf[y*im->width*3 + 3*x+1] +    // g
                                    pnm->buf[y*im->width*3 + 3*x+2])     // b
                        / 4;

                    im->buf[y*im->stride + x] = gray;
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
image_u8_write_pnm (const image_u8_t *im, const char *path)
{
    FILE *f = fopen(path, "wb");
    int res = 0;

    if (f == NULL) {
        res = -1;
        goto finish;
    }

    // Only outputs to grayscale
    fprintf(f, "P5\n%d %d\n255\n", im->width, im->height);

    for (int y = 0; y < im->height; y++) {
        if (im->width != fwrite(&im->buf[y*im->stride], 1, im->width, f)) {
            res = -2;
            goto finish;
        }
    }
finish:
    if (f != NULL)
        fclose(f);

    return res;
}

