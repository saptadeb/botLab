#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "vx_codes.h"
#include "vx_global.h"

#include "vxo_points.h"
#include "vxo_lines.h"
#include "vxo_mesh.h"

#include "vxo_sphere.h"
#include "vx_program.h"
#include "vx_util.h"
#include "vxo_chain.h"
#include "vxo_mat.h"

#include "common/zarray.h"
#include "common/zhash.h"


static vx_resc_t * vertex_points = NULL;
static vx_resc_t * tri_indices = NULL;
//static vx_resc_t * tri_normals = NULL;
static vx_resc_t * line_indices = NULL;
static int NVERTS = 0;
static int NTRIS = 0;

// XXX Move to a math library
static float* normalize(float vec[], float n[], int len)
{
    float sum = 0.0f;
    for (int i = 0; i < len; i++) {
        sum += vec[i]*vec[i];
    }
    sum = sqrt(sum);
    if (sum <= 0)
        return n;

    for (int i = 0; i < len; i++) {
        n[i] = vec[i]/sum;
    }

    return n;
}

static float* add(float a[], float b[], float r[], int len)
{
    for (int i = 0; i < len; i++) {
        r[i] = a[i] + b[i];
    }
    return r;
}

/*static float* sub(float a[], float b[], float r[], int len)
{
    for (int i = 0; i < len; i++) {
        r[i] = a[i] - b[i];
    }
    return r;
}*/

// 3x3 only
/*static float* crossp(float a[], float b[], float r[], int len)
{
    r[0] = a[1]*b[2] - a[2]*b[1];
    r[1] = a[2]*b[0] - a[0]*b[2];
    r[2] = a[0]*b[1] - a[1]*b[0];

    normalize(r, r, len);
    return r;
}*/

static void free_array(zarray_t *za)
{
    for (int i = 0; i < zarray_size(za); i++) {
        void *v = NULL;
        zarray_get(za, i, &v);
        free(v);
    }
}

