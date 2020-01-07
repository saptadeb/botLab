#include <stdarg.h>
#include <math.h>

#include "vxo_arrow.h"


#include "vxo_lines.h"
#include "vxo_mesh.h"
#include "vxo_chain.h"
#include "vxo_box.h"
#include "vxo_square_pyramid.h"
#include "vxo_mat.h"


// Returns a unit arrow
vx_object_t * _vxo_arrow_private(vx_style_t * style, ...)
{

    vx_object_t * vc = vxo_chain_create();
    va_list va;
    va_start(va, style);
    for (vx_style_t * sty = style; sty != NULL; sty = va_arg(va, vx_style_t *)) {

        vx_style_inc_ref(sty);

        vxo_chain_add(vc,
                      vxo_chain(
                          vxo_mat_scale3(.75, .06, .06),
                          vxo_mat_translate3(.5,0,0),
                                vxo_box(sty)),
                      vxo_chain(vxo_mat_translate3(0.75,0,0),
                                vxo_mat_rotate_y(M_PI/2),
                                vxo_mat_scale(.25),
                                vxo_square_pyramid(sty)));


        vx_style_dec_destroy(sty);
    }
    va_end(va);


    return vc;
}
