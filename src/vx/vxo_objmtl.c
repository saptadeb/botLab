#include "vxo_objmtl.h"
#include <stdlib.h>

#include <math.h>
#include <assert.h>
#include <libgen.h>
#include <stdio.h>
#include <float.h>


#include "vx_codes.h"
#include "vx_colors.h"
#include "vx_style.h"
#include "vx_resc.h"
#include "vxo_chain.h"
#include "vxo_mesh.h"
#include "vxo_lines.h"

#include "vx/math/math_util.h"
#include "common/string_util.h"
#include "common/zhash.h"

typedef struct
{
    float Ka[3];
    float Kd[3];
    float Ks[3];
    float Ke[3];

    float Ns;
    float Ni;
    float d;
    float Tr;
    float Tf[3];
    int illum;

} material_t;

typedef struct
{
    int vIdxs[3];
    int tIdxs[3];
    int nIdxs[3];

} tri_idx_t;

typedef struct
{
    // Store the indices into vertex, normal arrays
    zarray_t * group_idx; // < tri_idx_t> -- by value
    material_t material;
} wav_group_t;


// returns a hash <char*, material_t>, where keys and values are owned
// by the map, so be sure to free strings and dec ref the styles
static zhash_t * load_materials(const char * mtl_filename)
{
    FILE * fp_mtl = fopen(mtl_filename, "r");
    if (fp_mtl == NULL)
        return NULL;

    #define LNSZ 1024
    char line_buffer[LNSZ];

    // store materials by value
    zhash_t * mat_map = zhash_create(sizeof(char*), sizeof(material_t), zhash_str_hash, zhash_str_equals);

    char cur_name[LNSZ];

    material_t cur_material;
    memset(&cur_material,0, sizeof(cur_material));
    cur_material.illum = -1;

    // We commit to reading the
    while (1) {
        int eof = fgets(line_buffer, LNSZ, fp_mtl) == NULL;

        char * line = str_trim(line_buffer);

        // If possible, commit the old material
        if (str_starts_with(line, "newmtl") || eof) {
            if (cur_material.illum >= 0) {

                char * key = strdup(cur_name);

                char * oldkey = NULL;
                material_t oldmat;
                zhash_put(mat_map, &key, &cur_material, &oldkey, & oldmat);

                assert(oldkey == NULL);
                assert(cur_material.illum <= 2 && "can't handle anything higher than illum=2");
            }
        }

        if (eof)
            break;

        if (str_starts_with(line, "#") || strlen(line) == 0 || !strcmp(line, "\r"))
            continue;

        if (str_starts_with(line, "newmtl")) {
            sscanf(line, "newmtl %s", cur_name);
        } else if (str_starts_with(line, "Ns")) {
            sscanf(line, "Ns %f", &cur_material.Ns);
        } else if (str_starts_with(line, "Ni")) {
            sscanf(line, "Ni %f", &cur_material.Ni);
        } else if (str_starts_with(line, "d") || str_starts_with(line, "Tr")) {
            sscanf(line, "%*s %f", &cur_material.d);
        } else if (str_starts_with(line, "Tr")) {
            sscanf(line, "Tr %f", &cur_material.Tr);
        } else if (str_starts_with(line, "Tf")) {
            sscanf(line, "Tf %f %f %f", &cur_material.Tf[0],&cur_material.Tf[1],&cur_material.Tf[2]);
        }else if (str_starts_with(line, "illum")) {
            sscanf(line, "illum %d", &cur_material.illum);
        } else if (str_starts_with(line, "Ka")) {
            sscanf(line, "Ka %f %f %f", &cur_material.Ka[0],&cur_material.Ka[1],&cur_material.Ka[2]);
        } else if (str_starts_with(line, "Kd")) {
            sscanf(line, "Kd %f %f %f", &cur_material.Kd[0],&cur_material.Kd[1],&cur_material.Kd[2]);
        } else if (str_starts_with(line, "Ks")) {
            sscanf(line, "Ks %f %f %f", &cur_material.Ks[0],&cur_material.Ks[1],&cur_material.Ks[2]);
        } else if (str_starts_with(line, "Ke")) {
            sscanf(line, "Ke %f %f %f", &cur_material.Ke[0],&cur_material.Ke[1],&cur_material.Ke[2]);
        } else {
            printf("Did not parse: %s\n", line);

            for (int i = 0; i < strlen(line); i++) {
                printf("0x%x ", (int)line[i]);
            }
            printf("\n");
        }
    }

    fclose(fp_mtl);
    return mat_map;
}



