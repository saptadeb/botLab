#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#include "vx_tcp_util.h"
#include "vx_resc.h"

static int verbose = 0;

// Free memory for this vr, and for the wrapped data:
static void vx_resc_destroy_managed(vx_resc_t * r)
{
    free(r->res);
    free(r);
}

zhash_t * vx_tcp_util_unpack_resources(vx_code_input_stream_t * cins)
{
    zhash_t * resources = zhash_create(sizeof(uint64_t),sizeof(vx_resc_t*), zhash_uint64_hash, zhash_uint64_equals);

    int ct = cins->read_uint32(cins);
    if (verbose) printf("process_resources count %d\n", ct);

    for (int i = 0; i < ct; i++) {

        // manually create a resource, by unserializing the network data
        // each resource starts with a ref count of 0
        vx_resc_t * vr = calloc(1, sizeof(vx_resc_t));
        vr->id = cins->read_uint64(cins);
        vr->type = cins->read_uint32(cins);
        vr->count = cins->read_uint32(cins);
        vr->fieldwidth = cins->read_uint32(cins);
        if (verbose) printf("  id %"PRIu64" type %d count %d fieldwidth %d\n", vr->id, vr->type, vr->count, vr->fieldwidth);


        vr->res = calloc(vr->count,  vr->fieldwidth);
        vr->destroy = vx_resc_destroy_managed; // set the destructor

        zhash_put(resources, &vr->id, &vr, NULL, NULL);

        const uint8_t * data =  cins->read_bytes(cins, vr->count*vr->fieldwidth);
        memcpy(vr->res, data, vr->count*vr->fieldwidth);

        // now need to manually change the endianness of the data:
        uint8_t * res_end =  ((uint8_t*)vr->res) + vr->count*vr->fieldwidth;

        // declare outside switch:
        uint32_t h_val32 = 0;
        uint64_t h_val64 = 0;
        for (uint8_t * res_ptr = vr->res; res_ptr < res_end; res_ptr += vr->fieldwidth) {
            switch(vr->fieldwidth) {
                case 4:
                    h_val32 =be32toh(*(uint32_t*)res_ptr);
                    *(uint32_t*)res_ptr = h_val32;
                    break;
                case 8:
                    h_val64 = be64toh(*(uint64_t*)res_ptr);
                    *(uint64_t*)res_ptr = h_val64;
                    break;
                case 1:
                    break;
                default:
                    assert(1); // other sizes not implemented
            }
        }
        if (verbose) printf("  done\n");

    }
    return resources;
}

void vx_tcp_util_pack_resources(zhash_t * resources, vx_code_output_stream_t * couts)
{

    zhash_iterator_t itr;
    zhash_iterator_init(resources, &itr);
    uint64_t id = -1;
    vx_resc_t *vr = NULL;

    couts->write_uint32(couts, zhash_size(resources));

    while(zhash_iterator_next(&itr, &id, &vr)) {
        couts->write_uint64(couts, vr->id);
        couts->write_uint32(couts, vr->type);
        couts->write_uint32(couts, vr->count);
        couts->write_uint32(couts, vr->fieldwidth);

        uint8_t * res_end =  ((uint8_t*)vr->res) + vr->count*vr->fieldwidth;
        for (uint8_t * res_ptr = vr->res; res_ptr < res_end; res_ptr += vr->fieldwidth) {
            switch(vr->fieldwidth) {
                case 1:
                    couts->write_uint8(couts, *res_ptr);
                    break;
                case 4:
                    couts->write_uint32(couts, * ((uint32_t*)res_ptr));
                    break;
                case 8:
                    couts->write_uint64(couts, * ((uint64_t*)res_ptr));
                    break;
                default:
                    assert(1); // other sizes not implemented
            }
        }
    }
}



void vx_tcp_util_pack_camera_pos(const vx_camera_pos_t * pos, vx_code_output_stream_t * couts)
{
    for (int i = 0; i < 3; i ++)
        couts->write_double(couts, pos->eye[i]);

    for (int i = 0; i < 3; i ++)
        couts->write_double(couts, pos->lookat[i]);

    for (int i = 0; i < 3; i ++)
        couts->write_double(couts, pos->up[i]);

    for (int i = 0; i < 4; i ++)
        couts->write_uint32(couts, pos->viewport[i]);

    couts->write_double(couts, pos->perspectiveness);
    couts->write_double(couts, pos->perspective_fovy_degrees);
    couts->write_double(couts, pos->zclip_near);
    couts->write_double(couts, pos->zclip_far);
}



void vx_tcp_util_unpack_camera_pos(vx_code_input_stream_t * ins, vx_camera_pos_t * pos)
{
    pos->eye[0] = ins->read_double(ins);
    pos->eye[1] = ins->read_double(ins);
    pos->eye[2] = ins->read_double(ins);

    pos->lookat[0] = ins->read_double(ins);
    pos->lookat[1] = ins->read_double(ins);
    pos->lookat[2] = ins->read_double(ins);

    pos->up[0] = ins->read_double(ins);
    pos->up[1] = ins->read_double(ins);
    pos->up[2] = ins->read_double(ins);

    pos->viewport[0] = ins->read_uint32(ins);
    pos->viewport[1] = ins->read_uint32(ins);
    pos->viewport[2] = ins->read_uint32(ins);
    pos->viewport[3] = ins->read_uint32(ins);

    pos->perspectiveness = ins->read_double(ins);
    pos->perspective_fovy_degrees = ins->read_double(ins);
    pos->zclip_near = ins->read_double(ins);
    pos->zclip_far = ins->read_double(ins);
}
