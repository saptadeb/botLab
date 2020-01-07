#include "vx_resc_manager.h"
#include "vx_resc.h"
#include "vx_code_input_stream.h"
#include "vx_code_output_stream.h"
#include "vx_codes.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

struct vx_resc_manager
{
    vx_display_t *disp;

    // quasi-reference counting system to track how many users there are of
    // each resource
    zhash_t * allLiveSets; // map< int=worldID, map< char*=buffer_name, map<long=guid, vx_resc_t>>>
    zhash_t * remoteResc; // map <long=guid, vx_resc_t>
};


vx_resc_manager_t * vx_resc_manager_create(vx_display_t * disp)
{
    vx_resc_manager_t * mgr = calloc(1, sizeof(vx_resc_manager_t));
    mgr->disp = disp;
    mgr->allLiveSets = zhash_create(sizeof(uint32_t), sizeof(zhash_t*), zhash_uint32_hash, zhash_uint32_equals);
    mgr->remoteResc = zhash_create(sizeof(uint64_t), sizeof(vx_resc_t*), zhash_uint64_hash, zhash_uint64_equals);

    return mgr;
}

static void buffer_map_destroy(zhash_t * resc_map)
{
    zhash_vmap_keys(resc_map, free);
    zhash_vmap_values(resc_map, zhash_destroy);
    zhash_destroy(resc_map);
}


void vx_resc_manager_destroy(vx_resc_manager_t * mgr)
{
    // XXX should we issue all remaining OP_DEALLOC_RESOURCES at this point?

    zhash_vmap_values(mgr->allLiveSets, buffer_map_destroy);
    zhash_destroy(mgr->allLiveSets);

    zhash_destroy(mgr->remoteResc);
    free(mgr);
}

// Remove all elements from A which appear in B
static void removeAll(zhash_t * A, zhash_t  * B)
{
    zhash_iterator_t itr;
    zhash_iterator_init(A, &itr);
    uint64_t id = -1;
    void * value;
    while(zhash_iterator_next(&itr, &id, &value)) {
        if (zhash_contains(B, &id))
            zhash_iterator_remove(&itr);
    }
}


// Add all elements in B to A
static void addAll(zhash_t * A, zhash_t  * B)
{
    zhash_iterator_t itr;
    zhash_iterator_init(B, &itr);
    uint64_t id = -1;
    void * value;
    while(zhash_iterator_next(&itr, &id, &value)) {
        if (!zhash_contains(A, &id))
            zhash_put(A, &id, &value, NULL, NULL);
    }
}

// Pass in a list of resources which are new to some world. Increment user counts, and return a map
// of guids which actually need to be transmitted (i.e. that are new)
// Caller is responsible for freeing the map
zhash_t * vx_resc_manager_dedup_resources(vx_resc_manager_t * mgr, zhash_t * resources)
{
    zhash_t * transmit = zhash_copy(resources);
    removeAll(transmit, mgr->remoteResc); // remove existing
    addAll(mgr->remoteResc, transmit); // update remote, assuming transmission will be successful

    return transmit;
}

static void print_manager(vx_resc_manager_t * mgr)
{

    printf(" #mgr has  %d worlds:\n", zhash_size(mgr->allLiveSets));

    zhash_iterator_t world_itr;
    zhash_iterator_init(mgr->allLiveSets, &world_itr);
    uint32_t worldId = 0;
    zhash_t * buffer_map = NULL;
    while (zhash_iterator_next(&world_itr, &worldId, &buffer_map)) {
        zhash_iterator_t buffer_itr;
        zhash_iterator_init(buffer_map, &buffer_itr);
        printf("  >world %d contains %d buffers:\n", worldId, zhash_size(buffer_map));

        char * buffer_name = NULL;
        zhash_t * res_map = NULL;
        while(zhash_iterator_next(&buffer_itr, &buffer_name, &res_map)) {
            zhash_iterator_t res_itr;
            zhash_iterator_init(res_map, &res_itr);
            printf("  >>buffer %s (%d): ", buffer_name, zhash_size(res_map));

            uint64_t vrid = 0;
            vx_resc_t * vr = NULL;
            while (zhash_iterator_next(&res_itr, &vrid, &vr)) {
                printf("%"PRIu64" ", vrid);
            }
            printf("\n");
        }
    }

    printf(" #aggregate (%d): ", zhash_size(mgr->remoteResc));
    zhash_iterator_t res_itr;
    zhash_iterator_init(mgr->remoteResc, &res_itr);
    uint64_t vrid = 0;
    vx_resc_t * vr = NULL;
    while (zhash_iterator_next(&res_itr, &vrid, &vr)) {
        printf("%"PRIu64" ", vrid);
    }
    printf("\n");
}

