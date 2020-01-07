#ifndef VXO_MESH_H
#define VXO_MESH_H

#include "vx_object.h"
#include "vx_resc.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VXO_MESH_LIGHT_COUNT 2

#define VXO_MESH_STYLE 0xaf3254ab

// Basic, per vertex, lighting for meshes shading -- uses normals for diffuse shading
// only. Uses hard coded lighting positions and colors, etc
vx_style_t * vxo_mesh_style(const float * color4);

// Basic, per vertex, lighting for meshes shading -- uses normals for diffuse shading
// only. Uses hard coded lighting positions but per-vertex colors
vx_style_t * vxo_mesh_style_multi_colored(vx_resc_t * colors);

// Disable lighting entirely.
vx_style_t * vxo_mesh_style_solid(const float * color4);

// Disable lighting entirely.
vx_style_t * vxo_mesh_style_solid_multi_colored(vx_resc_t * colors);


// Phong shading, including material properties
// NOTE: these colors are dim=3, since the transparency is specified per
// material (not per color). You can still pass dim=4, but the last
// element will be ignored
// @opacity       -- 0.0 transparent, 1.0 opaque
// @specularity   -- higher range means specularities are more localized
// @type          -- 0 = solid,
//                   1 = diffuse
//                   2 = phong
// Note: generally, it's more efficient to use the appropriate style
// than to change type to 0 or 1
vx_style_t * vxo_mesh_style_fancy(const float * color_ambient3, const float * color_diffuse3,
                                  const float * color_specular3,
                                  float opacity, float specularity, int type);


// XXX Should add a mesh shader that uses lighting
//     would require extracting normal matrix from the pm stack in vx_gl_renderer, binding to uniform

// Legal values for 'type' in GLES are GL_TRIANGLES/GL_TRIANGLE_STRIP/GL_TRIANGLE_FAN
// note, normals can be NULL if the style does not require them
vx_object_t * vxo_mesh(vx_resc_t * vertices, int npoints,  vx_resc_t * normals,
                       int type, vx_style_t * style);

// Use indexed rendering via glDrawElements. Allows reusing vertex data
vx_object_t * vxo_mesh_indexed(vx_resc_t * vertices, int npoints, vx_resc_t * normals,
                               vx_resc_t * indices, int type, vx_style_t * style);

//Allow access to built in lights. Also allows to update them
// see VXO_MESH_LIGHT_COUNT
// Also note, these are not (currently) thread safe, in that if a vxo_style is
// created concurrently, the light position for that style will be
// undefined. You should ensure externally that no one else in your
// process is using them when you change them.
void vxo_mesh_get_light(int light_idx, float * light_xyz_out);
void vxo_mesh_set_light(int light_idx, const float * light_xyz);

#ifdef __cplusplus
}
#endif

#endif
