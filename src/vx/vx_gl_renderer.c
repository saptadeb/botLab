#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h> // for PRIu64

#include "vx_gl_renderer.h"
#include "vx_tcp_util.h"
#include "vx_resc.h"
#include "vx_codes.h"
#include "vx_matrix_stack.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>
#include <GL/gl.h>

#include "common/zhash.h"
#include "common/zarray.h"
#include "vx/math/matd.h"
#include "vx_util.h"

//state
struct vx_gl_renderer
{
    int change_since_last_render;

    zhash_t * resource_map; // holds vx_resc_t
    zhash_t * program_map; // holds gl_prog_resc_t
    zarray_t * dealloc_ids; // resources that need to be deleted

    // holds info about vbo and textures which have been allocated with GL
    zhash_t * vbo_map;
    zhash_t * texture_map;

    zhash_t * layer_map; // <int, vx_layer_info_t>
    zhash_t * world_map; // <int, vx_world_info_t>

    int verbose;
    int resc_verbose;
};

// Resource management for gl program and associated shaders:
// When the associated guid for the vertex shader is dealloacted (vx_resc_t)
// then we also need to free the associated GL resources listed in this struct
// (Note that the vx_resc_t corresponding to the fragment shader also needs to be deallocated
// but that the gl_prog_resc_t is indexed in the hashmap by the
// vertex_shader guid, so this struct (and associated GL objects) are
// only freed when the vertex shader is deallocated)
typedef struct gl_prog_resc gl_prog_resc_t;
struct gl_prog_resc {
    GLuint prog_id; // program id
    GLuint vert_id; // associated vertex_shader
    GLuint frag_id; // associated fragment shader
};


typedef struct vx_buffer_info vx_buffer_info_t;
struct vx_buffer_info {
    char * name; // used as key in the hash map
    int draw_order;
    vx_code_input_stream_t * codes;
    int enabled;
};


typedef struct vx_layer_info vx_layer_info_t;
struct vx_layer_info {
    // set with layer info
    int layer_id;
    int world_id;
    int draw_order;
    float bg_color[4];

    // set before each render
    int viewport[4];
    float layer_pm[16]; // proj + camera matrix
    float eye3[3];
    //float layer_model[16]; // not really model matrix -- actually camera location


    zhash_t * enabled_buffers; // <char*, uint8_t>
};

typedef struct vx_world_info vx_world_info_t;
struct vx_world_info {
    int world_id;
    zhash_t * buffer_map; // holds vx_buffer_info_t
};

vx_gl_renderer_t * vx_gl_renderer_create()
{
    vx_gl_renderer_t * state = calloc(1,sizeof(vx_gl_renderer_t));

    state->resource_map = zhash_create(sizeof(uint64_t), sizeof(vx_resc_t*), zhash_uint64_hash, zhash_uint64_equals);

    state->program_map = zhash_create(sizeof(uint64_t), sizeof(gl_prog_resc_t*), zhash_uint64_hash, zhash_uint64_equals);
    state->vbo_map = zhash_create(sizeof(uint64_t), sizeof(uint32_t), zhash_uint64_hash,zhash_uint64_equals);
    state->texture_map = zhash_create(sizeof(uint64_t), sizeof(uint32_t),zhash_uint64_hash,zhash_uint64_equals);

    state->dealloc_ids = zarray_create(sizeof(uint64_t));

    state->world_map = zhash_create(sizeof(uint32_t), sizeof(vx_world_info_t*), zhash_uint32_hash, zhash_uint32_equals);
    state->layer_map = zhash_create(sizeof(uint32_t), sizeof(vx_layer_info_t*), zhash_uint32_hash, zhash_uint32_equals);

    // load environment debugging variables

    const char * vx_verbose = getenv("VX_GL_VERBOSE");
    if (vx_verbose != NULL)
        state->verbose = atoi(vx_verbose); // returns 0 on error

    const char * vx_resc_verbose = getenv("VX_GL_RESC_VERBOSE");
    if (vx_resc_verbose != NULL)
        state->resc_verbose = atoi(vx_resc_verbose); // returns 0 on error

    return state;
}

int vx_gl_renderer_changed_since_last_render(vx_gl_renderer_t * rend)
{
    return rend->change_since_last_render;
}

static void vx_buffer_info_destroy(vx_buffer_info_t * binfo)
{
    free(binfo->name);
    vx_code_input_stream_destroy(binfo->codes);
    free(binfo);
}

static void vx_world_info_destroy(vx_world_info_t * winfo)
{
    // keys are also in struct, so no need to free
    zhash_vmap_values(winfo->buffer_map, vx_buffer_info_destroy);
    zhash_destroy(winfo->buffer_map);
    free(winfo);
}

static int buffer_compare(const void * _a, const void * _b)
{
    vx_buffer_info_t *a = *((vx_buffer_info_t**) _a);
    vx_buffer_info_t *b = *((vx_buffer_info_t**) _b);
    return a->draw_order - b->draw_order;
}

static int layer_info_compare(const void *_a, const void *_b)
{
    vx_layer_info_t *a = *((vx_layer_info_t**) _a);
    vx_layer_info_t *b = *((vx_layer_info_t**) _b);

    //printf("%d %d\n", a->draw_order, b->draw_order);
    return a->draw_order - b->draw_order;
}


