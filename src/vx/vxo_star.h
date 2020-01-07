#ifndef VXO_STAR_H_
#define VXO_STAR_H_

#include "vx_object.h"
#include "vx_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#define vxo_star(...) _vxo_star_private(__VA_ARGS__, NULL)
vx_object_t * _vxo_star_private(vx_style_t * style, ...) __attribute__((sentinel));

#ifdef __cplusplus
}
#endif

#endif /* VXO_STAR_H_ */
