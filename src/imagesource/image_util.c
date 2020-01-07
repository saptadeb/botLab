#include "image_util.h"

#include <stdlib.h>
#include <assert.h>

//image_u8_t * image_util_convert_rgb_to_rgba(image_u8_t * input)
//{
/*    image_u8_t * output = calloc(1, sizeof(image_u8_t));
    output->bpp = 4;
    output->width = input->width;
    output->height = input->height;
    output->stride = output->width * output->bpp; // guarantee 4 byte alignment

    //printf("size %d height %d stride %d width %d\n", output->stride*output->height, output->height, output->stride, output->width);
    output->buf = calloc(output->stride*output->height, sizeof(uint8_t));
    assert(input->bpp == 3);

    for (int y = 0; y < input->height; y++)
        for (int x = 0; x < input->width; x++) {
            int in_idx = y*input->stride + x*input->bpp;
            int r = input->buf[in_idx + 0];
            int g = input->buf[in_idx + 1];
            int b = input->buf[in_idx + 2];
            int a = 0xff;

            int out_idx = y*output->stride +x*output->bpp;
            output->buf[out_idx + 0] = r;
            output->buf[out_idx + 1] = g;
            output->buf[out_idx + 2] = b;
            output->buf[out_idx + 3] = a;
        }

    return output;*/
//    return input; // XXX
//
//}

image_u32_t *
image_util_u32_decimate (const image_u32_t *orig, double decimate_factor)
{
    int new_width = (int)(orig->width / decimate_factor);
    int new_height = (int)(orig->height / decimate_factor);
    image_u32_t *output = image_u32_create_alignment (new_width, new_height, 4);

    for (int y = 0; y < output->height; y++)
        for (int x = 0; x < output->width; x++) {
            int new_idx = y*output->stride + x;

            int old_y = (int)(y*decimate_factor);
            int old_x = (int)(x*decimate_factor);

            int old_idx = old_y * orig->stride + old_x;

            output->buf[new_idx] = orig->buf[old_idx];
        }

    return output;
}