static void process_deallocations(vx_gl_renderer_t * state)
{
    if (state->verbose) printf("Dealloc %d ids:\n   ", zarray_size(state->dealloc_ids));

    for (int i =0; i < zarray_size(state->dealloc_ids); i++) {
        uint64_t guid = 0;
        zarray_get(state->dealloc_ids, i, &guid);

        if (state->verbose) printf("%"PRIu64",",guid);

        vx_resc_t * vr = NULL;
        zhash_remove(state->resource_map, &guid, NULL, &vr);
        if (vr == NULL) {
            printf("GUID %"PRIu64" deletion error\n", guid);
        }
        assert(vr != NULL);

       // There may also be a program, or a vbo or texture for each guid
        GLuint vbo_id = 0;
        if (zhash_remove(state->vbo_map, &guid, NULL, &vbo_id)) {
            // Tell open GL to deallocate this VBO
            glDeleteBuffers(1, &vbo_id);
            if (state->verbose > 1) printf(" Deleted VBO %d \n", vbo_id);
        }

        // There is always a resource for each guid.
        if (vr != NULL) {
            assert(guid == vr->id);
            if (state->verbose > 1) printf("Attempting deallocating resource GUID=%"PRIu64"\n", vr->id);
            vx_resc_dec_destroy(vr);
        } else {
            printf("WRN!: Invalid request. Resource %"PRIu64" does not exist", guid);
        }


        GLuint tex_id = 0;
        if (zhash_remove(state->texture_map, &guid, NULL, &tex_id)) {
            // Tell open GL to deallocate this texture
            glDeleteTextures(1, &tex_id);
            if (state->verbose > 1) printf(" Deleted TEX %d \n", tex_id);
        }

        gl_prog_resc_t * prog = NULL;
        zhash_remove(state->program_map, &guid, NULL, &prog);
        if (prog) {
            glDetachShader(prog->prog_id,prog->vert_id);
            glDeleteShader(prog->vert_id);
            glDetachShader(prog->prog_id,prog->frag_id);
            glDeleteShader(prog->frag_id);
            glDeleteProgram(prog->prog_id);

            if (state->verbose > 1) printf("  Freed program %d vert %d and frag %d\n",
                                prog->prog_id, prog->vert_id, prog->frag_id);
            free(prog);
        }
    }

    zarray_clear(state->dealloc_ids);
    if (state->verbose) printf("\n");
}

