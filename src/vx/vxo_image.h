#ifndef VXO_IMAGE_H
#define VXO_IMAGE_H

#include "imagesource/image_u32.h"
#include "imagesource/image_u8.h"

#include "vx_resc.h"
#include "vx_object.h"

// Can pass some flags to change the way the image is rendered
#define VXO_IMAGE_NOFLAGS 0
#define VXO_IMAGE_FLIPY   1
#define VXO_IMAGE_FLIPX   2
#define VXO_IMAGE_CCW90   4
#define VXO_IMAGE_CW90    8

#ifdef __cplusplus
extern "C" {
#endif

// img_flags is used for flipping/rotating the image
// tex_flags is for tell OpenGL how to filter the image
//   MIN_FILTER = when image is small (being minified), uses a mip-map
//   MAG_FILTER = when image is very large (being magnified), uses blurring
//   REPEAT sets sampling to wrap around near edges (e.g. for rendering
//      a sphere)

vx_object_t * vxo_image(vx_resc_t * tex, int width, int height, int stride, int pixel_format, int img_flags);
vx_object_t * vxo_image_texflags(vx_resc_t * tex, int width, int height, int stride,
                                 int format, int img_flags, int tex_flags);

// assumes an image whose byte order is R, G, B, A
// (that's (a<<24)+(b<<16)+(g<<8)+(r<<0) on little endian machines (x86, ARM)
// or (r<<24)+(g<<16)+(b<<8)+(a<<0) on big endian machines.
vx_object_t * vxo_image_from_u32(image_u32_t *im, int img_flags, int tex_flags);
vx_object_t * vxo_image_from_u8(image_u8_t *im, int img_flags, int tex_flags);

// Note, there's currently no image_u8 wrapper because the format of the u8 image is not necessarily fixed. It can contain RGB, GRAY, RGBA, ARGB, etc.

#ifdef __cplusplus
}
#endif
#endif