static void vxo_sphere_init(int quality)
{
    // Initialize sphere as a tetrahedron
    const float v = (float)(sqrt(3.0)/3);
    // Since we're going to try to free other verts later, allocate
    // these to the heap (as well as the triangles);
    float *va = malloc(3*sizeof(float));
    va[0] = v;
    va[1] = v;
    va[2] = v;
    float *vb = malloc(3*sizeof(float));
    vb[0] = -v;
    vb[1] = -v;
    vb[2] = v;
    float *vc = malloc(3*sizeof(float));
    vc[0] = -v;
    vc[1] = v;
    vc[2] = -v;
    float *vd = malloc(3*sizeof(float));
    vd[0] = v;
    vd[1] = -v;
    vd[2] = -v;

    int *t0 = malloc(3*sizeof(int));
    t0[0] = 0;
    t0[1] = 1;
    t0[2] = 2;
    int *t1 = malloc(3*sizeof(int));
    t1[0] = 0;
    t1[1] = 3;
    t1[2] = 1;
    int *t2 = malloc(3*sizeof(int));
    t2[0] = 0;
    t2[1] = 2;
    t2[2] = 3;
    int *t3 = malloc(3*sizeof(int));
    t3[0] = 1;
    t3[1] = 3;
    t3[2] = 2;

    zarray_t *verts = zarray_create(sizeof(float*));
    zarray_t *tris = zarray_create(sizeof(int*));

    zarray_add(verts, &va);
    zarray_add(verts, &vb);
    zarray_add(verts, &vc);
    zarray_add(verts, &vd);

    zarray_add(tris, &t0);
    zarray_add(tris, &t1);
    zarray_add(tris, &t2);
    zarray_add(tris, &t3);

    // Iteratively refine the sphere
    for (int i = 0; i < quality; i++) {
        zarray_t *new_tris = zarray_create(sizeof(int*));

        // Subdivide each old triangle into 4 new ones. This involves
        // making 3 new vertices at the midpoints of the existing edges
        for (int j = 0; j < zarray_size(tris); j++) {
            int *tri = NULL;
            zarray_get(tris, j, &tri);
            float *va = NULL, *vb = NULL, *vc = NULL;
            zarray_get(verts, tri[0], &va);
            zarray_get(verts, tri[1], &vb);
            zarray_get(verts, tri[2], &vc);

            // Create new vertices
            float *temp = malloc(3*sizeof(float));
            add(va, vb, temp, 3);
            float *vab = calloc(3,sizeof(float));
            normalize(temp, vab, 3);
            add(vb, vc, temp, 3);
            float *vbc = calloc(3,sizeof(float));
            normalize(temp, vbc, 3);
            add(va, vc, temp, 3);
            float *vac = calloc(3,sizeof(float));
            normalize(temp, vac, 3);
            free(temp);

            // Add new vertices
            int sz = zarray_size(verts);
            zarray_add(verts, &vab);
            zarray_add(verts, &vbc);
            zarray_add(verts, &vac);

            // Make new triangles
            int *t0 = malloc(3*sizeof(int));
            t0[0] = tri[0];         // va
            t0[1] = sz;             // vab
            t0[2] = sz+2;           // vac
            zarray_add(new_tris, &t0);
            int *t1 = malloc(3*sizeof(int));
            t1[0] = sz;             // vab
            t1[1] = tri[1];         // vb
            t1[2] = sz+1;           // vbc
            zarray_add(new_tris, &t1);
            int *t2 = malloc(3*sizeof(int));
            t2[0] = sz+2;           // vac
            t2[1] = sz;             // vab
            t2[2] = sz+1;           // vbc
            zarray_add(new_tris, &t2);
            int *t3 = malloc(3*sizeof(int));
            t3[0] = tri[2];         // vc
            t3[1] = sz+2;           // vac
            t3[2] = sz+1;           // vbc
            zarray_add(new_tris, &t3);
        }

        // Cleanup old triangles
        free_array(tris);
        zarray_destroy(tris);
        tris = new_tris;
    }

    // Create resources
    // XXX TODO: Texture mapping
    NVERTS = zarray_size(verts);
    float *_verts = malloc(3*NVERTS*sizeof(float));
    for (int i = 0; i < NVERTS; i++) {
        float *v = NULL;
        zarray_get(verts, i, &v);
        _verts[3*i+0] = v[0];
        _verts[3*i+1] = v[1];
        _verts[3*i+2] = v[2];
    }

    NTRIS = zarray_size(tris);
    uint32_t *_tri_indices = malloc(3*NTRIS*sizeof(uint32_t));
    for (int i = 0; i < NTRIS; i++) {
        int *tri = NULL;
        zarray_get(tris, i, &tri);
        _tri_indices[3*i+0] = (uint32_t)tri[0];
        _tri_indices[3*i+1] = (uint32_t)tri[1];
        _tri_indices[3*i+2] = (uint32_t)tri[2];
    }

    // Lines...currently doubles them up
    uint32_t *_line_indices = malloc(3*2*NTRIS*sizeof(uint32_t));
    for (int i = 0; i < NTRIS; i++) {
        int *tri = NULL;
        zarray_get(tris, i, &tri);
        _line_indices[3*2*i+0] = (uint32_t)tri[0];
        _line_indices[3*2*i+1] = (uint32_t)tri[1];
        _line_indices[3*2*i+2] = (uint32_t)tri[1];
        _line_indices[3*2*i+3] = (uint32_t)tri[2];
        _line_indices[3*2*i+4] = (uint32_t)tri[2];
        _line_indices[3*2*i+5] = (uint32_t)tri[0];
    }

    vertex_points = vx_resc_copyf(_verts, 3*NVERTS);
    tri_indices = vx_resc_copyui(_tri_indices, 3*NTRIS);
    //tri_normals = vx_resc_copyf(_verts, 3*NVERTS);
    line_indices = vx_resc_copyui(_line_indices, 3*2*NTRIS);

    vx_resc_inc_ref(vertex_points);
    vx_resc_inc_ref(tri_indices);
    //vx_resc_inc_ref(tri_normals);
    vx_resc_inc_ref(line_indices);

    // Clean up
    free_array(verts);
    free_array(tris);
    zarray_destroy(verts);
    zarray_destroy(tris);
    free(_verts);
    free(_tri_indices);
}

static void vxo_sphere_destroy(void *ignored)
{
    vx_resc_dec_destroy(vertex_points);
    vx_resc_dec_destroy(tri_indices);
    //vx_resc_dec_destroy(tri_normals);
    vx_resc_dec_destroy(line_indices);
}

vx_object_t* _vxo_sphere_private(vx_style_t *style, ...)
{
    // Make sure the static geometry is initialized safely, correctly, and quickly
    if (vertex_points == NULL) {
        pthread_mutex_lock(&vx_convenience_mutex);
        if (vertex_points == NULL) {
            vxo_sphere_init(4);
            vx_global_register_destroy(vxo_sphere_destroy, NULL);
        }
        pthread_mutex_unlock(&vx_convenience_mutex);
    }

    // Apply styles
    vx_object_t * vc = vxo_chain_create();

    va_list va;
    va_start(va, style);

    for (vx_style_t * sty = style; sty != NULL; sty = va_arg(va, vx_style_t *)) {

        switch(sty->type) {
            case VXO_POINTS_STYLE:
                vxo_chain_add(vc, vxo_points(vertex_points, NVERTS, sty));
                break;
            case VXO_LINES_STYLE:
                vxo_chain_add(vc, vxo_lines_indexed(vertex_points, NVERTS, line_indices, GL_LINES, sty));
                break;
            case VXO_MESH_STYLE:
                vxo_chain_add(vc, vxo_mesh_indexed(vertex_points, NVERTS,
                                                   vertex_points,
                                                   tri_indices, GL_TRIANGLES,
                                                   sty));
                break;
        }
    }
    va_end(va);

    return vc;
}
