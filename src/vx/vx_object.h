#ifndef __VX_OBJECT_
#define __VX_OBJECT_

#include <assert.h>

#include "vx_types.h"
#include "vx_code_output_stream.h"
#include "common/zhash.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vx_object
{
    void (*append)(vx_object_t * obj, zhash_t * resources, vx_code_output_stream_t * output);
    void * impl;

    // Reference counting:
    uint32_t refcnt; // how many places have a reference to this?
    void (*destroy)(vx_object_t * vo); // Destroy this object, and release all resources.
};

// Note: It is illegal to create a cycle of references with vx_objects. Not only will this
// break the reference counting, it would also result in infinitely long rendering codes

// Note: Subclasses must be sure to properly initialize the reference count to zero, e.g.:
//   vx_object_t * vo = calloc(1,sizeof(vx_object_t));

// Decrements the reference count of the supplied object, signifying that the
//    caller is no longer maintaining an independent reference to the object,
//    thus allowing vx's garbage-collector to free the associated resources once
//    it is no longer needed by any other processes. The supplied object should
//    not be referenced after this call, as it may be destroyed at any time.
static inline void vx_object_dec_destroy(vx_object_t * obj)
{
    assert(obj->refcnt > 0);

    uint32_t has_ref = __sync_sub_and_fetch(&obj->refcnt, 1);

    if (!has_ref)
        obj->destroy(obj);

    /* assert(obj->rcts > 0); */
    /* obj->rcts--; */
    /* if (obj->rcts == 0) { */
    /*     // Implementer must guarantee to release all resources, including */
    /*     // decrementing any reference counts it may have been holding */
    /*     obj->destroy(obj); */
    /* } */
}


// Increments the reference count of the supplied object, which implies that the
//   caller is maintaining an independent reference. This will prevent vx's
//   garbage-collector from freeing the resources associated with the object.
// It is the caller's responsibility to call vx_object_dec_destroy() when the
//   object is no longer needed to free the associated resources, even if the
//   vx world that it has been added to has been destroyed.
static inline void vx_object_inc_ref(vx_object_t * obj)
{
    __sync_add_and_fetch(&obj->refcnt, 1);
}

#ifdef __cplusplus
}
#endif

#endif