// Pass in a codes describing which resources are no longer in use. Decrement user counts,
// and return a list of all resources whos counts have reached zero, which therefore
// should be deleted from the display using a OP_DEALLOC_RESOURCES opcode
void vx_resc_manager_buffer_resources(vx_resc_manager_t * mgr, const uint8_t * data, int datalen)
{
    if (0) print_manager(mgr);

    vx_code_input_stream_t * cins = vx_code_input_stream_create(data, datalen);
    int code = cins->read_uint32(cins);
    assert(code == OP_BUFFER_RESOURCES);
    int worldID = cins->read_uint32(cins);
    char * name = strdup(cins->read_str(cins)); //freed when cur_resources is eventually removed from the buffer map
    int count = cins->read_uint32(cins);

    zhash_t * cur_resources = zhash_create(sizeof(uint64_t), sizeof(vx_resc_t*), zhash_uint64_hash, zhash_uint64_equals);
    vx_resc_t * vr = NULL;
    for (int i = 0; i < count; i++) {
        uint64_t id = cins->read_uint64(cins);
        zhash_put(cur_resources, &id, &vr, NULL, NULL);
    }
    assert(cins->pos == cins->len); // we've emptied the stream
    vx_code_input_stream_destroy(cins);

    // 1 Update our records
    zhash_t * worldBuffers = NULL;
    zhash_get(mgr->allLiveSets, &worldID, &worldBuffers);
    if (worldBuffers == NULL) {
        worldBuffers = zhash_create(sizeof(char*), sizeof(zhash_t*), zhash_str_hash, zhash_str_equals);
        zhash_put(mgr->allLiveSets, &worldID, &worldBuffers, NULL, NULL);
    }

    zhash_t * old_resources = NULL;
    char * old_name = NULL;
    zhash_put(worldBuffers, &name, &cur_resources, &old_name, &old_resources);
    free(old_name);

    // 2 Figure out which resources have become unused:
    if(old_resources != NULL) {
        removeAll(old_resources, cur_resources);

        zarray_t * dealloc = zarray_create(sizeof(uint64_t));

        // now 'old_resources' contains only the resources that are no longer referenced
        // iterate through each one, and see if there is a buffer somewhere that references it
        zhash_iterator_t prev_itr;
        zhash_iterator_init(old_resources, &prev_itr);
        uint64_t id = -1;
        vx_resc_t * vr = NULL;
        while(zhash_iterator_next(&prev_itr, &id, &vr)) {
            // Check all worlds
            zhash_iterator_t  world_itr;// gives us all worlds
            zhash_iterator_init(mgr->allLiveSets, &world_itr);
            uint32_t wIDl = -1;
            zhash_t * buffer_map = NULL;
            while(zhash_iterator_next(&world_itr, &wIDl, &buffer_map)) {
                zhash_iterator_t buffer_itr; // gives us all buffers
                zhash_iterator_init(buffer_map, &buffer_itr);
                char * bName = NULL;
                zhash_t * resc_map = NULL;
                while(zhash_iterator_next(&buffer_itr, &bName, &resc_map)) {
                    if (zhash_contains(resc_map, &id)) {
                        goto continue_outer_loop;
                    }
                }

            }

            // If none of the worlds have this resource, we need to flag removal
            zarray_add(dealloc, &id);

          continue_outer_loop:
            ;
        }


        // 3 Issue dealloc commands
        if (zarray_size(dealloc) > 0) {
            vx_code_output_stream_t * couts = vx_code_output_stream_create(512);
            couts->write_uint32(couts, OP_DEALLOC_RESOURCES);
            couts->write_uint32(couts, zarray_size(dealloc));
            for (int i = 0; i < zarray_size(dealloc); i++) {
                uint64_t id = 0;
                zarray_get(dealloc, i, &id);
                couts->write_uint64(couts, id);
            }

            if (0) {
                printf("CMD dealloc %d ids: ", zarray_size(dealloc));
                for (int i = 0; i < zarray_size(dealloc); i++) {
                    uint64_t id = 0;
                    zarray_get(dealloc, i, &id);
                    printf("%"PRIu64",", id);
                }
                printf("\n");
            }

            mgr->disp->send_codes(mgr->disp, couts->data, couts->pos);

            vx_code_output_stream_destroy(couts);

            // Also remove the resources we deallocated from remoteResc
            for (int i = 0; i < zarray_size(dealloc); i++) {
                uint64_t id = 0;
                zarray_get(dealloc, i, &id);

                assert(zhash_contains(mgr->remoteResc, &id));
                zhash_remove(mgr->remoteResc, &id, NULL, NULL);
                assert(!zhash_contains(mgr->remoteResc, &id));
            }

        }
        zarray_destroy(dealloc);
        zhash_destroy(old_resources);

    }
    if (0) {
        print_manager(mgr);
        printf("\n\n");
    }
}
