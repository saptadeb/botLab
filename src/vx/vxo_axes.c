#include <math.h>

#include "vxo_axes.h"

#include "vxo_mesh.h"
#include "vxo_arrow.h"
#include "vxo_chain.h"
#include "vxo_mat.h"

#include "vx_colors.h"


vx_object_t * vxo_axes_styled(vx_style_t * x_mesh_style, vx_style_t * y_mesh_style, vx_style_t * z_mesh_style)
{

    return vxo_chain(vxo_arrow(x_mesh_style), // X
                     vxo_mat_rotate_z(M_PI/2),
                     vxo_arrow(y_mesh_style), // Y
                     vxo_mat_rotate_y(-M_PI/2),
                     vxo_arrow(z_mesh_style)); // Z
}

vx_object_t * vxo_axes(void)
{
    return vxo_axes_styled(vxo_mesh_style(vx_red), vxo_mesh_style(vx_green), vxo_mesh_style(vx_blue));
}



