#ifndef VX_TCP_UTIL_H
#define VX_TCP_UTIL_H

#include "common/zhash.h"
#include "vx_code_input_stream.h"
#include "vx_code_output_stream.h"
#include "vx_camera_pos.h"

// collection of functions related to marshaling over TCP stream. Might
// eventually rename to vx_remote_util

zhash_t * vx_tcp_util_unpack_resources(vx_code_input_stream_t * cins);
void vx_tcp_util_pack_resources(zhash_t * resources, vx_code_output_stream_t * couts);


void vx_tcp_util_pack_camera_pos(const vx_camera_pos_t * pos, vx_code_output_stream_t * couts);


void vx_tcp_util_unpack_camera_pos(vx_code_input_stream_t * cins, vx_camera_pos_t * output);

#endif
