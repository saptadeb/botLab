#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>
#include "vx_world.h"

#include "common/zarray.h"

#include "vx_resc.h"
#include "vx_codes.h"

// The most important job the world does is to coordinate buffer swapping.
// On a buffer swap, we must carefully serialize all objects in the 'front'
// and transmit them, ensuring that the correct resources will be available.
// Each world includes a thread which does the heavy lifting for the serialization
// this allows the swap() call to return instantly to the user.
// We must also ensure that each buffer is serialized to new clients
// Right now we only provide the guarantee that all buffers will
// eventually get serialized to a new listener -- it may not happen
// before add_listener() returns.
//
// In the current implementation, there is a single serialization
// thread, which pauses completely when a new client connects (to
// serialize all old buffers) before continuing to process new swap()
// operations.  Theoretically, this could be divided into N+1 threads,
// with one thread per listener and a centralized thread to codify all
// the vx objects. However, this makes it more difficult to handle
// back-pressure from a remote listener though, so for now we aren't
// going to do it that way

struct vx_world
{
    int worldID;
    zhash_t * buffer_map; // <char*, vx_buffer_t>

    pthread_mutex_t buffer_mutex; // this mutex must be locked for buffer_map & listeners

    zarray_t * listeners; // <vx_world_listener_t>
    pthread_mutex_t listener_mutex;

    // notify here when new work is available,
    // protect access to the queues below:
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;

    // append a buffer name if it needs to be re-serialized
    zarray_t * buffer_queue; // < char*>

    // Append a listener here if all buffers need to be flushed
    zarray_t * listener_queue; // <vx_world_listener_t>


    pthread_t process_thread;
    int process_running;
};


struct vx_buffer
{
    char * name;
    vx_world_t * world;
    int draw_order;

    // Protected by the mutex
    zarray_t * back_objs; // stores vx_object_t until swap() is called
    zarray_t * pending_objs; // stores objs until serialization occurs
    zarray_t * front_objs; // stores objs currently being drawn

    pthread_mutex_t mutex; // lock to add objects to back

    // cache the list of resources currently in use by this buffer
    // can only be accessed by the serialization thread.
    zhash_t * front_resc; // <guid, vx_resc_t *>
    vx_code_output_stream_t * front_codes;

};

static int atomicWorldID = 1; // XXXX need to actually make these atomic

static int verbose = 0;

// Pull swap operations off the stack, and process them.

// forward declaration
static void delayed_swap(vx_buffer_t * buffer);
static vx_code_output_stream_t * make_buffer_resource_codes(vx_buffer_t * buffer, zhash_t * resources);


static void * run_process(void * data)
{
    vx_world_t * world = data;


    while (world->process_running) {
        vx_world_listener_t * listener = NULL;
        char * buffer_name = NULL;

        // 1) Wait until there's data
        pthread_mutex_lock(&world->queue_mutex);
        while (zarray_size(world->buffer_queue) == 0 && zarray_size(world->listener_queue) == 0 && world->process_running) {
            pthread_cond_wait(&world->queue_cond, &world->queue_mutex);
        }

        if (!world->process_running) { // XXX cleaning out the queue?
            pthread_mutex_unlock(&world->queue_mutex);
            break;
        }

        // Processing new listeners takes priority
        if ( zarray_size(world->listener_queue) > 0) {
            zarray_get(world->listener_queue, 0, &listener);
            zarray_remove_index(world->listener_queue, 0, 0);
        } else {
            assert(zarray_size(world->buffer_queue) > 0);
            zarray_get(world->buffer_queue, 0, &buffer_name);
            zarray_remove_index(world->buffer_queue, 0, 0);
        }
        pthread_mutex_unlock(&world->queue_mutex);

        // Operation A: New listener
        if (listener != NULL) {
            // re-transmit each buffer that has already been serialized
            pthread_mutex_lock(&world->buffer_mutex);
            zhash_iterator_t itr;
            zhash_iterator_init(world->buffer_map, &itr);
            char * name = NULL;
            vx_buffer_t * buffer = NULL;
            while(zhash_iterator_next(&itr, &name, &buffer)) {
                if (buffer->front_codes->pos != 0) {
                    vx_code_output_stream_t * bresc_codes = make_buffer_resource_codes(buffer, buffer->front_resc);

                    // Doing a swap in 3 steps (instead of 4) like this
                    // is only safe if the listener is brand new, which we are
                    // guaranteed is the case in this chunk of code
                    listener->send_codes(listener, bresc_codes->data, bresc_codes->pos);
                    listener->send_resources(listener, buffer->front_resc);
                    listener->send_codes(listener, buffer->front_codes->data, buffer->front_codes->pos);

                    vx_code_output_stream_destroy(bresc_codes);
                }
            }

            pthread_mutex_unlock(&world->buffer_mutex);
        }

        // Operation B: buffer swap
        if (buffer_name != NULL) {
            vx_buffer_t * buffer = NULL;
            pthread_mutex_lock(&world->buffer_mutex);
            zhash_get(world->buffer_map, &buffer_name, &buffer);
            pthread_mutex_unlock(&world->buffer_mutex);

            delayed_swap(buffer);
        }

    }

    pthread_exit(NULL);
}

