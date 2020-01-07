#ifndef VXO_OBJMTL_H
#define VXO_OBJMTL_H

#include "vx_object.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a vx object from an Alias/Wavefront .obj file. This function
 * call is somewhat expensive, so it's best to cache the resulting
 * vx_object_t if it's going to be reused.
 *
 * NOTE: this implementation may not be 100% compliant. It may also be
 * desirable to create an intermediate representation which allows
 * callers to modify the appearance of the materials, or to specify
 * materials if none exist. A second function could be used to convert
 * to a vx_object_t
 *
 * On error, this function will either return null (e.g. file not found)
 *  or possibly assert() if the file format is not as expected
 */
vx_object_t * vxo_objmtl(const char * obj_filename);

/**
 * Load an object model, as above, but return a 2D approximation of the model.
 * outline_num_points - number of outline points. e.g. 360
 */
vx_object_t * vxo_objmtl_outline(const char * obj_filename, int outline_num_points);

#ifdef __cplusplus
}
#endif

#endif
