#include "vxo_depth_test.h"
#include "vx_codes.h"
#include <stdlib.h>

// Basically a copy of vxo_chain

typedef struct vxo_depth_test vxo_depth_test_t;
struct vxo_depth_test
{
    vx_object_t * super;
    int enable;
    vx_object_t * obj;
};

static void vxo_depth_test_append(vx_object_t * obj, zhash_t * resources, vx_code_output_stream_t * codes)
{
    vxo_depth_test_t * vdepth = obj->impl;

    codes->write_uint32(codes, OP_DEPTH_PUSH);

    codes->write_uint32(codes, vdepth->enable ? OP_DEPTH_ENABLE : OP_DEPTH_DISABLE);

    // recurse
    vdepth->obj->append(vdepth->obj, resources, codes);

    codes->write_uint32(codes, OP_DEPTH_POP);
}

static void vxo_depth_test_destroy(vx_object_t * vo)
{
    vxo_depth_test_t * vdepth = vo->impl;

    vx_object_dec_destroy(vdepth->obj);

    free(vo);
    free(vdepth);
}

vx_object_t * vxo_depth_test(int enable, vx_object_t * obj)
{
    vxo_depth_test_t * vdepth = calloc(1, sizeof(vxo_depth_test_t));
    vdepth->super = calloc(1, sizeof(vx_object_t));
    vdepth->super->impl = vdepth;
    vdepth->super->destroy = vxo_depth_test_destroy;
    vdepth->super->append = vxo_depth_test_append;

    // data
    vdepth->enable = enable;
    vdepth->obj = obj;

    vx_object_inc_ref(vdepth->obj);

    return vdepth->super;
}
