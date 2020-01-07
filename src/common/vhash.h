#ifndef __VHASH_H__
#define __VHASH_H__

#include <stdint.h>

struct vhash_element {
    void *key;
    void *value;
    struct vhash_element *next;
};

typedef struct {
    uint32_t(*hash)(const void *a);

    // returns 1 if equal
    int(*equals)(const void *a, const void *b);

    int alloc;
    int size;

    struct vhash_element **elements;
} vhash_t;

typedef struct {
    int bucket;
    struct vhash_element *el;
} vhash_iterator_t;

typedef struct {
    void *key;
    void *value;
} vhash_pair_t;

#ifdef __cplusplus
extern "C" {
#endif

vhash_t *
vhash_create (uint32_t(*hash)(const void *a), int(*equals)(const void *a, const void *b));

void
vhash_destroy (vhash_t *vh);

void *
vhash_get (vhash_t *vh, const void *key);

// the old key will be retained in preference to the new key.
void
vhash_put (vhash_t *vh, void *key, void *value);

void
vhash_iterator_init (vhash_t *vh, vhash_iterator_t *vit);

void *
vhash_iterator_next_key (vhash_t *vh, vhash_iterator_t *vit);

// returns the removed element pair, which can be used for deallocation.
vhash_pair_t
vhash_remove (vhash_t *vh, void *key);

/////////////////////////////////////////////////////
// Functions for string-typed keys
uint32_t
vhash_str_hash (const void *a);

int
vhash_str_equals (const void *a, const void *b);

/////////////////////////////////////////////////////
// Functions for keys of type uint32_t.  These methods cast the keys
// into the pointer, and thus assume that the size of a pointer is
// larger than 4 bytes.
// When calling put/get/remove, the key should be cast to (void*). e.g.,
// uint32_t key = 0x1234;
// vhash_put((void*) key, value);

uint32_t
vhash_uint32_hash (const void *a);

int
vhash_uint32_equals (const void *_a, const void *_b);

#ifdef __cplusplus
}
#endif

#endif //__VHASH_H__
