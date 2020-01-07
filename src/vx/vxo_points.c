#include <stdarg.h>
#include <stdlib.h>

#include "vxo_points.h"
#include "vx_program.h"
#include "vx_codes.h"
#include "vxo_chain.h"

#include "vx_util.h"

enum
{
    STYLE_SINGLE_COLOR,
    STYLE_MULTI_COLOR
};

typedef struct
{
    int program_type; // see enum above

    // only populated in certain type
    float color[4]; // might be undefined, depending on type
    vx_resc_t * colors;

    float pt_size;

} vx_points_style_t;

static vx_program_t * get_program(vx_style_t * sty)
{
    vx_points_style_t * point_style = sty->impl;
    vx_program_t * prog = NULL;

    if (point_style->program_type == STYLE_SINGLE_COLOR) {
        prog = vx_program_load_library("single-color");
        vx_program_set_uniform4fv(prog, "color", point_style->color);
    } else if (point_style->program_type == STYLE_MULTI_COLOR) {
        prog = vx_program_load_library("multi-colored");
        vx_program_set_vertex_attrib(prog, "color", point_style->colors, 4);
    }

    vx_program_set_line_width(prog, point_style->pt_size);

    return prog;
}

static void style_destroy(vx_style_t * sty)
{
    vx_points_style_t * point_style = sty->impl;
    if (point_style->colors != NULL) {
        vx_resc_dec_destroy(point_style->colors);
    }
    free(point_style);
    free(sty);
}

vx_style_t * vxo_points_style(const float * color4, int pt_size)
{
    vx_points_style_t * point_style = calloc(1,sizeof(vx_points_style_t));
    memcpy(point_style->color, color4, sizeof(float)*4);
    point_style->pt_size = pt_size;
    point_style->program_type = STYLE_SINGLE_COLOR;

    vx_style_t * style = calloc(1, sizeof(vx_style_t));
    style->impl = point_style;
    style->type = VXO_POINTS_STYLE;
    style->get_program = get_program;
    style->destroy =  style_destroy;

    return style;
}

vx_style_t * vxo_points_style_multi_colored(vx_resc_t * colors, int pt_size)
{
    vx_points_style_t * point_style = calloc(1,sizeof(vx_points_style_t));
    point_style->colors = colors;
    vx_resc_inc_ref(point_style->colors);
    point_style->pt_size = pt_size;
    point_style->program_type = STYLE_MULTI_COLOR;

    vx_style_t * style = calloc(1, sizeof(vx_style_t));
    style->impl = point_style;
    style->type = VXO_POINTS_STYLE;
    style->get_program = get_program;
    style->destroy =  style_destroy;

    return style;
}

vx_object_t * vxo_points(vx_resc_t * vertex_data, int npoints, vx_style_t * style)
{
    vx_style_inc_ref(style);
    vx_object_t * vc = vxo_chain_create();

    if (npoints == 0) {
        vx_style_dec_destroy(style);
        return vc;
    }

    int pdim = vertex_data->count / npoints; // 2D or 3D?

    // each style needs to know which program to load, and should also
    // bind any style data on creation

    vx_program_t * prog = style->get_program(style);
    vx_program_set_vertex_attrib(prog, "position", vertex_data, pdim);
    vx_program_set_draw_array(prog, npoints, GL_POINTS);

    vxo_chain_add(vc, prog->super);

    vx_style_dec_destroy(style);
    return vc;
}

vx_object_t * vxo_points_indexed(vx_resc_t * vertex_data, int npoints,
                                 vx_resc_t * indices, vx_style_t * style)
{
    vx_style_inc_ref(style);
    vx_object_t * vc = vxo_chain_create();

    if (npoints == 0) {
        vx_style_dec_destroy(style);
        return vc;
    }

    int pdim = vertex_data->count / npoints; // 2D or 3D?

    // each style needs to know which program to load, and should bind
    // any style data on creation
    vx_program_t * prog = style->get_program(style);
    vx_program_set_vertex_attrib(prog, "position", vertex_data, pdim);
    vx_program_set_element_array(prog, indices, GL_POINTS);

    vxo_chain_add(vc, prog->super);

    vx_style_dec_destroy(style);
    return vc;
}