void vx_gl_renderer_set_layer_render_details(vx_gl_renderer_t *state, int layer_id, const int * viewport4,
                                             const float * pm16, const float * eye3)
{
    vx_layer_info_t * layer = NULL;
    zhash_get(state->layer_map, &layer_id, &layer);
    if (layer == NULL) {
        printf("WRN: Layer %d not found when attempting to set pm matrix\n", layer_id);
        return;
    }

    for (int i = 0; i < 16; i++) {
        if (layer->layer_pm[i] != pm16[i]) {
            state->change_since_last_render = 1;
            break;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (layer->viewport[i] != viewport4[i]) {
            state->change_since_last_render = 1;
            break;
        }
    }

    memcpy(layer->viewport, viewport4, 4*sizeof(int));
    memcpy(layer->layer_pm, pm16, 16*sizeof(float));
    memcpy(layer->eye3, eye3, 3*sizeof(float));
}

void vx_gl_renderer_update_layer(vx_gl_renderer_t * state, vx_code_input_stream_t * cins)
{
    state->change_since_last_render = 1; // lazy, not data dependent

    uint32_t layer_id = cins->read_uint32(cins);
    uint32_t world_id = cins->read_uint32(cins);
    uint32_t draw_order = cins->read_uint32(cins);
    float red = cins->read_float(cins);
    float green = cins->read_float(cins);
    float blue = cins->read_float(cins);
    float alpha = cins->read_float(cins);

    vx_layer_info_t * layer = NULL;
    zhash_get(state->layer_map, &layer_id, &layer);
    if (layer == NULL) { // Allocate a new layer
        layer = calloc(1,sizeof(vx_layer_info_t));
        layer->layer_id = layer_id;
        layer->world_id = world_id;
        layer->enabled_buffers = zhash_create(sizeof(char*), sizeof(uint8_t), zhash_str_hash, zhash_str_equals);

        if (state->verbose) printf("Initializing layer %d\n", layer_id);
        // initialize layer to arbitrary 640x480
        layer->viewport[0] = layer->viewport[1] = 0;
        layer->viewport[2] = 640;
        layer->viewport[3] = 480;

        // Initialize projection, model matrix to the identity
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                layer->layer_pm[i*4 + j] = (i == j ? 1.0f : 0.0f);

        vx_layer_info_t * old_value = NULL;
        zhash_put(state->layer_map, &layer_id, &layer, NULL, &old_value);
        assert(old_value == NULL);
    }
    // changing worlds is not allowed, for now
    assert(layer_id == layer->layer_id);
    assert(world_id == layer->world_id);

    // dynamic content: order and viewport
    layer->draw_order = draw_order;

    layer->bg_color[0] = red;
    layer->bg_color[1] = green;
    layer->bg_color[2] = blue;
    layer->bg_color[3] = alpha;
}


// get (or create if they don't exist) the world and buffer info structs for a named buffer
static void get_world_buffer_infos(vx_gl_renderer_t * state, int world_id, const char * name,
                                   vx_world_info_t ** out_world, vx_buffer_info_t ** out_buffer)
{
    vx_world_info_t * world = NULL;
    zhash_get(state->world_map, &world_id, &world);
    if (world == NULL) { // Allocate a new world
        world = calloc(1,sizeof(vx_world_info_t));
        world->world_id = world_id;
        world->buffer_map = zhash_create(sizeof(char*), sizeof(vx_buffer_info_t*), zhash_str_hash, zhash_str_equals);

        vx_world_info_t * old_value = NULL;
        zhash_put(state->world_map, &world_id, &world, NULL, &old_value);
        assert(old_value == NULL);
    }

    vx_buffer_info_t * buf = NULL;
    zhash_get(world->buffer_map, &name, &buf);
    if (buf == NULL) {
        buf = calloc(1,sizeof(vx_buffer_info_t));
        buf->name = strdup(name);
        zhash_put(world->buffer_map, &buf->name, &buf, NULL, NULL);
        buf->enabled = 1; // default new buffers to be enabled
    }

    *out_world = world;
    *out_buffer = buf;

}

void vx_gl_renderer_buffer_enabled(vx_gl_renderer_t * state, vx_code_input_stream_t * cins)
{

    int layer_id = cins->read_uint32(cins);
    const char * name = cins->read_str(cins);
    char * alloc_name = strdup(name);
    uint8_t enabled = cins->read_uint8(cins);

    vx_layer_info_t * linfo = NULL;
    zhash_get(state->layer_map, &layer_id, &linfo);


    uint8_t old_enabled = 1;
    char * old_buffer_name = NULL;
    zhash_put(linfo->enabled_buffers, &alloc_name, &enabled, &old_buffer_name, &old_enabled);
    // XXX zhash_put scribbles on return value when empty

   if (old_enabled != enabled) {
        state->change_since_last_render = 1;
    }

    free(old_buffer_name); // could be NULL but that's ok
}

void vx_gl_renderer_set_buffer_render_codes(vx_gl_renderer_t * state, vx_code_input_stream_t * cins)
{
    state->change_since_last_render = 1; // lazy, not data dependent

    int world_id = cins->read_uint32(cins);
    const char * name = cins->read_str(cins);
    int draw_order = cins->read_uint32(cins);
    int clen = vx_code_input_stream_available(cins); // length of buffer/program codes

    vx_world_info_t * world = NULL;
    vx_buffer_info_t * buffer = NULL;
    get_world_buffer_infos(state, world_id, name, &world, &buffer);

    // XXX Should this meta data end up in some other packet?
    buffer->draw_order = draw_order;

    if (buffer->codes != NULL) {
        vx_code_input_stream_destroy(buffer->codes);
        buffer->codes = NULL;
    }

    buffer->codes = vx_code_input_stream_create(cins->data +cins->pos, clen);

    if (state->verbose) printf("Updating codes buffer: world ID %d %s codes->len %d codes->pos %d\n", world_id, buffer->name, buffer->codes->len, buffer->codes->pos);

}


void vx_gl_renderer_remove_resources(vx_gl_renderer_t *state, vx_code_input_stream_t * cins)
{
    // Add the resources, flag them for deletion later
    // XXX We don't currently handle duplicates already in the list.

    int count = cins->read_uint32(cins);

    if (state->resc_verbose) printf("Marking for deletion %d ids:\n", count);

    for (int i =0; i < count; i++) {
        uint64_t id = cins->read_uint64(cins);

        if (!zarray_contains(state->dealloc_ids, &id)) {
            zarray_add(state->dealloc_ids, &id);
            if (state->resc_verbose) printf("%"PRIu64",", id);
        } else {
            assert(0); // Generally, shouldn't be asking twice to
            // delete a resource
            if (state->resc_verbose) printf("skip%"PRIu64",", id);
        }
    }
    if (state->resc_verbose) printf("\n");
}

// Accepts reference counted resources
void vx_gl_renderer_add_resources(vx_gl_renderer_t * state, zhash_t * resources)
{
    if (state->resc_verbose) printf("Updating %d resources:   ", zhash_size(resources));

    state->change_since_last_render = 1; // lazy, not data dependent

    zhash_iterator_t itr;
    zhash_iterator_init(resources, &itr);
    uint64_t id = -1;
    vx_resc_t * vr = NULL;
    while(zhash_iterator_next(&itr, &id, &vr)) {
        vx_resc_t * old_vr = NULL;
        vx_resc_inc_ref(vr); // signal we will hang onto a reference

        zhash_put(state->resource_map, &vr->id, &vr, NULL, &old_vr);

        if (state->resc_verbose) printf("%"PRIu64"[%d],", id, vr->count*vr->fieldwidth);

        if (old_vr != NULL) {
            // Check to see if this was previously flagged for deletion.
            // If so, unmark for deletion

            int found_idx = -1;
            int found = 0;
            for (int i = 0; i < zarray_size(state->dealloc_ids); i++) {
                uint64_t del_guid = -1;
                zarray_get(state->dealloc_ids, i, &del_guid);
                if (del_guid == vr->id) {
                    found_idx = i;
                    found++;
                }
            }

            if (found == 0)
                printf("WRN: ID collision, 0x%"PRIx64" resource already exists\n", vr->id);

            zarray_remove_index(state->dealloc_ids, found_idx, 0);
            assert(found <= 1);


            vx_resc_dec_destroy(old_vr);
        }
    }
    if (state->resc_verbose) printf("\n");
}

static void vx_layer_info_destroy(vx_layer_info_t * linfo)
{
    zhash_vmap_keys(linfo->enabled_buffers, free); // full strings are stored
    zhash_destroy(linfo->enabled_buffers);
    free(linfo);
}

// must be called on gl thread
void vx_gl_renderer_destroy(vx_gl_renderer_t * state)
{
    // Make sure all resources are listed in the dealloc_ids array:
    {
        zhash_iterator_t itr;
        zhash_iterator_init(state->resource_map, &itr);
        uint64_t id = -1;
        vx_resc_t *vr = NULL;
        if (state->verbose) printf("Processing %d exiting resources on exit\n", zhash_size(state->resource_map));
        while(zhash_iterator_next(&itr, &id, &vr)){
            if (!zarray_contains(state->dealloc_ids, &id)) {
                zarray_add(state->dealloc_ids, &id);
            }
        }
    }
    if (state->verbose) printf("vx_local_renderer: %d resources to be deallocated on exit\n", zarray_size(state->dealloc_ids));
    process_deallocations(state); // remove all the resources, and associated GL stuff

    //there should be nothing left in 'program_map' 'resource_map' 'vbo_map' and 'texture_map':
    assert(zhash_size(state->resource_map) == 0);
    assert(zhash_size(state->program_map) == 0);
    assert(zhash_size(state->vbo_map) == 0);
    assert(zhash_size(state->texture_map) == 0);

    zhash_destroy(state->resource_map);
    zhash_destroy(state->program_map);
    zarray_destroy(state->dealloc_ids);

    zhash_destroy(state->vbo_map);
    zhash_destroy(state->texture_map);

    zhash_vmap_values(state->layer_map, vx_layer_info_destroy);
    zhash_vmap_values(state->world_map, vx_world_info_destroy);

    zhash_destroy(state->layer_map);
    zhash_destroy(state->world_map);
    free(state);
}

static void print_hex(const uint8_t * buf, int buflen)
{
    for (int i = 0; i < buflen; i++) {
        printf("%02x ", buf[i]);

        if ((i + 1) % 16 == 0)
            printf("\n");
        else if ((i + 1) % 8 == 0)
            printf(" ");
    }
    printf("\n");
}

// print shader line by line to get around android 1024 character line buf
static void print_shader(const char * shader_source)
{
    const char * start = shader_source;
    const char * end = NULL;

    while ( (end = strchr(start, '\n')) != NULL) {
        int len = end - start;
        printf("%.*s\n", len, start);

        start = end + 1; // skip new line
    }
}

// Allocates new VBO, and stores in hash table, results in a bound VBO
static GLuint vbo_allocate(vx_gl_renderer_t * state, GLenum target, vx_resc_t *vr)
{
    GLuint vbo_id;
    glGenBuffers(1, &vbo_id);
    glBindBuffer(target, vbo_id);
    glBufferData(target, vr->count * vr->fieldwidth, vr->res, GL_STATIC_DRAW);
    if (state->verbose) printf("      Allocated VBO %d for guid %"PRIu64" of size %d\n",
                        vbo_id, vr->id, vr->count);

    zhash_put(state->vbo_map, &vr->id, &vbo_id, NULL, NULL);

    return vbo_id;
}

static int validate_program(GLint prog_id, char * stage_description)
{
    char output[65535];

    GLint len = 0;
    glGetProgramInfoLog(prog_id, 65535, &len, output);
    if (len != 0)
        printf("%s len = %d:\n%s\n", stage_description, len, output);
    return !len;
}

static int validate_shader(GLint prog_id, char * stage_description)
{
    char output[65535];

    GLint len = 0;
    glGetShaderInfoLog(prog_id, 65535, &len, output);
    if (len != 0)
        printf("%s len = %d:\n%s\n", stage_description, len, output);
    return !len;
}

static void transpose44(float mat44[])
{
    float tmp44[16];
    memcpy(tmp44, mat44, 16*sizeof(float));

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            mat44[i*4+j] = tmp44[j*4 +i];
        }
}

