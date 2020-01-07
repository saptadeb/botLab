#ifndef VX_CHAIN_H
#define VX_CHAIN_H

#include "vx_object.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vxo_chain vxo_chain_t;
struct vxo_chain
{
    vx_object_t vxo;

    zarray_t *objs;
};

// Macros for creating/adding-to a chain (with objects)
# define vxo_chain(...) _vxo_chain_create_varargs_private(__VA_ARGS__, NULL)
# define vxo_chain_add(vc, ...) _vxo_chain_add_varargs_private(vc, __VA_ARGS__, NULL)

// Use this call if you just want to create an empty chain
vx_object_t * vxo_chain_create();

// NOTE: Both of the var args calls require the use of a NULL 'sentinel' at the end of the argument list.
//       Most users should just use the macros (defined above), which automatically insert it.
//       e.g. vxo_chain_create_varargs(vo1, vo2, vo3);
vx_object_t * _vxo_chain_create_varargs_private(vx_object_t * first, ...);
void _vxo_chain_add_varargs_private(vx_object_t * chain, vx_object_t * first, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif
