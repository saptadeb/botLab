#ifndef _VX_CODES_h
#define _VX_CODES_h

#ifdef __cplusplus
extern "C" {
#endif

#include "vx_key_codes.h"

// These opcodes are available in the "top-level" context, and NOT
// within a program context.
#define OP_PROGRAM       1
#define OP_MODEL_PUSH    2
#define OP_MODEL_POP     3
#define OP_MODEL_IDENT   4
#define OP_MODEL_MULTF   5
#define OP_DEPTH_ENABLE  7
#define OP_DEPTH_DISABLE 8
#define OP_DEPTH_PUSH    9
#define OP_DEPTH_POP    10
#define OP_PROJ_PUSH      11
#define OP_PROJ_POP       12
#define OP_PROJ_PIXCOORDS 13
#define OP_PROJ_RELCOORDS 14

// Note: model matrix contains displacement from world origin
//       proj matrix contains projection + camera displacement

// These opcodes are available from within a program context.
#define OP_ELEMENT_ARRAY      100
#define OP_VERT_ATTRIB        101
#define OP_UNIFORM_MATRIX_FV  102
#define OP_TEXTURE            103
#define OP_MODEL_MATRIX_44    104
#define OP_PM_MAT_NAME        105
#define OP_VALIDATE_PROGRAM   106
#define OP_DRAW_ARRAY         107
#define OP_UNIFORM_VECTOR_FV  108
#define OP_LINE_WIDTH         109
#define OP_NORMAL_MAT_NAME    110
#define OP_MODEL_MAT_NAME     111
#define OP_CAM_POS_NAME       112
#define OP_UNIFORM_VECTOR_IV  113

// Super top-level codes. Command-type specifiers
#define OP_BUFFER_CODES           1000
#define OP_LAYER_INFO             1001

#define OP_BUFFER_RESOURCES       1003
#define OP_DEALLOC_RESOURCES      1004

#define OP_LAYER_VIEWPORT_REL     1008
#define OP_LAYER_VIEWPORT_ABS     1009

// These commands may optionally be ignored by a remote (or other) display implementation
#define OP_LAYER_CAMERA           1005
#define OP_WINDOW_SCREENSHOT      1006
#define OP_BUFFER_ENABLED         1007
#define OP_WINDOW_MOVIE_RECORD    1010
#define OP_WINDOW_MOVIE_STOP      1011
#define OP_WINDOW_SCENE           1012

// Available within OP_LAYER_CAMERA context
#define OP_PROJ_PERSPECTIVE 1
#define OP_PROJ_ORTHO       2
#define OP_FIT2D            3
#define OP_LOOKAT           4
#define OP_FOLLOW_MODE      5
#define OP_DEFAULTS         6
#define OP_MANIP_POINT      7 // XXX placeholder
#define OP_FOLLOW_MODE_DISABLE 8
#define OP_INTERFACE_MODE   9

#define VX_FLOAT_ARRAY  0
#define VX_BYTE_ARRAY   1
#define VX_INT_ARRAY    2

// Layer alignment
#define OP_ANCHOR_NONE           1
#define OP_ANCHOR_TOP_LEFT       2
#define OP_ANCHOR_TOP            3
#define OP_ANCHOR_TOP_RIGHT      4
#define OP_ANCHOR_RIGHT          5
#define OP_ANCHOR_BOTTOM_RIGHT   6
#define OP_ANCHOR_BOTTOM         7
#define OP_ANCHOR_BOTTOM_LEFT    8
#define OP_ANCHOR_LEFT           9
#define OP_ANCHOR_CENTER        10

// pixcoord origins
#define VX_ORIGIN_NONE           1
#define VX_ORIGIN_TOP_LEFT       2
#define VX_ORIGIN_TOP            3
#define VX_ORIGIN_TOP_RIGHT      4
#define VX_ORIGIN_RIGHT          5
#define VX_ORIGIN_BOTTOM_RIGHT   6
#define VX_ORIGIN_BOTTOM         7
#define VX_ORIGIN_BOTTOM_LEFT    8
#define VX_ORIGIN_LEFT           9
#define VX_ORIGIN_CENTER        10
#define VX_ORIGIN_CENTER_ROUND  11

// Texture attributes

#define VX_TEX_MIN_FILTER 1
#define VX_TEX_MAG_FILTER 2
#define VX_TEX_REPEAT     4


// Events

#define VX_SHIFT_MASK    1
#define VX_CTRL_MASK     2
#define VX_WIN_MASK      4
#define VX_ALT_MASK      8
#define VX_CAPS_MASK    16
#define VX_NUM_MASK     32

#define VX_BUTTON1_MASK    1
#define VX_BUTTON2_MASK    2
#define VX_BUTTON3_MASK    4

#define VX_TOUCH_DOWN    1
#define VX_TOUCH_MOVE    2
#define VX_TOUCH_UP      3

// XXX should these GL types be defined here??? or included from GL.h?
// OpenGL Es 2.0 Types:
// GL_POINTS, GL_LINE_STRIP, GL_LINE_LOOP, GL_LINES,
// GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, and GL_TRIANGLES
#define GL_POINTS          0x0000
#define GL_LINES           0x0001
#define GL_LINE_LOOP       0x0002
#define GL_LINE_STRIP      0x0003
#define GL_TRIANGLES       0x0004
#define GL_TRIANGLE_STRIP  0x0005
#define GL_TRIANGLE_FAN    0x0006

/* Data types */
#define GL_BYTE					0x1400
#define GL_UNSIGNED_BYTE			0x1401
#define GL_SHORT				0x1402
#define GL_UNSIGNED_SHORT			0x1403
#define GL_INT					0x1404
#define GL_UNSIGNED_INT				0x1405
#define GL_FLOAT				0x1406
#define GL_2_BYTES				0x1407
#define GL_3_BYTES				0x1408
#define GL_4_BYTES				0x1409
#define GL_DOUBLE				0x140A

/* Image formats */
#define GL_ALPHA				0x1906
#define GL_RGB					0x1907
#define GL_RGBA					0x1908
#define GL_LUMINANCE			0x1909
#define GL_LUMINANCE_ALPHA	   	0x190A

#ifdef __cplusplus
}
#endif

#endif
