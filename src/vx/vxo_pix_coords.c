#include "vxo_pix_coords.h"

#include <stdlib.h>

#include "vx_codes.h"

typedef struct
{
    vx_object_t vxo; // inherit

    int origin_code;
    vx_object_t * obj;

} pix_coord_t;


static void vxo_pix_coords_append (vx_object_t * obj, zhash_t * resources, vx_code_output_stream_t * codes)
{
    pix_coord_t * pixc = (pix_coord_t*) obj;

    codes->write_uint32(codes, OP_PROJ_PUSH);
    codes->write_uint32(codes, OP_PROJ_PIXCOORDS);
    codes->write_uint32(codes, pixc->origin_code);

    pixc->obj->append(pixc->obj, resources, codes);

    codes->write_uint32(codes, OP_PROJ_POP);
}



static void vxo_pix_coords_destroy(vx_object_t * vo)
{
    pix_coord_t * pixc = (pix_coord_t*) vo;

    vx_object_dec_destroy(pixc->obj);
    free(vo);
}

vx_object_t * vxo_pix_coords(int origin_code, vx_object_t * obj)
{

    pix_coord_t * pixc = calloc(1,sizeof(pix_coord_t));
    pixc->origin_code = origin_code;
    pixc->obj = obj;
    vx_object_inc_ref(pixc->obj);

    pixc->vxo.destroy = vxo_pix_coords_destroy;
    pixc->vxo.append = vxo_pix_coords_append;
    // Note: refcnt is implicitly set to 0

    return (vx_object_t*) pixc;
}
