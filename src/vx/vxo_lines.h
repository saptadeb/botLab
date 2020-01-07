#ifndef VX_LINES_H
#define VX_LINES_H

#include "vx_object.h"
#include "vx_resc.h"
#include "vx_style.h"
#include "vx_codes.h" // for GL_LINE_*

#ifdef __cplusplus
extern "C" {
#endif

#define VXO_LINES_STYLE 0x651a5fbb

// These style can be passed anytime a vxo_ object requires a styling.

// single color, caller owns color4 data
vx_style_t * vxo_lines_style(const float * color4, int pt_size);

// per vertex color, each one is 4 floats, RGBA
vx_style_t * vxo_lines_style_multi_colored(vx_resc_t * colors, int pt_size);

// basic line drawing. Type should be one of GL_LINES, GL_LINE_LOOP or GL_LINE_STRIP
vx_object_t * vxo_lines(vx_resc_t * points, int npoints, int type, vx_style_t * style);

// Use indexed rendering via glDrawElements. Allows reusing vertex data
vx_object_t * vxo_lines_indexed(vx_resc_t * points, int npoints, vx_resc_t * indices,
                                int type, vx_style_t * style);


#ifdef __cplusplus
}
#endif

#endif
