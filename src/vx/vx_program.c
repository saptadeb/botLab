#include "vx_program.h"

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <inttypes.h>

#include "vx_codes.h"
#include "vx_global.h"
#include "common/zhash.h"

static int verbose = 0;

static int library_init = 0;

struct vx_program_state
{
    vx_resc_t * vert;
    vx_resc_t * frag;

    uint8_t use_PM_matrix; // proj*model
    uint8_t use_M_matrix;  // model matrix
    uint8_t use_N_matrix;  // N = (model)^-1
    uint8_t use_cam_position;// Position of the camera in the world frame

    zhash_t * attribMap; //<char*, _vertex_attrib_t> and many more

    // Uniforms have memory allocated by this object, stored in these maps
    // which must be freed when this object is destroyed
    zhash_t * uniformNfvMap; //<char*, _float_uniform_t*>
    zhash_t * uniformNivMap; //<char*, _int32_uniform_t*>

    //Textures
    zhash_t * texMap; // <char*, _texinfo_t>

    float size;

    int draw_type;
    int draw_count; // if draw_array
    vx_resc_t * indices; // if element_array
};

typedef struct
{
    int dim;
    float * data;
    char * name;
} _float_uniform_t;

typedef struct
{
    int dim;
    int32_t * data;
    char * name;
} _int32_uniform_t;

typedef struct _vertex_attrib _vertex_attrib_t;
struct _vertex_attrib
{
    vx_resc_t * vr;
    int dim; // how many 'types' per vertex. e.g. 3 for xyz data
    char * name;
};

typedef struct _texinfo _texinfo_t;
struct _texinfo
{
    vx_resc_t * vr;
    char * name;
    int width, height, format, flags;
};


// XXX thread safety, in access, AND initialization....
static char * shader_dir = NULL;
static zhash_t * shader_store = NULL; // Stores _shader_pair each shader name

typedef struct {
    vx_resc_t * vert;
    vx_resc_t * frag;
} _shader_pair_t;

static void shader_pair_destroy(_shader_pair_t * pair)
{
    vx_resc_dec_destroy(pair->vert);
    vx_resc_dec_destroy(pair->frag);
    free(pair);
}

static void vx_program_library_destroy()
{
    assert(shader_dir != NULL && shader_store != NULL);
    free(shader_dir);

    zhash_vmap_keys(shader_store, free);
    zhash_vmap_values(shader_store, shader_pair_destroy);
    zhash_destroy(shader_store);
}

