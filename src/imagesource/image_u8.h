#ifndef __IMAGE_U8_H__
#define __IMAGE_U8_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct image_u8 image_u8_t;
struct image_u8 {
    int width, height;
    int stride;

    uint8_t *buf;
};

// Create or load an image. returns NULL on failure. Uses default
// stride alignment.
image_u8_t *
image_u8_create (int width, int height);

// Force stride to be a multiple of 'alignment' bytes.
image_u8_t *
image_u8_create_alignment (int width, int height, int alignment);

image_u8_t *
image_u8_create_from_pnm (const char *path);

void
image_u8_destroy (image_u8_t *im);

// Write a pnm. Returns 0 on success
// Currently only supports GRAY and RGBA. Does not write out alpha for RGBA
int
image_u8_write_pnm (const image_u8_t *im, const char *path);

// Convert an image to the bpp specified. Currently supports GRAY, RGB, and RGBA.
// ARGB would be mistaken for RGBA. Alpha is ignored when converting from RGBA
// and is set to 0xFF when converting to RGBA. Grayscale conversion uses:
//     i = (r + g + g + b)/4)
// If im->bpp == desired_bpp, a copy is made
// XXX Deal with this later...
image_u8_t *
image_u8_convert (const image_u8_t *im, int desired_bpp);

#ifdef __cplusplus
}
#endif

#endif //__IMAGE_U8_H__
