#include "vxo_chain.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include "vx_codes.h"
#include "common/zarray.h"

static void vxo_chain_append (vx_object_t * obj, zhash_t * resources, vx_code_output_stream_t * codes)
{
    codes->write_uint32(codes, OP_MODEL_PUSH);
    vxo_chain_t *vc = (vxo_chain_t *) obj;
    for (int i = 0; i < zarray_size(vc->objs); i++) {
        vx_object_t * vo = NULL;
        zarray_get(vc->objs, i, &vo);
        vo->append(vo, resources, codes);
    }
    codes->write_uint32(codes, OP_MODEL_POP);
}

static void vxo_chain_destroy(vx_object_t * vo)
{
    vxo_chain_t * vc = (vxo_chain_t*) vo;
    if (vc && vc->objs) {
        zarray_vmap(vc->objs, vx_object_dec_destroy);
        zarray_destroy(vc->objs);
    }
    free(vo);
    //free(vc); // vo and vc are the same pointer
}

static void vxo_chain_add_real(vxo_chain_t * vc, vx_object_t * first, va_list va)
{
    for (vx_object_t * vo = first; vo != NULL; vo = va_arg(va, vx_object_t *)) {
        assert(vo != NULL);
        vx_object_inc_ref(vo);
        zarray_add(vc->objs, &vo);
    }
}

vx_object_t * vxo_chain_create()
{
    vxo_chain_t * vc = calloc(1, sizeof(vxo_chain_t));
    vc->vxo.destroy = vxo_chain_destroy;
    vc->vxo.append = vxo_chain_append;
    vc->objs = zarray_create(sizeof(vx_object_t*));
    return (vx_object_t*) vc;
}

vx_object_t * _vxo_chain_create_varargs_private(vx_object_t * first, ...) {
    vxo_chain_t *vc  = (vxo_chain_t*) vxo_chain_create();
    va_list va;
    va_start(va, first);
    vxo_chain_add_real(vc, first, va);
    va_end(va);
    return (vx_object_t*) vc;
}


void _vxo_chain_add_varargs_private(vx_object_t * vo, vx_object_t * first, ...)
{
    va_list va;
    va_start(va, first);
    vxo_chain_add_real((vxo_chain_t*)vo, first, va);
    va_end(va);
}

