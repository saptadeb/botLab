#ifndef __IMAGE_CONVERT_H__
#define __IMAGE_CONVERT_H__

#include "image_u32.h"
#include "image_u8x3.h"

#include "image_source.h"

#ifdef __cplusplus
extern "C" {
#endif

// Convert frame from imagesource to a useable image_u32_t
// packed like this:
// (a<<24)+(b<<16)+(g<<8)+r.
image_u32_t *
image_convert_u32 (image_source_data_t *frmd);

// r, g, b, r, g, b,...
image_u8x3_t *
image_convert_u8x3 (image_source_data_t *isdata);

#ifdef __cplusplus
}
#endif

#endif //__IMAGE_CONVERT_H__