vx_world_t * vx_world_create()
{
    vx_world_t *world = malloc(sizeof(vx_world_t));
    world->worldID = __sync_fetch_and_add(&atomicWorldID, 1);
    world->buffer_map = zhash_create(sizeof(char*), sizeof(vx_buffer_t*), zhash_str_hash, zhash_str_equals);
    world->listeners = zarray_create(sizeof(vx_world_listener_t*));
    pthread_mutex_init(&world->buffer_mutex, NULL);
    pthread_mutex_init(&world->listener_mutex, NULL);

    pthread_mutex_init(&world->queue_mutex, NULL);
    pthread_cond_init(&world->queue_cond, NULL);

    world->listener_queue = zarray_create(sizeof(vx_world_listener_t*));
    world->buffer_queue = zarray_create(sizeof(char*));

    world->process_running = 1;
    pthread_create(&world->process_thread, NULL, run_process, world);
    return world;
}

static void vx_world_buffer_destroy(vx_buffer_t * buffer)
{
    vx_world_t * vw = buffer->world;

    pthread_mutex_lock(&vw->buffer_mutex);

    free(buffer->name);
    zarray_vmap(buffer->back_objs, vx_object_dec_destroy);
    zarray_destroy(buffer->back_objs);

    zarray_vmap(buffer->pending_objs, vx_object_dec_destroy);
    zarray_destroy(buffer->pending_objs);

    zarray_vmap(buffer->front_objs, vx_object_dec_destroy);
    zarray_destroy(buffer->front_objs);

    zhash_vmap_values(buffer->front_resc, vx_resc_dec_destroy);
    zhash_destroy(buffer->front_resc);

    vx_code_output_stream_destroy(buffer->front_codes);

    pthread_mutex_destroy(&buffer->mutex);
    free(buffer);

    pthread_mutex_unlock(&vw->buffer_mutex);
}

void vx_world_destroy(vx_world_t * world)
{
    zhash_vmap_values(world->buffer_map, vx_world_buffer_destroy); // keys are stored in buffer struct
    zhash_destroy(world->buffer_map);
    assert(zarray_size(world->listeners) == 0 && "Destroy layers referencing worlds before worlds"); // we can't release these resources properly
    zarray_destroy(world->listeners);

    pthread_mutex_destroy(&world->buffer_mutex);
    pthread_mutex_destroy(&world->listener_mutex);

    // Tell the processing thread to quit
    world->process_running = 0;
    pthread_mutex_lock(&world->queue_mutex);
    pthread_cond_signal(&world->queue_cond);
    pthread_mutex_unlock(&world->queue_mutex);

    pthread_join(world->process_thread, NULL);

    pthread_mutex_destroy(&world->queue_mutex);
    pthread_cond_destroy(&world->queue_cond);

    // These are pointers to data stored elsewhere, just delete the
    // data structure
    zarray_destroy(world->listener_queue);
    zarray_destroy(world->buffer_queue);

    free(world);
}

int vx_world_get_id(vx_world_t * world)
{
    // Don't need to sync

    return world->worldID;
}

zarray_t *vx_world_get_buffer_list(vx_world_t * world)
{
    zarray_t *buffers = zarray_create(sizeof(char*));

    pthread_mutex_lock(&world->buffer_mutex);
    {
        zarray_t *keys = zhash_keys(world->buffer_map);

        for (int i = 0; i < zarray_size(keys); i++) {

            char *key = NULL;
            zarray_get(keys, i, &key);

            char *copy = strdup(key);
            zarray_add(buffers, &copy);
        }

        zarray_destroy(keys);
    }
    pthread_mutex_unlock(&world->buffer_mutex);

    return buffers;
}

