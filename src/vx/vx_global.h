#ifndef VX_GLOBAL_H
#define VX_GLOBAL_H

// Initialize the global state to allow vx to be fully threadsafe
// This a thin wrapper around a pthread mutex, which allows any other module
// to safely initialize any global state. The two current usecases for this are:
//   1. initialize the program library
//   2. start the gl thread singleton
// Mainly, these will use the global lock to answer the question:
// "Am I the first thread to arrive at the initialization code. If so,
//  set a flag so no one runs the initialization code twice"
// This ensures that if, for some reason, vx is being used from
// multiple threads, it will initialize properly

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// Deprecated
void vx_global_init();

// Convenience mutex used for protecting initialization of reusable vertex
// geometries or shader source code
static pthread_mutex_t vx_convenience_mutex __attribute__((__unused__)) = PTHREAD_MUTEX_INITIALIZER;

// vx internal modules call this to run shutdown code when 'vx_global_destroy()' is called
void vx_global_register_destroy(void(*destroy)(), void * data);
void vx_global_destroy();

#ifdef __cplusplus
}
#endif

#endif
