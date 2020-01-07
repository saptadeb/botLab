#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_u8x3.h"

// least common multiple of 32 (cache line) and 24 (stride needed for
// 8byte-wide RGB processing). (It's possible that 24 would be enough).
#define DEFAULT_ALIGNMENT 96

image_u8x3_t *
image_u8x3_create (int width, int height)
{
    return image_u8x3_create_alignment(width, height, DEFAULT_ALIGNMENT);
}

// force stride to be a multiple of 'alignment' bytes.
image_u8x3_t *
image_u8x3_create_alignment (int width, int height, int alignment)
{
    assert(alignment > 0);

    image_u8x3_t *im = (image_u8x3_t*) calloc(1, sizeof(image_u8x3_t));

    im->width  = width;
    im->height = height;
    im->stride = width*3;

    if ((im->stride % alignment) != 0)
        im->stride += alignment - (im->stride % alignment);

    im->buf = (uint8_t*) calloc(1, im->height*im->stride);

    return im;
}

void
image_u8x3_destroy (image_u8x3_t *im)
{
    if (!im)
        return;

    free(im->buf);
    free(im);
}

int
image_u8x3_write_pnm (const image_u8x3_t *im, const char *path)
{
    FILE *f = fopen(path, "wb");
    int res = 0;

    if (f == NULL) {
        res = -1;
        goto finish;
    }

    // Only outputs to RGB
    fprintf(f, "P6\n%d %d\n255\n", im->width, im->height);
    int linesz = im->width * 3;
    for (int y = 0; y < im->height; y++) {
        if (linesz != fwrite(&im->buf[y*im->stride], 1, linesz, f)) {
            res = -1;
            goto finish;
        }
    }

finish:
    if (f != NULL)
        fclose(f);

    return res;
}