static void wav_group_destroy(wav_group_t * wgrp)
{
    zarray_destroy(wgrp->group_idx);
    free(wgrp);
}


static void print_bounds(zarray_t * vertices)
{
    // Approximate centroid: Just average all vertices, regardless of
    // the frequency they are referenced by an index
    float minMax[3][2] = {{FLT_MAX,-FLT_MAX},{FLT_MAX,-FLT_MAX},{FLT_MAX,-FLT_MAX}};
    for (int i = 0; i < zarray_size(vertices); i++) {
        float pt[3];
        zarray_get(vertices, i, &pt);

        // iterate over xyz coords
        for (int j = 0; j < 3; j++) {
            minMax[j][0] = fminf(pt[j], minMax[j][0]);
            minMax[j][1] = fmaxf(pt[j], minMax[j][1]);
        }
    }

    printf(" Object bounds: xyz [%.2f, %.2f][%.2f, %.2f][%.2f, %.2f]\n",
           minMax[0][0],minMax[0][1],
           minMax[1][0],minMax[1][1],
           minMax[2][0],minMax[2][1]);
}

// Read in the entire file, save vertices, and indices for later processing.
static int vxo_objmtl_load(FILE *fp_obj, const char * obj_filename,
                           zarray_t *vertices, zarray_t *textures, zarray_t *normals,
                           zarray_t *group_list, zhash_t **mtl_map_pointer, int *texture_flag)
{
    #define LNSZ 1024
    char line_buffer[LNSZ];

    wav_group_t * cur_group = NULL;

    while (1) {
        int eof = fgets(line_buffer, LNSZ, fp_obj) == NULL;

        char * line = str_trim(line_buffer);

        // If possible, batch process the last group
        if (str_starts_with(line, "g ") || eof) {
            if (cur_group != NULL) {
                assert(cur_group->group_idx != NULL);

                zarray_add(group_list, &cur_group);
                cur_group = NULL;
            }
        }

        if (eof)
            break;

        if (str_starts_with(line, "#") || strlen(line) == 0 || !strcmp(line,"\r"))
            continue;

        if (str_starts_with(line, "g ")) {
            assert(*mtl_map_pointer != NULL);

            char obj_name[LNSZ];
            sscanf(line, "g %s", obj_name);

            cur_group = calloc(1, sizeof(wav_group_t));
            cur_group->group_idx = zarray_create(sizeof(tri_idx_t));

        } else if (str_starts_with(line, "v ")) {
            float vertex[3];
            sscanf(line, "v %f %f %f", &vertex[0], &vertex[1], &vertex[2]);
            zarray_add(vertices, &vertex);
        } else if (str_starts_with(line, "vn ")) {
            float normal[3];
            sscanf(line, "vn %f %f %f", &normal[0], &normal[1], &normal[2]);
            zarray_add(normals, &normal);
        } else if (str_starts_with(line, "vt ")) {
            *texture_flag = 1;
            float texture[3];
            sscanf(line, "vt %f %f %f", &texture[0], &texture[1], &texture[2]);
            zarray_add(textures, &texture);
        } else if (str_starts_with(line, "f ")) {
            tri_idx_t idxs;

            if (*texture_flag) {
                sscanf(line, "f %d/%d/%d %d/%d/%d %d/%d/%d",
                       &idxs.vIdxs[0], &idxs.tIdxs[0], &idxs.nIdxs[0],
                       &idxs.vIdxs[1], &idxs.tIdxs[1], &idxs.nIdxs[1],
                       &idxs.vIdxs[2], &idxs.tIdxs[2], &idxs.nIdxs[2]);
            }
            else {
                sscanf(line, "f %d//%d %d//%d %d//%d",
                       &idxs.vIdxs[0], &idxs.nIdxs[0],
                       &idxs.vIdxs[1], &idxs.nIdxs[1],
                       &idxs.vIdxs[2], &idxs.nIdxs[2]);
            }

            zarray_add(cur_group->group_idx, &idxs);

        } else if (str_starts_with(line, "usemtl ")) {
            char *mname = calloc(1, sizeof(char)*1024);
            sscanf(line, "usemtl %s", mname);
            zhash_get(*mtl_map_pointer, &mname, &cur_group->material);
            free(mname);
        } else if (str_starts_with(line, "s ")) {
            // No idea what to do with smoothing instructions
        } else if (str_starts_with(line, "mtllib ")) {
            char * cur_path = strdup(obj_filename);
            const char * dir_name = dirname(cur_path);

            char mtl_basename[LNSZ];
            sscanf(line, "mtllib %s", mtl_basename);

            char mtl_filename[LNSZ];
            sprintf(mtl_filename,"%s/%s", dir_name, mtl_basename);

            *mtl_map_pointer = load_materials(mtl_filename);
            if (*mtl_map_pointer == NULL) {
                zarray_destroy(vertices);
                zarray_destroy(normals);
                return 1;
            }
            free(cur_path);
        } else {
            printf("Did not parse: %s\n", line);

            for (int i = 0; i < strlen(line); i++) {
                printf("0x%x ", (int)line[i]);
            }
            printf("\n");
        }
    }

    return 0;
}