static void transpose33(float mat33[])
{
    float tmp33[9];
    memcpy(tmp33, mat33, 9*sizeof(float));

    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            mat33[i*3+j] = tmp33[j*3 +i];
        }
}

// A = A * B
static void multEqf44(float A44[], float B44[])
{
    float tmp44[16];
    memcpy(tmp44, A44, 16*sizeof(float));
    memset(A44, 0, 16*sizeof(float));

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                A44[i*4 + j] += tmp44[i*4 + k] * B44[k*4 + j];
            }
        }

}

static void multEq44(double A44[], double B44[])
{
    double tmp44[16];
    memcpy(tmp44, A44, 16*sizeof(double));
    memset(A44, 0, 16*sizeof(double));

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                A44[i*4 + j] += tmp44[i*4 + k] * B44[k*4 + j];
            }
        }
}

static void print44(double * M)
{
    for (int  r = 0; r <4; r++){
        printf("   ");
        for (int c = 0; c < 4; c++) {
            printf("%5.3f ", M[r*4+c]);
        }
        printf("\n");
    }
}

// generate a projection matrix for pix coords
static void pix_coords_pm(int width, int height, int origin, double * M)
{
    memset(M, 0, 16*sizeof(double));
    vx_util_gl_ortho(0, width, 0, height, -1, 1, M);

    double tx = 0, ty = 0;

    switch (origin) {
        case VX_ORIGIN_BOTTOM_LEFT:
        case VX_ORIGIN_BOTTOM:
        case VX_ORIGIN_BOTTOM_RIGHT:
            ty = 0;
            break;

        case VX_ORIGIN_LEFT:
        case VX_ORIGIN_CENTER:
        case VX_ORIGIN_RIGHT:
            ty = height / 2;
            break;
        case VX_ORIGIN_CENTER_ROUND:
            ty = round(height/2);
            break;

        case VX_ORIGIN_TOP_LEFT:
        case VX_ORIGIN_TOP:
        case VX_ORIGIN_TOP_RIGHT:
            ty = height;
            break;
    }

    switch (origin) {
        case VX_ORIGIN_BOTTOM_LEFT:
        case VX_ORIGIN_TOP_LEFT:
        case VX_ORIGIN_LEFT:
            tx = 0;
            break;

        case VX_ORIGIN_BOTTOM:
        case VX_ORIGIN_CENTER:
        case VX_ORIGIN_TOP:
            tx = width / 2;
            break;

        case VX_ORIGIN_CENTER_ROUND:
            tx = round(width/2);
            break;

        case VX_ORIGIN_BOTTOM_RIGHT:
        case VX_ORIGIN_TOP_RIGHT:
        case VX_ORIGIN_RIGHT:
            tx = width;
            break;
    }

    // Translation matrix
    double T[16];
    memset(T, 0, 16*sizeof(double));
    T[0] = T[1*4 + 1] = T[2*4 + 2]  = T[3*4 + 3] = 1.0;
    T[0*4 + 3] = tx;
    T[1*4 + 3] = ty;

    multEq44(M, T);
}


// right multiply a vector and matrix, and provide the resulting
// vector in 'v3_out':
// v3_out = A33 * v3_in
/*
static void multVec33(float A33[], float v3_in[], float v3_out[])
{
    for (int i = 0; i < 3; i ++) {
        v3_out[i] = 0;
        for (int j = 0; j < 3; j++) {
            v3_out[i] += A33[i*3 + j] * v3_in[j];
        }
    }
}
*/

