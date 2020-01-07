#include "vx_matrix_stack.h"
#include "common/zarray.h"
#include "string.h"
#include "stdlib.h"

struct vx_matrix_stack {

    zarray_t * stack;
    double M[16];
};

// C = A*B
static void mult44(const double * A, const double * B, double * C)
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            double acc = 0.0f;
            for (int k = 0; k < 4; k++)
                acc += A[i*4 + k] * B[k*4 + j];
            C[i*4 +j] = acc;
        }
    }
}

void vx_matrix_stack_get(vx_matrix_stack_t * ms, double * out44)
{
    memcpy(out44, ms->M, 16*sizeof(double));
}

void vx_matrix_stack_getf(vx_matrix_stack_t * ms, float * out44)
{
    for (int i = 0; i < 16; i++)
        out44[i] = (float) ms->M[i];
}

void vx_matrix_stack_ident(vx_matrix_stack_t * ms)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            ms->M[i*4+j] = (i == j ? 1.0 : 0.0);
}

vx_matrix_stack_t * vx_matrix_stack_create()
{
    vx_matrix_stack_t * ms = calloc(1, sizeof(vx_matrix_stack_t));
    ms->stack = zarray_create(16*sizeof(double));
    vx_matrix_stack_ident(ms);
    return ms;
}

void vx_matrix_stack_destroy(vx_matrix_stack_t * ms)
{
    zarray_destroy(ms->stack);
    free(ms);
}

void vx_matrix_stack_set(vx_matrix_stack_t * ms, const double * in44)
{
    for (int i = 0; i < 16; i++)
        ms->M[i] = in44[i];
}

void vx_matrix_stack_setf(vx_matrix_stack_t * ms, const float * _in44)
{
    for (int i = 0; i < 16; i++)
        ms->M[i] = (double)_in44[i];
}


void vx_matrix_stack_mult(vx_matrix_stack_t * ms, const double * in44)
{
    double temp[16];
    memcpy(temp, ms->M, 16*sizeof(double)); // from M to temp
    mult44(temp, in44, ms->M); // mult equals
}

void vx_matrix_stack_multf(vx_matrix_stack_t * ms, const float * _in44)
{
    double in44[16];
    for (int i = 0; i < 16; i++)
        in44[i] = _in44[i];

    vx_matrix_stack_mult(ms, in44);
}

void vx_matrix_stack_push(vx_matrix_stack_t * ms)
{
    zarray_add(ms->stack, &ms->M);
}

void vx_matrix_stack_pop(vx_matrix_stack_t * ms)
{
    // copy data out
    zarray_get(ms->stack, zarray_size(ms->stack)-1, &ms->M);
    zarray_remove_index(ms->stack, zarray_size(ms->stack)-1, 0); // prune
}