vx_object_t * vxo_objmtl(const char * obj_filename)
{
    int  texture_flag = 0;

    FILE * fp_obj = fopen(obj_filename, "r");
    if (fp_obj == NULL)
        return NULL;

    // Store 3D vertices by value
    zarray_t    * vertices   = zarray_create(sizeof(float)*3);
    zarray_t    * textures   = zarray_create(sizeof(float)*3);
    zarray_t    * normals    = zarray_create(sizeof(float)*3);
    zarray_t    * group_list = zarray_create(sizeof(wav_group_t*));
    zhash_t     * mtl_map    = NULL; // created on reading mtllib entry
    vx_object_t * vchain     = NULL;

    zarray_t * sorted_groups = zarray_create(sizeof(wav_group_t*));

    if (vxo_objmtl_load(fp_obj, obj_filename,
                        vertices, textures, normals,
                        group_list, &mtl_map, &texture_flag))
    {
        goto cleanup; // return value is NULL;
    }

    if (1) // useful to enable when compensating for model scale
        print_bounds(vertices);

    // Process the model sections in two passes -- first add all the
    // objects which are not transparent. Then render transparent
    // objects after

    vchain = vxo_chain_create();

    for (int i = 0, sz = zarray_size(group_list); i < sz; i++) {
        wav_group_t * group = NULL;
        zarray_get(group_list, i, &group);

        // add to front if solid
        if (group->material.d == 1.0f) {
            zarray_insert(sorted_groups, 0, &group);
        } else { // add to back if transparent
            zarray_add(sorted_groups, &group);
        }
    }

    int total_triangles = 0;
    for (int i = 0, sz = zarray_size(sorted_groups); i < sz; i++) {
        wav_group_t * group = NULL;
        zarray_get(sorted_groups, i, &group);

        int ntri =  zarray_size(group->group_idx);

        vx_resc_t * vert_resc = vx_resc_createf(ntri*9);
        vx_resc_t * norm_resc = vx_resc_createf(ntri*9);

        for (int j = 0; j < ntri; j++) {
            tri_idx_t idxs;
            zarray_get(group->group_idx, j, &idxs);

            for (int  i = 0; i < 3; i++) {
                zarray_get(vertices, idxs.vIdxs[i]-1, &((float*)vert_resc->res)[9*j + i*3]);
                zarray_get(normals,  idxs.nIdxs[i]-1, &((float*)norm_resc->res)[9*j + i*3]);
            }
        }

        vx_style_t * sty = vxo_mesh_style_fancy(group->material.Ka, group->material.Kd, group->material.Ks, group->material.d, group->material.Ns, group->material.illum);
        vxo_chain_add(vchain, vxo_mesh(vert_resc, ntri*3, norm_resc, GL_TRIANGLES, sty));

        total_triangles += ntri;
    }

cleanup:
    // 1. Materials, names are by reference, but materials are by value
    zhash_vmap_keys(mtl_map, free);
    zhash_destroy(mtl_map);

    // 2. Geometry
    zarray_destroy(vertices); // stored by value
    zarray_destroy(normals); // stored by value

    // 2b wav_group_t are stored by reference

    zarray_vmap(group_list, wav_group_destroy);
    zarray_destroy(group_list);
    zarray_destroy(sorted_groups); // duplicate list, so don't need to free

    fclose(fp_obj);

    return vchain;
}

