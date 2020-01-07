#ifndef __IMAGE_UTIL_H__
#define __IMAGE_UTIL_H__

#include "image_u8.h"
#include "image_u32.h"

#ifdef __cplusplus
extern "C" {
#endif

// Convert an rgb-format image with arbitrary stride to a tightly packed 4-bytes per pixel
// image suitable for vx use
// user is responsible for freeing the output
//image_u8_t *
//image_util_convert_rgb_to_rgba (image_u8_t *rgb_input);


// returns an aliased image of w/h= (int)(orig->width/decimate_factor),
// (int)(orig->height/decimate_factor)
image_u32_t *
image_util_u32_decimate (const image_u32_t *orig, double decimate_factor);

#ifdef __cplusplus
}
#endif

#endif //__IMAGE_UTIL_H__