// This function does the heavy lifting for rendering a data + program pair
// Which program, and data to use are specified in the codes
// input_stream
// This breaks down into the following steps:
// 1) Find the glProgram. If it doesn't exist, create it from the associated
//    vertex and fragment shader string resources
// 2) Find all the VBOs that will bound as vertex attributes. If they
//    don't exist, create them from the specified resources
// 3) Read all the data to be bound as uniforms from the input stream.
// 4) Textures
// 5) Render using either glDrawArrays or glElementArray
//
// Note: After step 1 and 4, the state of the program is queried,
//       and debugging information (if an error occurs) is printed to stdout
static int render_program(vx_gl_renderer_t * state, vx_layer_info_t *layer,
                          vx_code_input_stream_t * codes,
                          char * dbg_buffer_name,
                          vx_matrix_stack_t * proj_stack,
                          const float * eye3,
                          vx_matrix_stack_t *model_stack)
{
    if (codes->pos >= codes->len) // exhausted the stream
        return 1;
    if (state->verbose) printf("  Processing program, codes has %d remaining\n",codes->len-codes->pos);

    if (state->verbose > 2) print_hex(codes->data + codes->pos, codes->len - codes->pos);

    // STEP 1: find/allocate the glProgram (using vertex shader and fragment shader)
    GLuint prog_id = 0;
    uint64_t vert_shad_id = 0;
    uint64_t frag_shad_id = 0;
    {
        uint64_t vertId = codes->read_uint64(codes);
        uint64_t fragId = codes->read_uint64(codes);

        vert_shad_id = vertId;
        frag_shad_id = fragId;

        // Programs can be found by the guid of the vertex shader
        gl_prog_resc_t * prog = NULL;
        zhash_get(state->program_map, &vertId, &prog);
        if (prog == NULL) {
            prog = calloc(1,sizeof(gl_prog_resc_t));
            // Allocate a program if we haven't made it yet
            prog->vert_id = glCreateShader(GL_VERTEX_SHADER);
            prog->frag_id = glCreateShader(GL_FRAGMENT_SHADER);

            vx_resc_t * vertResc = NULL;
            vx_resc_t * fragResc = NULL;

            zhash_get(state->resource_map, &vertId, &vertResc);
            zhash_get(state->resource_map, &fragId, &fragResc);

            if (state->verbose) {
                printf("   Vert id %"PRIu64" frag id %"PRIu64" \n", vertId, fragId);
            }

            assert(vertResc != NULL);
            assert(fragResc != NULL);

            const char * vertSource = vertResc->res;
            const char * fragSource = fragResc->res;

            if (state->verbose > 1) {
                printf("Vertex source len %zu:\n", strlen(vertSource));
                print_shader(vertSource);
                printf("Fragment source len %zu:\n", strlen(fragSource));
                print_shader(fragSource);
            }

            glShaderSource(prog->vert_id, 1, &vertSource, NULL);
            glShaderSource(prog->frag_id, 1, &fragSource, NULL);

            glCompileShader(prog->vert_id);
            glCompileShader(prog->frag_id);

            prog->prog_id = glCreateProgram();

            glAttachShader(prog->prog_id, prog->vert_id);
            glAttachShader(prog->prog_id, prog->frag_id);

            glLinkProgram(prog->prog_id);

            zhash_put(state->program_map, &vertId, &prog, NULL, NULL);

            if (state->verbose) printf("  Created gl program %d from guid %"PRIu64" and %"PRIu64" (gl ids %d and %d)\n",
                   prog->prog_id, vertId, fragId, prog->vert_id, prog->frag_id);
        }
        prog_id = prog->prog_id;
        glUseProgram(prog_id);
    }

    #define MAX_ATTRIB_COUNT 32
    uint32_t attribCount = 0;
    GLint attribLocs[MAX_ATTRIB_COUNT];

    uint32_t texCount = 0;

    while (codes->pos < codes->len) {
        uint32_t op = codes->read_uint32(codes);

        if (state->verbose > 1) printf("OP %d\n", op);

        if (op == 0) // end of program opcodes
            break;

        switch (op) {
            case OP_VALIDATE_PROGRAM: {
                gl_prog_resc_t * prog = NULL;
                zhash_get(state->program_map, &vert_shad_id, &prog);
                assert(prog != NULL);

                int success = 1;
                success &= validate_shader(prog->frag_id, "FRAG");
                success &= validate_shader(prog->vert_id, "VERT");
                success &= validate_program(prog_id, "Post-link");

                if (!success) {
                    vx_resc_t * vertResc = NULL;
                    vx_resc_t * fragResc = NULL;

                    zhash_get(state->resource_map, &vert_shad_id, &vertResc);
                    zhash_get(state->resource_map, &frag_shad_id, &fragResc);

                    assert(vertResc != NULL);
                    assert(fragResc != NULL);

                    printf("Vertex shader %"PRIu64" source:\n",
                           vert_shad_id);
                    print_shader((char*)vertResc->res);

                    printf("Fragment shader %"PRIu64" source:\n",
                           frag_shad_id);
                    print_shader((char*)fragResc->res);
                }
                break;
            }

            case OP_PM_MAT_NAME: {
                const char * pmName = codes->read_str(codes);

                float model[16];
                vx_matrix_stack_getf(model_stack, model);

                float PM[16];
                vx_matrix_stack_getf(proj_stack, PM);
                multEqf44(PM, model);

                GLint unif_loc = glGetUniformLocation(prog_id, pmName);
                if (state->verbose) printf("   uniform %s  loc %d\n", pmName, unif_loc);

                assert(unif_loc >= 0); // Ensure this field exists
                int transpose = 0;
                if (!transpose)
                    transpose44(PM);

                // GL ES 2 prohibits 'transpose = 1' here
                glUniformMatrix4fv(unif_loc, 1 , transpose, (GLfloat *)PM);

                break;
            }

            case OP_MODEL_MAT_NAME: {
                const char * modelName = codes->read_str(codes);

                float model[16];
                vx_matrix_stack_getf(model_stack, model);

                GLint unif_loc = glGetUniformLocation(prog_id, modelName);
                if (state->verbose) printf("   uniform %s  loc %d err %d\n", modelName, unif_loc, glGetError());


                //XXX assert(unif_loc >= 0); // Ensure this field exists
                int transpose = 0;
                if (!transpose)
                    transpose44(model);

                // GL ES 2 prohibits 'transpose = 1' here
                glUniformMatrix4fv(unif_loc, 1 , transpose, (GLfloat *)model);

                break;
            }

            case OP_NORMAL_MAT_NAME: {
                const char * normName = codes->read_str(codes);

                // compute transpose of inverse of model matrix
                // Note: this is usually just the model matrix, except
                // if any scaling has been done.
                matd_t * model44 = matd_create(4,4);
                vx_matrix_stack_get(model_stack, model44->data);

                matd_t * model33 = matd_select(model44, 0,2, 0,2);

                matd_t * normD = matd_op("(M^-1)'", model33);

                float normf[9];
                vx_util_copy_floats(normD->data, normf, 9);
                matd_destroy(model44);
                matd_destroy(model33);
                matd_destroy(normD);


                GLint unif_loc = glGetUniformLocation(prog_id, normName);
                if (state->verbose) printf("   uniform %s  loc %d\n", normName, unif_loc);

                // XXX assert(unif_loc >= 0); // Ensure this field exists
                int transpose = 0;
                if (!transpose)
                    transpose33(normf);

                if (state->verbose > 2) {
                    printf("NORMAL MATRIX:\n");
                    for (int i = 0; i < 3; i++) {
                        for (int j = 0; j <3; j++)
                            printf("%.3f ", normf[i*3+j]);
                        printf("\n");
                    }
                }

                // GL ES 2 prohibits 'transpose = 1' here
                glUniformMatrix3fv(unif_loc, 1 , transpose, (GLfloat *)normf);

                break;
            }

            case OP_CAM_POS_NAME: {
                const char * camName = codes->read_str(codes);

                GLint unif_loc = glGetUniformLocation(prog_id, camName);
                if (state->verbose) printf("   uniform %s  loc %d err %d cam_pos =%.2f %.2f %.2f\n",
                                         camName, unif_loc, glGetError(), eye3[0],eye3[1],eye3[2]);

                glUniform3fv(unif_loc, 1, (GLfloat *)eye3);
                break;
            }

            case OP_VERT_ATTRIB: {

                assert (attribCount < MAX_ATTRIB_COUNT);

                uint64_t attribId = codes->read_uint64(codes);
                uint32_t dim = codes->read_uint32(codes);
                const char * name = codes->read_str(codes); //Not a copy!


                // This should never fail!
                vx_resc_t * vr  = NULL;
                zhash_get(state->resource_map, &attribId, &vr);
                if (vr == NULL) {
                    printf("GUID %"PRIu64" not available yet!\n", attribId);
                }
                assert(vr != NULL);

                if (vr->type != GL_FLOAT) {
                    printf("ERR: Type on resource %"PRIu64" is wrong: %d. Expected %d. Buffer name %s \n", attribId, vr->type, GL_FLOAT, dbg_buffer_name);
                }
                assert(vr->type == GL_FLOAT);

                if (state->verbose) printf("   vertex attrib %s %"PRIu64" dim %d count %d\n", name, attribId, dim, vr->count);

                if (zhash_contains(state->vbo_map, &attribId)) {
                    GLuint vbo_id = 0;
                    zhash_get(state->vbo_map, &attribId, &vbo_id);
                    glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
                } else {
                    vbo_allocate(state, GL_ARRAY_BUFFER, vr);
                }

                if (state->verbose > 2) {
                    for (int i = 0; i < vr->count; i++) {
                        printf("%f,",((float*)vr->res)[i]);
                        if ((i + 1) % dim == 0)
                            printf("\n");
                    }
                }

                // Attach to attribute
                GLint attr_loc = glGetAttribLocation(prog_id, name);
                attribLocs[attribCount] = attr_loc;

                glEnableVertexAttribArray(attr_loc);
                glVertexAttribPointer(attr_loc, dim, vr->type, 0, 0, 0);

                assert(vr->type == GL_FLOAT);

                attribCount++;
                break;
            }

            case OP_UNIFORM_VECTOR_IV:
            case OP_UNIFORM_MATRIX_FV:
            case OP_UNIFORM_VECTOR_FV: {
                // Functionality common to all uniforms, regardless of type
                const char * name = codes->read_str(codes);
                GLint unif_loc = glGetUniformLocation(prog_id, name);
                // size of the data, measured in 32 bit words.
                uint32_t size = codes->read_uint32(codes);

                // The uniform data is stored at the end, so it can be copied
                // into statically allocated array
                int vals[size];
                for (int j = 0; j < size; j++)
                    vals[j] = codes->read_uint32(codes); //XXX bug??

                // XXX For floats, these are hardcoded to a specific length...
                if (op == OP_UNIFORM_MATRIX_FV) {
                    uint32_t transpose = 0; // codes->read_uint32(codes);
                    glUniformMatrix4fv(unif_loc, 1, transpose, (GLfloat *)vals);
                } else if (op == OP_UNIFORM_VECTOR_FV && size == 4) {
                    glUniform4fv(unif_loc, 1, (GLfloat *) vals);
                } else if (op == OP_UNIFORM_VECTOR_FV && size == 3) {
                    glUniform3fv(unif_loc, 1, (GLfloat *) vals);
                } else if (op == OP_UNIFORM_VECTOR_FV && size == 1) {
                    glUniform1fv(unif_loc, 1, (GLfloat *) vals);
                } else if (op == OP_UNIFORM_VECTOR_IV && size == 1) {
                    glUniform1iv(unif_loc, 1, vals);
                } else {
                    assert(0);
                }

                break;
            }

            case OP_TEXTURE: {
                const char * name = codes->read_str(codes);
                uint64_t texGuid = codes->read_uint64(codes);
                // This should never fail!
                vx_resc_t * vr  = NULL;
                zhash_get(state->resource_map, &texGuid, &vr);
                assert(vr != NULL);

                uint32_t width = codes->read_uint32(codes);
                uint32_t height = codes->read_uint32(codes);
                uint32_t format = codes->read_uint32(codes);
                uint32_t flags = codes->read_uint32(codes);

                if (zhash_contains(state->texture_map, &vr->id)) {
                    GLuint tex_id = 0;
                    zhash_get(state->texture_map, &vr->id, &tex_id);
                    glBindTexture(GL_TEXTURE_2D, tex_id);
                }
                else {
                    GLuint tex_id = 0;

                    glEnable(GL_TEXTURE_2D);
                    glGenTextures(1, &tex_id);

                    glBindTexture(GL_TEXTURE_2D, tex_id);

                    // Note: these are logically reversed from Vis to
                    // match OpenGL: min filter is when image is small,
                    // mag filter is when zoomed into the image

                    int min_filter = (flags & VX_TEX_MIN_FILTER) ? 1 : 0; // aka mipmap
                    int mag_filter = (flags & VX_TEX_MAG_FILTER) ? 1 : 0;
                    int repeat     = (flags & VX_TEX_REPEAT) ? 1 : 0;     // or clamp?


                    if (min_filter)
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                    else
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

                    if (mag_filter) {
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    } else {
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    }

                    if (repeat) {
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                    } else {
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    }

                    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, vr->res);

                    if (min_filter)
                        glGenerateMipmap(GL_TEXTURE_2D);

                    if (state->verbose) printf("Allocated TEX %d for guid %"PRIu64"\n", tex_id, vr->id);
                    zhash_put(state->texture_map, &vr->id, &tex_id, NULL, NULL);
                }

                int attrTexI = glGetUniformLocation(prog_id, name);
                glActiveTexture(GL_TEXTURE0 + texCount);
                glUniform1i(attrTexI, texCount); // Bind the uniform to TEXTUREi

                texCount++;
                break;
            }

            case OP_LINE_WIDTH: {
                float size = codes->read_float(codes);
                glLineWidth(size);
                glEnable(GL_PROGRAM_POINT_SIZE);
                int unifPtSz = glGetUniformLocation(prog_id, "pointSize");
                glUniform1f(unifPtSz, size); // Attempt to set the point size, harmless? if pointSize is not in program
                break;
            }

            case OP_DRAW_ARRAY: {
                uint32_t drawCount = codes->read_uint32(codes);
                uint32_t drawType = codes->read_uint32(codes);

                if (state->verbose) printf("   Rendering DRAW_ARRAY type %d\n",
                                    drawType);

                glDrawArrays(drawType, 0, drawCount);

                break;
            }

            case OP_ELEMENT_ARRAY: {
                uint64_t elementId = codes->read_uint64(codes);
                uint32_t elementType = codes->read_uint32(codes);
                if (state->verbose) printf("   Rendering ELEMENT_ARRAY type %d\n",
                                    elementType);

                // This should never fail!
                vx_resc_t * vr  = NULL;
                zhash_get(state->resource_map, &elementId, &vr);
                assert(vr != NULL);

                if (zhash_contains(state->vbo_map, &elementId)) {
                    GLuint vbo_id = 0;
                    zhash_get(state->vbo_map, &elementId, &vbo_id);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo_id);
                } else {
                    vbo_allocate(state, GL_ELEMENT_ARRAY_BUFFER, vr);
                }

                glDrawElements(elementType, vr->count, vr->type, NULL);
                break;
            }

            default:
                assert(0);
        }
    }

    // Important: Disable all vertex attribute arrays, or we can contaminate the state
    // for future programs. Might be resolved by switching to VBAs
    for (int i = 0; i < attribCount; i++)
        glDisableVertexAttribArray(attribLocs[i]);

    return 0;
}