vx_buffer_t * vx_world_get_buffer(vx_world_t * world, const char * name)
{
    vx_buffer_t * buffer = NULL;

    pthread_mutex_lock(&world->buffer_mutex);

    zhash_get(world->buffer_map, &name, &buffer);
    if (buffer == NULL) {
        buffer = calloc(1, sizeof(vx_buffer_t));

        buffer->name = strdup(name);
        buffer->world = world;
        buffer->draw_order = 0;

        buffer->back_objs = zarray_create(sizeof(vx_object_t*));
        buffer->pending_objs = zarray_create(sizeof(vx_object_t*));
        buffer->front_objs = zarray_create(sizeof(vx_object_t*));

        buffer->front_resc = zhash_create(sizeof(uint64_t), sizeof(vx_resc_t*), zhash_uint64_hash, zhash_uint64_equals);
        buffer->front_codes = vx_code_output_stream_create(128);

        pthread_mutex_init(&buffer->mutex, NULL);

        vx_buffer_t * oldBuffer= NULL;
        zhash_put(buffer->world->buffer_map, &buffer->name, &buffer, NULL, &oldBuffer);
        assert(oldBuffer == NULL);
    }

    pthread_mutex_unlock(&world->buffer_mutex);

    return buffer;
}

void vx_buffer_set_draw_order(vx_buffer_t * buffer, int draw_order)
{
    pthread_mutex_lock(&buffer->mutex);

    // XXX Should introduce a new OP code for setting the draw order
    //  this should trigger some opcodes to get sent
    buffer->draw_order = draw_order;

    pthread_mutex_unlock(&buffer->mutex);
}

void vx_buffer_add_back(vx_buffer_t * buffer, vx_object_t * obj)
{
    pthread_mutex_lock(&buffer->mutex);

    zarray_add(buffer->back_objs, &obj);
    vx_object_inc_ref(obj); // *+*+ increment obj references

    pthread_mutex_unlock(&buffer->mutex);
}

void vx_world_add_listener(vx_world_t * world, vx_world_listener_t * listener)
{
    // Add the listener so future buffer swaps will be registered
    pthread_mutex_lock(&world->listener_mutex);
    {
        zarray_add(world->listeners, &listener);
    }
    pthread_mutex_unlock(&world->listener_mutex);

    // Flag re-transmission of all buffers to this listener
    pthread_mutex_lock(&world->queue_mutex);
    {
        zarray_add(world->listener_queue, &listener);
        pthread_cond_signal(&world->queue_cond);
    }
    pthread_mutex_unlock(&world->queue_mutex);

}

void vx_world_remove_listener(vx_world_t * world, vx_world_listener_t * listener)
{
    pthread_mutex_lock(&world->listener_mutex);
    {
        zarray_remove_value(world->listeners, &listener, 0);
    }
    pthread_mutex_unlock(&world->listener_mutex);
}

static void notify_listeners_send_resources(vx_world_t * world, zhash_t * new_resources)
{
    pthread_mutex_lock(&world->listener_mutex);
    for (int i = 0; i < zarray_size(world->listeners); i++) {
        vx_world_listener_t * listener = NULL;
        zarray_get(world->listeners, i, &listener);
        listener->send_resources(listener, new_resources);
    }
    pthread_mutex_unlock(&world->listener_mutex);
}

static void notify_listeners_codes(vx_world_t * world, vx_code_output_stream_t * couts)
{
    pthread_mutex_lock(&world->listener_mutex);
    for (int i = 0; i < zarray_size(world->listeners); i++) {
        vx_world_listener_t * listener = NULL;
        zarray_get(world->listeners, i, &listener);
        listener->send_codes(listener, couts->data, couts->pos);
    }
    pthread_mutex_unlock(&world->listener_mutex);
}

static vx_code_output_stream_t * make_buffer_resource_codes(vx_buffer_t * buffer, zhash_t * resources)
{
    vx_code_output_stream_t * couts = vx_code_output_stream_create(256);
    couts->write_uint32(couts, OP_BUFFER_RESOURCES); // tell the display which resources are currently in use
    couts->write_uint32(couts, buffer->world->worldID);
    couts->write_str(couts, buffer->name);
    couts->write_uint32(couts, zhash_size(resources));

    zhash_iterator_t itr;
    zhash_iterator_init(resources, &itr);

    uint64_t guid = -1;
    vx_resc_t *resc = NULL;
    while (zhash_iterator_next(&itr, &guid, &resc))
        couts->write_uint64(couts, guid);
    return couts;
}

