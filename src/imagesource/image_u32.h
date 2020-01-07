#ifndef __IMAGE_U32_H__
#define __IMAGE_U32_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct image_u32 image_u32_t;
struct image_u32 {
    int width, height;
    int stride;

    uint32_t *buf;
};

/////////////////////////////////////
// IMPORTANT NOTE ON BYTE ORDER
//
// This format is designed to make dealing with 4 color channel images
// efficient by loading an entire pixel into a single uint32_t. This
// raises a number of endian-ness and format issues.
//
// The SUPPORTED format is byte-ordered: A, B, G, R. Whether the R
// channel ends up mapped to high-order bits or low-order bits depends
// on the endianness of your platform..
//
// On little-endian machines (x86, ARM), this will look like:
//
//     uint32_t v = (a<<24) + (b<<16) + (g<<8) + (r<<0)
//
// On big-endian machines, this will look like:
//
//     uint32_t v = (r<<24) + (g<<16) + (b<<8) + (a<<0)
//
// Obviously, you can do whatever you want, but if you don't adhere
// to this convention, you may find your color channels oddly swapped around
// if you convert between formats.
//
// Since most platforms are little endian, you could simply assume
// little-endian ordering and add:
//
// #ifdef __ORDER_BIG_ENDIAN
//   #error big endian not supported
// #endif

/////////////////////////////////////

// Create or load an image. returns NULL on failure
image_u32_t *
image_u32_create (int width, int height);

image_u32_t *
image_u32_create_alignment (int width, int height, int alignment);

image_u32_t *
image_u32_create_from_pnm (const char *path);

image_u32_t *
image_u32_copy (const image_u32_t *im);

void
image_u32_destroy (image_u32_t *im);

// Write a pnm. Returns 0 on success
// Currently only supports GRAY and ABGR. Does not write out alpha for ABGR
int
image_u32_write_pnm (const image_u32_t *im, const char *path);

#ifdef __cplusplus
}
#endif

#endif //__IMAGE_U32_H__