// NOTE: Thread safety must be guaranteed externally
void vx_gl_renderer_draw_frame(vx_gl_renderer_t * state, int width, int height)
{
    static int first = 1;
    if (first) {
        printf("CDBG: First render() call %d layers\n", zhash_size(state->layer_map));
        first = 0;
    }

    if (state->verbose > 1) {

        int range[] = {0, 0};
        int precision = 0;

        glGetShaderPrecisionFormat(GL_VERTEX_SHADER, GL_LOW_FLOAT, range, &precision);
        printf("VERT LOW_FLOAT range = [%d,%d] precision = %d\n", range[0], range[1], precision);

        glGetShaderPrecisionFormat(GL_VERTEX_SHADER, GL_MEDIUM_FLOAT, range, &precision);
        printf("VERT MEDIUM_FLOAT range = [%d,%d] precision = %d\n", range[0], range[1], precision);

        glGetShaderPrecisionFormat(GL_VERTEX_SHADER, GL_HIGH_FLOAT, range, &precision);
        printf("VERT HIGH_FLOAT range = [%d,%d] precision = %d\n", range[0], range[1], precision);

        glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_LOW_FLOAT, range, &precision);
        printf("FRAG LOW_FLOAT range = [%d,%d] precision = %d\n", range[0], range[1], precision);

        glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_MEDIUM_FLOAT, range, &precision);
        printf("FRAG MEDIUM_FLOAT range = [%d,%d] precision = %d\n", range[0], range[1], precision);

        glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
        printf("FRAG HIGH_FLOAT range = [%d,%d] precision = %d\n", range[0], range[1], precision);

        int depth_bits = 0;
        glGetIntegerv(GL_DEPTH_BITS, &depth_bits);
        printf("GL_DEPTH_BITS  %d\n", depth_bits);
    }

    // Deallocate any resources flagged for deletion
    process_deallocations(state);

    // debug: print stats
    if (state->verbose) printf("n layers %d n resc %d, n vbos %d, n programs %d n tex %d w %d h %d\n",
                        zhash_size(state->layer_map),
                        zhash_size(state->resource_map),
                        zhash_size(state->vbo_map),
                        zhash_size(state->program_map),
                        zhash_size(state->texture_map), width, height);

    /* resize_fbo(state, width, height); */

    glViewport(0,0,width,height);
    glEnable(GL_SCISSOR_TEST);

    // We process each layer, change the viewport, and then process each buffer in the associated world:
    zarray_t * layers = zhash_values(state->layer_map);

    zarray_sort(layers, layer_info_compare);

    for (int i = 0; i < zarray_size(layers); i++) {
        vx_layer_info_t * layer = NULL;
        zarray_get(layers, i, &layer);

        glScissor (layer->viewport[0], layer->viewport[1], layer->viewport[2], layer->viewport[3]);
        glViewport(layer->viewport[0], layer->viewport[1], layer->viewport[2], layer->viewport[3]);
        if (state->verbose) printf("viewport for layer %d is [%d, %d, %d, %d]\n",
                            layer->layer_id, layer->viewport[0], layer->viewport[1], layer->viewport[2], layer->viewport[3]);

        // Background color
        glClearColor(layer->bg_color[0],
                     layer->bg_color[1],
                     layer->bg_color[2],
                     layer->bg_color[3]);

        uint8_t depthEnabled = 1;
        if (depthEnabled)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND); // needed for colors alpha transparency
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        vx_world_info_t * world = NULL;
        zhash_get(state->world_map, &layer->world_id, &world);
        if (world == NULL){ // haven't uploaded world yet?
            if (state->verbose) printf("WRN: world %d not populated yet!\n", layer->world_id);
            continue;
        }

        zarray_t * buffers = zhash_values(world->buffer_map);
        zarray_sort(buffers, buffer_compare);

        for (int i = 0; i < zarray_size(buffers); i++) {
            vx_buffer_info_t * buffer = NULL;
            zarray_get(buffers, i, &buffer);
            assert(buffer);
            assert(buffer->codes);
            assert(buffer->codes->reset);
            buffer->codes->reset(buffer->codes);

            uint8_t enabled = 1; // default to enabled
            zhash_get(layer->enabled_buffers, &buffer->name, &enabled);
            if (!enabled)
                continue;

            if (state->verbose) printf("  Rendering buffer: %s with order %d codes->len %d codes->pos %d\n",
                                buffer->name, buffer->draw_order, buffer->codes->len, buffer->codes->pos);

            // XXX For doing lighting, we'll need the model and
            // projection matrices separately.
            vx_matrix_stack_t *model_stack = vx_matrix_stack_create();
            vx_matrix_stack_t *proj_stack = vx_matrix_stack_create();
            vx_matrix_stack_setf(proj_stack, layer->layer_pm);

            // store depth enabled T/F as uint8
            zarray_t *depth_stack = zarray_create(sizeof(uint8_t));

            while (buffer->codes->pos < buffer->codes->len) {
                uint32_t op = buffer->codes->read_uint32(buffer->codes);

                switch (op) {
                    case OP_PROGRAM:
                        render_program(state, layer, buffer->codes, buffer->name, proj_stack, layer->eye3, model_stack);
                        break;

                    case OP_MODEL_PUSH:
                        vx_matrix_stack_push(model_stack);
                        break;

                    case OP_MODEL_POP:
                        vx_matrix_stack_pop(model_stack);
                        break;

                    case OP_MODEL_IDENT:
                        vx_matrix_stack_ident(model_stack);
                        break;

                    case OP_MODEL_MULTF: {
                        double userM[16];
                        for (int i =0; i < 16; i++)
                            userM[i] = (double) buffer->codes->read_float(buffer->codes);

                        vx_matrix_stack_mult(model_stack, userM);
                        break;
                    }

                    case OP_PROJ_PUSH:
                        vx_matrix_stack_push(proj_stack);
                        break;

                    case OP_PROJ_POP:
                        vx_matrix_stack_pop(proj_stack);
                        break;

                    case OP_PROJ_PIXCOORDS: {
                        uint32_t origin_code = buffer->codes->read_uint32(buffer->codes);
                        double pixPM[16];
                        pix_coords_pm(layer->viewport[2], layer->viewport[3], origin_code, pixPM);

                        if (0) {
                            printf("origin_code  %d vp[2]  %d vp[3] %d\n",
                                   origin_code, layer->viewport[2], layer->viewport[3]);
                            print44(pixPM);
                        }
                        vx_matrix_stack_set(proj_stack, pixPM);

                        break;
                    }

                    case OP_DEPTH_ENABLE: {
                        depthEnabled = 1;
                        glEnable(GL_DEPTH_TEST);
                        break;
                    }

                    case OP_DEPTH_DISABLE: {
                        depthEnabled = 0;
                        glDisable(GL_DEPTH_TEST);
                        break;
                    }

                    case OP_DEPTH_PUSH:
                        zarray_add(depth_stack, &depthEnabled);
                        break;

                    case OP_DEPTH_POP: {
                        zarray_get(depth_stack, 0, &depthEnabled);
                        zarray_remove_index(depth_stack, 0, 0);
                        if (depthEnabled)
                            glEnable(GL_DEPTH_TEST);
                        else
                            glDisable(GL_DEPTH_TEST);
                        break;
                    }

                }
            }
            zarray_destroy(depth_stack);
            vx_matrix_stack_destroy(model_stack);
            vx_matrix_stack_destroy(proj_stack);
        }
        zarray_destroy(buffers);
    }
    zarray_destroy(layers);


    state->change_since_last_render = 0;
}

