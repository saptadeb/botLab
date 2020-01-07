#ifndef VX_STYLE_H
#define VX_STYLE_H

#include "vx_program.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vx_style vx_style_t;

// Styles provide a powerful, simple way to specify the appearance of vxo_* objects
// Multiple styles can be passed to an object to specify how the outline should be stroked,
// how the interior should be filled (and lit), etc.
// The main styles are vxo_points_style, vxo_lines_style and vxo_mesh_style, which indicated
// that points, lines, and mesh (filled) styling should be drawn for the specified shape
//
// Similarly to vx_object, and vx_resc, the vx_style is reference
// counted, starting with an initial reference count of 0. For correct
// management, you should pass the style to a vxo_object which will
// handle memory management for you. If you need to hang onto a style
// for longer, you should use the inc_ref and dec_destroy functions to
// maintain your own reference lifetime
//
// The reference count semantics for most user-written functions that
// accept a style as an argument should be to remain reference-count
// neutral. That is, inc_ref() and dec_destroy() calls should balance
// each other.
// Furthermore, functions which accept a style should be written so that
// a zero-reference count style can be passed, which would then be
// destroyed when the last dec_destroy() is called.
//
// CORRECT example:
// static void my_function(vx_style_t * style)
// {
//     vx_style_inc_ref(style);
//     vxo_chain_add(vc, vxo_box(style));
//     vxo_chain_add(vc, vxo_sphere(style));
//     vx_style_dec_destroy(style);
// }
//
// INCORRECT example:
// static void my_bad_function(vx_style_t * style)
// {
//     vx_style_inc_ref(style);
//     vxo_chain_add(vc, vxo_box(style));
//     vx_style_dec_destroy(style);
//     // XXX Reference count could reach zero here, and style could be destroyed
//     vx_style_inc_ref(style); // Segfault possible
//     vxo_chain_add(vc, vxo_sphere(style));
//     vx_style_dec_destroy(style);
// }
//
// Each style generally maps directly to a shader pair, which by
// convention should have a 'position' attribute where the vertex
// data can be bound by the vxo_ object

struct vx_style
{
    int type; // { VX_LINE_STYLE, VX_POINT_STYLE, ...}
    int requires_normals; // some vertex-data-dependent styles require normals to support proper lighting
    void * impl;

    // returns the appropriate program for this appearance style, with
    // all appearance data already bound to the appropriate uniform/ vertex
    // attributes
    vx_program_t * (*get_program)(vx_style_t * sty);

    // Reference counting:
    uint32_t rcts; // how many places have a reference to this?
    void (*destroy)(vx_style_t * sty);
};


static inline void vx_style_dec_destroy(vx_style_t * obj)
{
    assert(obj->rcts > 0);
    obj->rcts--;
    if (obj->rcts == 0) {
        // Implementer must guarantee to release all resources, including
        // decrementing any reference counts it may have been holding
        obj->destroy(obj);
    }
}

static inline void vx_style_inc_ref(vx_style_t * obj)
{
    obj->rcts++;
}

// It may eventually be possible to remove the vx_style type, and have vxo_points_style simply return a vx_program_t
// Also, we might pass the number of elements to get_program() to allow checking that the style dimension matches
// the vertex dimension.

#ifdef __cplusplus
}
#endif

#endif
