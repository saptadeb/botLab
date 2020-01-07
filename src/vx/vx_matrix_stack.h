#ifndef __VX_MATRIX_STACK_
#define __VX_MATRIX_STACK_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vx_matrix_stack vx_matrix_stack_t;

// memory management: copy everything!
// All matrices are row major!
void vx_matrix_stack_ident(vx_matrix_stack_t * ms);
void vx_matrix_stack_mult(vx_matrix_stack_t * ms, const double * in44);
void vx_matrix_stack_multf(vx_matrix_stack_t * ms, const float * _in44);
void vx_matrix_stack_set(vx_matrix_stack_t * ms, const double * in44);
void vx_matrix_stack_setf(vx_matrix_stack_t * ms, const float * _in44);

void vx_matrix_stack_push(vx_matrix_stack_t * ms);
void vx_matrix_stack_pop(vx_matrix_stack_t * ms);
void vx_matrix_stack_get(vx_matrix_stack_t * ms, double * out44);
void vx_matrix_stack_getf(vx_matrix_stack_t * ms, float * out44);

vx_matrix_stack_t * vx_matrix_stack_create();
void vx_matrix_stack_destroy(vx_matrix_stack_t * ms);

#ifdef __cplusplus
}
#endif

#endif
