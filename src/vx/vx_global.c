#include "vx_global.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "common/zarray.h"

typedef struct
{
    void(*destroy)(void * data);
    void * data;

} destroy_listener_t;


static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
static zarray_t * destroy_listeners = NULL; // <destroy_listener_t>

// Initialize the global state to allow vx to be fully threadsafe
void vx_global_init()
{
    printf("NFO: vx_global_init() is deprecated\n");
    /*
    // allow multiple initializations
    if (destroy_listeners != NULL)
        return;

    destroy_listeners = zarray_create(sizeof(destroy_listener_t*));
    pthread_mutex_init(&global_vx_mutex, NULL);
    pthread_mutex_init(&list_mutex, NULL);
    */
}

void vx_global_register_destroy(void(*destroy)(void * data), void * data)
{
    pthread_mutex_lock(&list_mutex);
    if (destroy_listeners == NULL)
        destroy_listeners = zarray_create(sizeof(destroy_listener_t*));

    destroy_listener_t * l = calloc(1, sizeof(destroy_listener_t));
    l->destroy = destroy;
    l->data = data;
    zarray_add(destroy_listeners, &l);
    pthread_mutex_unlock(&list_mutex);
}

void vx_global_destroy()
{
    if (destroy_listeners == NULL)
        return;

    pthread_mutex_lock(&list_mutex);
    for (int i = 0; i < zarray_size(destroy_listeners); i++) {
        destroy_listener_t * l = NULL;
        zarray_get(destroy_listeners, i, &l);
        l->destroy(l->data);
        free(l);
    }
    zarray_destroy(destroy_listeners);
    destroy_listeners = NULL;
    pthread_mutex_unlock(&list_mutex);

    //pthread_mutex_destroy(&global_vx_mutex);
}