static void send_buffer_resource_codes(vx_buffer_t * buffer, zhash_t * resources)
{
    vx_code_output_stream_t * couts = make_buffer_resource_codes(buffer, resources);
    notify_listeners_codes(buffer->world, couts);

    vx_code_output_stream_destroy(couts);
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

// Do a quick swap:
void vx_buffer_swap(vx_buffer_t * buffer)
{
    pthread_mutex_lock(&buffer->mutex);
    {
        // It's possible that the serialization is running behind,
        // and may not have already serialized the pending_objs
        // In that case, they are discarded here, momentarily blocking
        // the calling thread.
        if (zarray_size(buffer->pending_objs) > 0) {
            // *+*+ corresponding decrement (if falling behind)
            zarray_vmap(buffer->pending_objs, vx_object_dec_destroy);
            zarray_clear(buffer->pending_objs);
            static int once = 1;
            if (once) {
                once = 0;
                printf("NFO: World serialization fell behind\n");
            }
        }

        // swap
        zarray_t * tmp = buffer->pending_objs;
        buffer->pending_objs = buffer->back_objs;
        buffer->back_objs = tmp;
    }
    pthread_mutex_unlock(&buffer->mutex);

    // Now flag a swap on the serialization thread
    pthread_mutex_lock(&buffer->world->queue_mutex);
    {
        // Ensure that the string is already in the queue, or add it if
        // it isn't. Duplicates are prohibited
        int index = -1;
        for (int i = 0, sz = zarray_size(buffer->world->buffer_queue); i < sz; i++) {
            char * test = NULL;
            zarray_get(buffer->world->buffer_queue, i, &test);

            if (strcmp(test, buffer->name) == 0) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            zarray_add(buffer->world->buffer_queue, &buffer->name);
            pthread_cond_signal(&buffer->world->queue_cond);
        }
    }
    pthread_mutex_unlock(&buffer->world->queue_mutex);

}

static void _print_id(vx_resc_t * vr)
{
    printf("%"PRIu64" ",vr->id);
}

// Call this only from the serialization thread
//
static void delayed_swap(vx_buffer_t * buffer)
{
    if (!buffer->front_objs)
        return; // buffer has not yet finished initialization
    if (verbose) printf("DBG: swap %s\n", buffer->name);

    pthread_mutex_lock(&buffer->mutex);
    {
        // clear existing front
        buffer->front_codes->pos = 0; // reset

        // *+*+ corresponding decrement (if keeping up)
        zarray_vmap(buffer->front_objs, vx_object_dec_destroy);
        zarray_clear(buffer->front_objs);


        // swap out the pending objects without tying up this mutex for too long
        zarray_t * tmp = buffer->front_objs;
        buffer->front_objs = buffer->pending_objs;
        buffer->pending_objs = tmp; // empty
    }
    pthread_mutex_unlock(&buffer->mutex);

    vx_world_t * world = buffer->world;
    zhash_t * old_resources = buffer->front_resc;

    // Serialize each object into resources and opcodes
    vx_code_output_stream_t * codes = buffer->front_codes;

    codes->write_uint32(codes, OP_BUFFER_CODES);
    codes->write_uint32(codes, world->worldID);
    codes->write_str(codes, buffer->name);
    codes->write_uint32(codes, buffer->draw_order); // XXX We should move this op code elsewhere

    zhash_t * resources = zhash_create(sizeof(uint64_t),sizeof(vx_resc_t*), zhash_uint64_hash, zhash_uint64_equals);

    for (int i = 0; i < zarray_size(buffer->front_objs); i++) {
        vx_object_t * obj = NULL;
        zarray_get(buffer->front_objs, i, &obj);
        obj->append(obj, resources, codes);
    }

    // *&&* we will hold on to these until next swap() call
    zhash_vmap_values(resources, vx_resc_inc_ref);

    zhash_t * all_resources = zhash_copy(resources);
    addAll(all_resources, old_resources);

    // Claim use of the union of the last frame, and the current frame
    // this avoids a race condition where another buffer (maybe in another world)
    // could force deallocation of some resources before the new render codes arrive
    if (verbose) printf("DBG: claim union via codes\n");
    if (verbose > 1)  {
        printf("  claimed %d IDS ", zhash_size(all_resources));
        zhash_vmap_values(all_resources, _print_id);
        printf("\n");
    }
    send_buffer_resource_codes(buffer, all_resources);

    if (verbose) printf("DBG: Send actual resources\n");
    if (verbose > 1)  {
        printf("  send %d IDS ", zhash_size(all_resources));
        zhash_vmap_values(all_resources, _print_id);
        printf("\n");
    }
    notify_listeners_send_resources(world, all_resources);

    if (verbose) printf("DBG: send render codes\n");
    notify_listeners_codes(world, codes);

    // Claim the actual set of resources which are in use now, which may result in some
    // resources being deleted from the gl context
    if (verbose) printf("DBG: reduce claim to actual in use\n");
    if (verbose > 1)  {
        printf("  actual %d IDS ", zhash_size(resources));
        zhash_vmap_values(resources, _print_id);
        printf("\n\n");
    }
    send_buffer_resource_codes(buffer, resources);



    buffer->front_resc = resources; // set of current resources

    zhash_destroy(all_resources);

    // *&&* corresponding decrement of resources
    zhash_vmap_values(old_resources, vx_resc_dec_destroy);
    zhash_destroy(old_resources);

}