vx_object_t * vxo_objmtl_outline(const char * obj_filename, int outline_num_points)
{
    int  texture_flag = 0;

    FILE * fp_obj = fopen(obj_filename, "r");
    if (fp_obj == NULL)
        return NULL;

    // Store 3D vertices by value
    zarray_t    * vertices   = zarray_create(sizeof(float)*3);
    zarray_t    * textures   = zarray_create(sizeof(float)*3);
    zarray_t    * normals    = zarray_create(sizeof(float)*3);
    zarray_t    * group_list = zarray_create(sizeof(wav_group_t*));
    zhash_t     * mtl_map    = NULL; // created on reading mtllib entry
    vx_object_t * vchain     = NULL;

    float outline_range[outline_num_points];
    for (int i = 0; i < outline_num_points; i++) {
        outline_range[i] = 0;
    }

    float outline_buf[3*(2+outline_num_points)];
    int outline_index = 0;

    float meshcolor[4] = { 80.0f/255, 80.0f/255, 160.0f/255, 1.0f };

    if (vxo_objmtl_load(fp_obj, obj_filename,
                        vertices, textures, normals,
                        group_list, &mtl_map, &texture_flag))
    {
        goto cleanup; // return value is NULL
    }

    if (1) // useful to enable when compensating for model scale
        print_bounds(vertices);

    // Process the model sections in two passes -- first add all the
    // objects which are not transparent. Then render transparent
    // objects after

    vchain = vxo_chain_create();

    zarray_t * sorted_groups = zarray_create(sizeof(wav_group_t*));
    for (int i = 0, sz = zarray_size(group_list); i < sz; i++) {
        wav_group_t * group = NULL;
        zarray_get(group_list, i, &group);

        // add to front if solid
        if (group->material.d == 1.0f) {
            zarray_insert(sorted_groups, 0, &group);
        } else { // add to back if transparent
            zarray_add(sorted_groups, &group);
        }
    }

    for (int i = 0, sz = zarray_size(sorted_groups); i < sz; i++) {
        wav_group_t * group = NULL;
        zarray_get(sorted_groups, i, &group);

        int ntri =  zarray_size(group->group_idx);

        for (int j = 0; j < ntri; j++) {
            tri_idx_t idxs;
            zarray_get(group->group_idx, j, &idxs);

            for (int  i = 0; i < 3; i++)
            {
                float xyz[3] = { 0 };
                zarray_get(vertices, idxs.vIdxs[i]-1, xyz);

                double theta = mod2pi_positive(atan2(xyz[2], xyz[1]));
                int bin = (int) floor(theta * outline_num_points / (2*M_PI));

                double range = sqrt(xyz[1]*xyz[1] + xyz[2]*xyz[2]);
                outline_range[bin] = fmax(outline_range[bin], range);
            }
        }
    }

    outline_buf[3*outline_index+0] = 0.0f;
    outline_buf[3*outline_index+1] = 0.0f;
    outline_buf[3*outline_index+2] = 0.0f;
    outline_index++;

    for (int i = 0; i < outline_num_points; i++)
    {
        double range = outline_range[i];
        if (range == 0)
            continue;

        double theta = (i + 0.5) * 2*M_PI / outline_num_points;

        outline_buf[3*outline_index+0] = 0.0f;
        outline_buf[3*outline_index+1] = range*cos(theta);
        outline_buf[3*outline_index+2] = range*sin(theta);
        outline_index++;
    }

    outline_buf[3*outline_index+0] = outline_buf[3*1+0];
    outline_buf[3*outline_index+1] = outline_buf[3*1+1];
    outline_buf[3*outline_index+2] = outline_buf[3*1+2];
    outline_index++;

    vxo_chain_add(vchain, vxo_mesh(vx_resc_copyf(outline_buf, 3*outline_index),
                                   outline_index,
                                   NULL,
                                   GL_TRIANGLE_FAN,
                                   vxo_mesh_style(meshcolor)));
    vxo_chain_add(vchain,
                  vxo_lines(vx_resc_copyf(&outline_buf[3], 3*(outline_index-2)),
                            outline_index-2,
                            GL_LINE_LOOP,
                            vxo_lines_style(vx_white, 4)));

cleanup:
    // 1. Materials, names are by reference, but materials are by value
    zhash_vmap_keys(mtl_map, free);
    zhash_destroy(mtl_map);

    // 2. Geometry
    zarray_destroy(vertices); // stored by value
    zarray_destroy(normals); // stored by value

    // 2b wav_group_t are stored by reference

    zarray_vmap(group_list, wav_group_destroy);
    zarray_destroy(group_list);

    fclose(fp_obj);

    return vchain;
}

