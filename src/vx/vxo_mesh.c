#include "vxo_mesh.h"
#include "vxo_chain.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct
{
    float * color_ambient3;
    float * color_diffuse3;
    float * color_specular3;

    float transparency;
    float specularity;
    int type;
} fancy_t;


static float lights_xyz[VXO_MESH_LIGHT_COUNT][3] = {{100.0f, 150.0f, 120.0f},
                                                    {-100.0f, -150.0f, 120.0f}};

void vxo_mesh_get_light(int light_idx, float * xyz)
{
    memcpy(xyz, lights_xyz[light_idx], sizeof(float)*3);
}

void vxo_mesh_set_light(int light_idx, const float * light_xyz)
{
    memcpy(lights_xyz[light_idx], light_xyz, sizeof(float)*3);
}


// Let's try to make all shaders that use lighting employ this standard
// set of 2 lights, allows code reuse
static void set_lights(vx_program_t * prog,
                       float ambient1_v, float diffuse1_v, float specular1_v,
                       float ambient2_v, float diffuse2_v, float specular2_v)
{
    // Hard code the position these two  (white) lights. Implement your own style,
    // shader pair if you want something else!

    vx_program_set_uniform3fv(prog, "light1", lights_xyz[0]);

    float light1ambient[] =  { ambient1_v, ambient1_v, ambient1_v};
    vx_program_set_uniform3fv(prog, "light1ambient", light1ambient);
    float light1diffuse[] =  { diffuse1_v, diffuse1_v, diffuse1_v};
    vx_program_set_uniform3fv(prog, "light1diffuse", light1diffuse);
    float light1specular[] = { specular1_v, specular1_v, specular1_v};
    vx_program_set_uniform3fv(prog, "light1specular", light1specular);


    vx_program_set_uniform3fv(prog, "light2", lights_xyz[1]);

    float light2ambient[]  = { ambient2_v, ambient2_v, ambient2_v};
    vx_program_set_uniform3fv(prog, "light2ambient", light2ambient);
    float light2diffuse[]  = { diffuse2_v, diffuse2_v, diffuse2_v };
    vx_program_set_uniform3fv(prog, "light2diffuse", light2diffuse);
    float light2specular[] = { specular2_v, specular2_v, specular2_v};
    vx_program_set_uniform3fv(prog, "light2specular", light2specular);

}

static vx_program_t * get_program(vx_style_t * sty)
{
    vx_program_t * prog = vx_program_load_library("diffuse-single-color");

    set_lights(prog, 0.4, 0.4, 0.5,
               0.3, 0.2, 0.3);

    vx_program_set_uniform4fv(prog, "color", sty->impl);

    vx_program_set_flags(prog, VX_PROGRAM_USE_N);
    vx_program_set_flags(prog, VX_PROGRAM_USE_M);
    //vx_program_set_flags(prog, VX_PROGRAM_USE_CAM_POS);

    return prog;
}

static vx_program_t * get_program_multi(vx_style_t * sty)
{
    vx_program_t * prog = vx_program_load_library("vcolor_diffuse");

    set_lights(prog, 0.4, 0.4, 0.5,
               0.3, 0.2, 0.3);

    vx_program_set_vertex_attrib(prog, "color", (vx_resc_t*)sty->impl, 4);

    vx_program_set_flags(prog, VX_PROGRAM_USE_N);
    vx_program_set_flags(prog, VX_PROGRAM_USE_M);
    //vx_program_set_flags(prog, VX_PROGRAM_USE_CAM_POS);

    return prog;
}

static vx_program_t * get_program_solid(vx_style_t * sty)
{
    vx_program_t * prog = vx_program_load_library("single-color");

    vx_program_set_uniform4fv(prog, "color", sty->impl);

    return prog;
}

static vx_program_t * get_program_solid_multi(vx_style_t * sty)
{
    vx_program_t * prog = vx_program_load_library("multi-colored");
    vx_program_set_vertex_attrib(prog, "color", sty->impl, 4);

    return prog;
}


static vx_program_t * get_program_fancy(vx_style_t * sty)
{
    fancy_t * data = sty->impl;

    vx_program_t * prog = vx_program_load_library("phong-single-color");

    set_lights(prog, 0.4, 0.4, 0.5,
               0.3, 0.2, 0.5);

    vx_program_set_uniform3fv(prog, "material_ambient", data->color_ambient3);
    vx_program_set_uniform3fv(prog, "material_diffuse", data->color_diffuse3);
    vx_program_set_uniform3fv(prog, "material_specular", data->color_specular3);

    vx_program_set_uniform1fv(prog, "transparency", &data->transparency);
    vx_program_set_uniform1iv(prog, "type", &data->type);

    vx_program_set_uniform1fv(prog, "specularity", &data->specularity);

    vx_program_set_flags(prog, VX_PROGRAM_USE_N);
    vx_program_set_flags(prog, VX_PROGRAM_USE_M);
    vx_program_set_flags(prog, VX_PROGRAM_USE_CAM_POS);

    return prog;
}

static void style_destroy(vx_style_t * sty)
{
    free(sty->impl); // color4
    free(sty);
}

static void style_destroy_multi(vx_style_t * sty)
{
    vx_resc_dec_destroy((vx_resc_t*)sty->impl); // vx_resc
    free(sty);
}

static void style_destroy_solid(vx_style_t * sty)
{
    free(sty->impl); // color4
    free(sty);
}