static void vx_program_library_init()
{
    pthread_mutex_lock(&vx_convenience_mutex);
    if (library_init){
        pthread_mutex_unlock(&vx_convenience_mutex);
        return;
    }

    assert(shader_dir == NULL && shader_store == NULL);

    const char *shader_path = getenv("VX_SHADER_PATH");
    if (shader_path == NULL) {
        printf("WARNING: No VX_SHADER_PATH specified.\n");
        shader_path = ".";
    }
    shader_dir = strdup(shader_path);

    shader_store = zhash_create(sizeof(char*), sizeof(_shader_pair_t*), zhash_str_hash, zhash_str_equals);

    vx_global_register_destroy(vx_program_library_destroy, NULL);

    library_init = 1;
    pthread_mutex_unlock(&vx_convenience_mutex);
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

vx_program_t * vx_program_load_library(char * name)
{
    if (!library_init) {
        vx_program_library_init();
    }

    assert(shader_dir != NULL && shader_store != NULL);

    _shader_pair_t * pair = NULL;
    zhash_get(shader_store, &name, &pair);

    if (pair == NULL) {
        char frag_file[1024];
        char vert_file[1024];

        sprintf(frag_file, "%s/%s.frag", shader_dir, name);
        sprintf(vert_file, "%s/%s.vert", shader_dir, name);

        if (verbose) printf("Loading program from %s and %s\n", frag_file, vert_file);

        pair = calloc(1, sizeof(_shader_pair_t));
        pair->vert = vx_resc_load(vert_file);
        if (pair->vert == NULL) {
            printf("ERR: Couldn't open vertex shader file %s\n", vert_file);
            exit(-1);
        }


        pair->frag = vx_resc_load(frag_file);
        if (pair->frag == NULL) {
            printf("ERR: Couldn't open fragment shader file %s\n", frag_file);
            exit(-1);
        }

        vx_resc_inc_ref(pair->vert); // Manually hold onto a reference
        vx_resc_inc_ref(pair->frag);
        if (verbose) printf("Loaded program %s with vertID %ld and fragID %ld\n", name, pair->vert->id, pair->frag->id);

        if (verbose > 1) {
            const char * vertSource = pair->vert->res;
            const char * fragSource = pair->frag->res;

            printf("Vertex id %"PRIu64" len %zu ct*wd %d:\n", pair->vert->id, strlen(vertSource),
                   pair->vert->count*pair->vert->fieldwidth);
            print_shader(vertSource);

            printf("Fragment id %"PRIu64" len %zu ct*wd %d:\n", pair->frag->id, strlen(fragSource),
                   pair->frag->count*pair->frag->fieldwidth);
            print_shader(fragSource);
        }

        char * name_copy = strdup(name);
        zhash_put(shader_store, &name_copy, &pair, NULL, NULL);
    }

    return vx_program_create(pair->vert, pair->frag);
}


static vx_program_state_t * vx_program_state_create()
{
    vx_program_state_t * state = calloc(1, sizeof(vx_program_state_t));
    state->attribMap = zhash_create(sizeof(char*), sizeof(_vertex_attrib_t*), zhash_str_hash, zhash_str_equals);
    state->uniformNfvMap = zhash_create(sizeof(char*), sizeof(_float_uniform_t*), zhash_str_hash, zhash_str_equals);
    state->uniformNivMap = zhash_create(sizeof(char*), sizeof(_int32_uniform_t*), zhash_str_hash, zhash_str_equals);

    state->texMap = zhash_create(sizeof(char*), sizeof(_texinfo_t*), zhash_str_hash, zhash_str_equals);

    state->size = 1.0f;
    state->draw_type = -1;
    state->draw_count = -1;
    state->indices = NULL;

    state->use_PM_matrix = 1; // default=enabled

    return state;
}

static void _vertex_attrib_destroy(_vertex_attrib_t * attrib)
{
    vx_resc_dec_destroy(attrib->vr);
    free(attrib->name);
    free(attrib);

}

static void _texinfo_destroy(_texinfo_t * tex)
{
    vx_resc_dec_destroy(tex->vr);
    free(tex->name);
    free(tex);
}

static void _float_uniform_destroy(_float_uniform_t * funif)
{
    free(funif->name);
    free(funif->data);
    free(funif);
}

static void _int32_uniform_destroy(_int32_uniform_t * iunif)
{
    free(iunif->name);
    free(iunif->data);
    free(iunif);
}


static void vx_program_state_destroy(vx_program_state_t * state)
{
    // decrement references to all vx_resources, then call destroy

    // direct references
    vx_resc_dec_destroy(state->vert);
    vx_resc_dec_destroy(state->frag);

    if (state->indices != NULL)
        vx_resc_dec_destroy(state->indices);

    // maps:
    zhash_vmap_values(state->attribMap, _vertex_attrib_destroy); // <char*, _vertex_attrib_t>
    zhash_vmap_values(state->uniformNfvMap, _float_uniform_destroy); // <char*, _float_uniform_t>
    zhash_vmap_values(state->uniformNivMap, _int32_uniform_destroy); // <char*, _int32_uniform_t>
    zhash_vmap_values(state->texMap, _texinfo_destroy); // <char*, _texinfo_t>

    // note, for uniform structs and _vertex_attrib_t and _texinfo_t, the 'char* name' is
    // stored in the associated struct, so it does not need to be freed using
    // the map2 call, hence we pass NULL.

    zhash_destroy(state->attribMap);
    zhash_destroy(state->uniformNfvMap);
    zhash_destroy(state->uniformNivMap);
    zhash_destroy(state->texMap);

    // Would also need to decrement any reference counts of sub vx_objects...
    free(state);
}

static void vx_program_destroy(vx_object_t * vo)
{
    vx_program_t * prog = (vx_program_t*)vo->impl;

    vx_program_state_destroy(prog->state);

    free(vo);
    free(prog);
}

static void vx_program_append(vx_object_t * obj, zhash_t * resources, vx_code_output_stream_t * codes)
{
    // Safe because this function is only assigned to vx_program types
    vx_program_state_t * state = ((vx_program_t *)obj->impl)->state;

    codes->write_uint32(codes, OP_PROGRAM);
    codes->write_uint64(codes, state->vert->id);
    codes->write_uint64(codes, state->frag->id);

    zhash_put(resources, &state->vert->id, &state->vert, NULL, NULL);
    zhash_put(resources, &state->frag->id, &state->frag, NULL, NULL);

    codes->write_uint32(codes, OP_VALIDATE_PROGRAM);

    if (state->use_PM_matrix) {
        codes->write_uint32(codes, OP_PM_MAT_NAME);
        codes->write_str(codes, "PM"); // XXX hardcoded
    }

    if (state->use_N_matrix) {
        codes->write_uint32(codes, OP_NORMAL_MAT_NAME);
        codes->write_str(codes, "N"); // XXX hardcoded
    }

    if (state->use_M_matrix) {
        codes->write_uint32(codes, OP_MODEL_MAT_NAME);
        codes->write_str(codes, "M"); // XXX hardcoded
    }

    if (state->use_cam_position) {
        codes->write_uint32(codes, OP_CAM_POS_NAME);
        codes->write_str(codes, "view_world"); // XXX hardcoded
    }

    {
        zhash_iterator_t itr;
        zhash_iterator_init(state->attribMap, &itr);

        char * key = NULL;
        _vertex_attrib_t * value = NULL;
        while(zhash_iterator_next(&itr, &key, &value)) {
            codes->write_uint32(codes, OP_VERT_ATTRIB);
            codes->write_uint64(codes, value->vr->id);
            codes->write_uint32(codes, value->dim);
            codes->write_str(codes,  value->name);

            zhash_put(resources, &value->vr->id, &value->vr, NULL, NULL);
        }
    }

    {
        zhash_iterator_t itr;
        zhash_iterator_init(state->uniformNfvMap, &itr);
        char * key = NULL;
        _float_uniform_t * funif = NULL;

        while(zhash_iterator_next(&itr, &key, &funif)) {
            codes->write_uint32(codes, OP_UNIFORM_VECTOR_FV);
            codes->write_str(codes, key);
            codes->write_uint32(codes, funif->dim);
            for (int i = 0; i < funif->dim; i++)
                codes->write_float(codes, funif->data[i]);
        }
    }

    {
        zhash_iterator_t itr;
        zhash_iterator_init(state->uniformNivMap, &itr);
        char * key = NULL;
        _int32_uniform_t * iunif = NULL;

        while(zhash_iterator_next(&itr, &key, &iunif)) {
            codes->write_uint32(codes, OP_UNIFORM_VECTOR_IV);
            codes->write_str(codes, key);
            codes->write_uint32(codes, iunif->dim);
            for (int i = 0; i < iunif->dim; i++)
                codes->write_uint32(codes, iunif->data[i]);
        }
    }


    {
        zhash_iterator_t itr;
        zhash_iterator_init(state->texMap, &itr);

        char * key = NULL;
        _texinfo_t * value = NULL;
        while(zhash_iterator_next(&itr, &key, &value)) {
            codes->write_uint32(codes, OP_TEXTURE);
            codes->write_str(codes,  value->name);
            codes->write_uint64(codes, value->vr->id);

            codes->write_uint32(codes, value->width);
            codes->write_uint32(codes, value->height);
            codes->write_uint32(codes, value->format);
            codes->write_uint32(codes, value->flags);

            zhash_put(resources, &value->vr->id, &value->vr, NULL, NULL);
        }
    }

    {
        codes->write_uint32(codes, OP_LINE_WIDTH);
        codes->write_float(codes, state->size);
    }

    // bind drawing instructions
    if (state->indices != NULL) {
        // Element array
        codes->write_uint32(codes, OP_ELEMENT_ARRAY);
        codes->write_uint64(codes, state->indices->id);
        codes->write_uint32(codes, state->draw_type);

        zhash_put(resources, &state->indices->id, &state->indices, NULL, NULL);
    } else {
        // draw array
        codes->write_uint32(codes, OP_DRAW_ARRAY);
        codes->write_uint32(codes, state->draw_count);
        codes->write_uint32(codes, state->draw_type);
    }

    codes->write_uint32(codes, 0);
}


vx_program_t * vx_program_create(vx_resc_t * vert_src, vx_resc_t * frag_src)
{
    vx_program_t * program = malloc(sizeof(vx_program_t));
    vx_object_t * obj = calloc(1,sizeof(vx_object_t));
    obj->append = vx_program_append;
    obj->impl = program;
    obj->destroy = vx_program_destroy;

    program->super = obj;
    program->state = vx_program_state_create();
    program->state->vert = vert_src;
    program->state->frag = frag_src;

    vx_resc_inc_ref(program->state->vert);
    vx_resc_inc_ref(program->state->frag);

    return program;
}

void vx_program_set_draw_array(vx_program_t * program, int count, int type)
{
    assert(program->state->draw_type == -1); // Enforce only calling this once, for now

    program->state->draw_type = type;
    program->state->draw_count = count;
}

void vx_program_set_element_array(vx_program_t * program, vx_resc_t * indices, int type)
{
    assert(program->state->draw_type == -1); // Enforce only calling this once, for now
    assert(indices->type == GL_UNSIGNED_INT);
    program->state->draw_type = type;
    program->state->indices = indices;
    vx_resc_inc_ref(program->state->indices);
}


void vx_program_set_vertex_attrib(vx_program_t * program, char * name, vx_resc_t * attrib, int dim)
{
    vx_resc_inc_ref(attrib);

    _vertex_attrib_t * va = malloc(sizeof(_vertex_attrib_t));
    va->vr = attrib;
    va->dim = dim;
    va->name = strdup(name);

    char * old_key = NULL;
    _vertex_attrib_t * old_value = NULL;

    zhash_put(program->state->attribMap, &va->name, &va, &old_key, &old_value);

    assert(old_key == NULL);
}

static void vx_program_set_uniformNiv(vx_program_t * program, const char * name, int n, const int32_t * vecN)
{
    _int32_uniform_t * iunif = calloc(1, sizeof(_int32_uniform_t));
    iunif->dim = n;
    iunif->data = calloc(1, sizeof(int32_t)*iunif->dim);
    iunif->name = strdup(name);
    memcpy(iunif->data, vecN, sizeof(int32_t)*iunif->dim);

    char * old_name = NULL;
    _int32_uniform_t * old_iunif = NULL;
    zhash_put(program->state->uniformNivMap, &iunif->name, &iunif, &old_name, &old_iunif);

    // If there's an old value, free it
    if (old_iunif)
        _int32_uniform_destroy(old_iunif);
}

void vx_program_set_uniform4iv(vx_program_t * program, char * name, const int32_t * vec4)
{
    vx_program_set_uniformNiv(program, name, 4, vec4);
}

void vx_program_set_uniform3iv(vx_program_t * program, char * name, const int32_t * vec3)
{
    vx_program_set_uniformNiv(program, name, 3, vec3);
}

void vx_program_set_uniform2iv(vx_program_t * program, char * name, const int32_t * vec2)
{
    vx_program_set_uniformNiv(program, name, 2, vec2);
}

void vx_program_set_uniform1iv(vx_program_t * program, char * name, const int32_t * vec1)
{
    vx_program_set_uniformNiv(program, name, 1, vec1);
}

static void vx_program_set_uniformNfv(vx_program_t * program, const char * name, int n, const float * vecN)
{
    _float_uniform_t * funif = calloc(1, sizeof(_float_uniform_t));
    funif->dim = n;
    funif->data = calloc(1, sizeof(float)*funif->dim);
    funif->name = strdup(name);
    memcpy(funif->data, vecN, sizeof(float)*funif->dim);

    char * old_name = NULL;
    _float_uniform_t * old_funif = NULL;
    zhash_put(program->state->uniformNfvMap, &funif->name, &funif, &old_name, &old_funif);

    // If there's an old value, free it
    if (old_funif)
        _float_uniform_destroy(old_funif);
}

void vx_program_set_uniform4fv(vx_program_t * program, char * name, const float * vec4)
{
    vx_program_set_uniformNfv(program, name, 4, vec4);
}

void vx_program_set_uniform3fv(vx_program_t * program, char * name, const float * vec3)
{
    vx_program_set_uniformNfv(program, name, 3, vec3);
}

void vx_program_set_uniform2fv(vx_program_t * program, char * name, const float * vec2)
{
    vx_program_set_uniformNfv(program, name, 2, vec2);
}

void vx_program_set_uniform1fv(vx_program_t * program, char * name, const float * vec1)
{
    vx_program_set_uniformNfv(program, name, 1, vec1);
}

void vx_program_set_texture(vx_program_t * program, char * name, vx_resc_t * vr, int width, int height, int format, uint32_t flags)
{
    vx_resc_inc_ref(vr);

    _texinfo_t * tinfo = malloc(sizeof(_texinfo_t));
    tinfo->name = strdup(name);
    tinfo->vr = vr;
    tinfo->width = width;
    tinfo->height = height;
    tinfo->format = format;
    tinfo->flags = flags;

    char * old_key = NULL;
    _vertex_attrib_t * old_value = NULL;
    zhash_put(program->state->texMap, &tinfo->name, &tinfo, &old_key, &old_value);
    assert(old_key == NULL); // For now, don't support over writing existing values
}

void vx_program_set_line_width(vx_program_t * program, float size)
{
    program->state->size = size;
}


void vx_program_set_flags(vx_program_t * prog, int flags)
{

    if (flags & VX_PROGRAM_USE_PM) {
        prog->state->use_PM_matrix = 1;
    }

    if (flags & VX_PROGRAM_USE_M) {
        prog->state->use_M_matrix = 1;
    }

    if (flags & VX_PROGRAM_USE_N) {
        prog->state->use_N_matrix = 1;
    }

    if (flags & VX_PROGRAM_USE_CAM_POS) {
        prog->state->use_cam_position = 1;
    }
}