vx_code_output_stream_t * vx_gl_renderer_serialize(vx_gl_renderer_t * rend)
{
    vx_code_output_stream_t * couts = vx_code_output_stream_create(128);

    // Write all the layers
    couts->write_uint32(couts, zhash_size(rend->layer_map));
    {
        zhash_iterator_t itr;
        uint32_t layer_id = 0;
        vx_layer_info_t * linfo = NULL;
        zhash_iterator_init(rend->layer_map, &itr);
        while (zhash_iterator_next(&itr, &layer_id, &linfo)) {

            int length = sizeof(uint32_t)*4 + sizeof(float)*4;
            couts->write_uint32(couts, length);

            couts->write_uint32(couts, OP_LAYER_INFO);
            couts->write_uint32(couts, linfo->layer_id);
            couts->write_uint32(couts, linfo->world_id);
            couts->write_uint32(couts, linfo->draw_order);
            couts->write_float(couts, linfo->bg_color[0]);
            couts->write_float(couts, linfo->bg_color[1]);
            couts->write_float(couts, linfo->bg_color[2]);
            couts->write_float(couts, linfo->bg_color[3]);
        }
    }

    // Output buffer codes for each buffer
    couts->write_uint32(couts, zhash_size(rend->world_map));
    {
        zhash_iterator_t itr;
        uint32_t world_id = 0;
        vx_world_info_t * winfo = NULL;
        zhash_iterator_init(rend->world_map, &itr);
        while (zhash_iterator_next(&itr, &world_id, &winfo)) {
            couts->write_uint32(couts, zhash_size(winfo->buffer_map));

            zhash_iterator_t bitr;
            char * bname = NULL;
            vx_buffer_info_t * binfo = NULL;
            zhash_iterator_init(winfo->buffer_map, &bitr);
            while (zhash_iterator_next(&bitr, &bname, &binfo)) {

                int length = sizeof(uint32_t)*2 + strlen(binfo->name) + 1 + sizeof(uint32_t) + binfo->codes->pos;

                couts->write_uint32(couts, length);
                couts->write_uint32(couts, OP_BUFFER_CODES);
                couts->write_uint32(couts, world_id);
                couts->write_str(couts, binfo->name);
                couts->write_uint32(couts, binfo->draw_order);

                couts->write_bytes(couts, binfo->codes->data,
                                   binfo->codes->pos);
            }
        }
    }

    // Output resources
    vx_tcp_util_pack_resources(rend->resource_map, couts);

    return couts;
}