static void style_destroy_solid_multi(vx_style_t * sty)
{
    vx_resc_dec_destroy((vx_resc_t*)sty->impl); // vx_resc
    free(sty);
}

static void style_destroy_fancy(vx_style_t * sty)
{
    fancy_t * data = sty->impl;
    free(data->color_ambient3);
    free(data->color_diffuse3);
    free(data->color_specular3);
    free(data);
    free(sty);
}

vx_style_t * vxo_mesh_style(const float * color4)
{
    float * color = calloc(4, sizeof(float));
    memcpy(color, color4, sizeof(float)*4);

    vx_style_t * style = calloc(1, sizeof(vx_style_t));
    style->impl = color;
    style->type = VXO_MESH_STYLE;
    style->requires_normals = 1;
    style->get_program = get_program;
    style->destroy =  style_destroy;

    return style;
}

vx_style_t * vxo_mesh_style_multi_colored(vx_resc_t * colors)
{
    vx_style_t * style = calloc(1, sizeof(vx_style_t));
    style->impl = colors;
    vx_resc_inc_ref(colors);

    style->type = VXO_MESH_STYLE;
    style->requires_normals = 1;
    style->get_program = get_program_multi;
    style->destroy =  style_destroy_multi;

    return style;
}

vx_style_t * vxo_mesh_style_solid(const float * color4)
{
    float * color = calloc(4, sizeof(float));
    memcpy(color, color4, sizeof(float)*4);

    vx_style_t * style = calloc(1, sizeof(vx_style_t));
    style->impl = color;
    style->type = VXO_MESH_STYLE;
    style->requires_normals = 1;
    style->get_program = get_program_solid;
    style->destroy =  style_destroy_solid;

    return style;
}

vx_style_t * vxo_mesh_style_solid_multi_colored(vx_resc_t * vr_colors)
{
    vx_style_t * style = calloc(1, sizeof(vx_style_t));
    style->impl = vr_colors;
    vx_resc_inc_ref(vr_colors);

    style->type = VXO_MESH_STYLE;
    style->requires_normals = 1;
    style->get_program = get_program_solid_multi;
    style->destroy =  style_destroy_solid_multi;

    return style;
}


vx_style_t * vxo_mesh_style_fancy(const float * color_ambient3, const float * color_diffuse3, const float * color_specular3,
                                  float transparency, float specularity, int type)
{
    fancy_t * data = calloc(1,sizeof(fancy_t));
    data->color_ambient3 = calloc(1, sizeof(float)*3);
    data->color_diffuse3 = calloc(1, sizeof(float)*3);
    data->color_specular3 = calloc(1, sizeof(float)*3);

    switch(type) {
        case 2:
            memcpy(data->color_specular3, color_specular3, sizeof(float)*3);
            //nobreak intentional fallthrough
        case 1:
            memcpy(data->color_diffuse3, color_diffuse3, sizeof(float)*3);
            //nobreak intentional fallthrough
        case 0:
            memcpy(data->color_ambient3, color_ambient3, sizeof(float)*3);
            break;
        default:
            assert(0);
    }
    data->transparency = transparency;
    data->specularity = specularity;
    data->type = type;

    vx_style_t * style = calloc(1, sizeof(vx_style_t));
    style->impl = data;
    style->type = VXO_MESH_STYLE;
    style->requires_normals = 1;
    style->get_program = get_program_fancy;
    style->destroy =  style_destroy_fancy;

    return style;
}

vx_object_t * vxo_mesh(vx_resc_t * vertex_data, int npoints,  vx_resc_t * normals,
                       int type, vx_style_t * style)
{
    vx_style_inc_ref(style);

    vx_object_t * vc = vxo_chain_create();

    if (npoints == 0) {
        vx_style_dec_destroy(style);
        return vc;
    }

     int pdim = vertex_data->count / npoints; // 2D or 3D?
     int ndim = -1;
     if (normals != NULL) {
         ndim = normals->count / npoints;
         assert(ndim == 3); // normals should always be 3D
     }

     // each style needs to know which program to load, and should also
     // bind any style data on creation
     vx_program_t * prog = style->get_program(style);
     vx_program_set_vertex_attrib(prog, "position", vertex_data, pdim);
     if (normals != NULL) {
         vx_program_set_vertex_attrib(prog, "normal", normals, ndim);
     }
     vx_program_set_draw_array(prog, npoints, type);

     vxo_chain_add(vc, prog->super);

     vx_style_dec_destroy(style);
     return vc;
}

vx_object_t * vxo_mesh_indexed(vx_resc_t * vertex_data, int npoints, vx_resc_t * normals,
                               vx_resc_t * indices, int type, vx_style_t * style)
{
    vx_style_inc_ref(style);
    vx_object_t * vc = vxo_chain_create();

    if (npoints == 0) {
        vx_style_dec_destroy(style);
        return vc;
    }

     int pdim = vertex_data->count / npoints; // 2D or 3D?
     int ndim = -1;
     if (normals != NULL) {
         ndim = normals->count / npoints;
         assert(ndim == 3); // normals should always be 3D
     }

     // each style needs to know which program to load, and should also
     // bind any style data on creation
     vx_program_t * prog = style->get_program(style);
     vx_program_set_vertex_attrib(prog, "position", vertex_data, pdim);
     if (normals != NULL) {
         vx_program_set_vertex_attrib(prog, "normal", normals, ndim);
     }
     vx_program_set_element_array(prog, indices, type);

     vxo_chain_add(vc, prog->super);

     vx_style_dec_destroy(style);
     return vc;
}

