#ifndef __VARRAY_H__
#define __VARRAY_H__

typedef struct varray varray_t;

#ifdef __cplusplus
extern "C" {
#endif

varray_t *
varray_create (void);

void
varray_destroy (varray_t *va);

int
varray_size (varray_t *va);

void
varray_add (varray_t *va, void *p);

void *
varray_get (varray_t *va, int idx);

// remove the idx'th element, returning the element that was removed
// and moving all subsequent entries down one index position.
void *
varray_remove (varray_t *va, int idx);

// remove the idx'th element, returning the element that was removed
// and moving the last entry of the array to the newly-empty position.
void *
varray_remove_shuffle (varray_t *va, int idx);

// search the array for the specified value and remove it.
void
varray_remove_value (varray_t *va, void *d);

// apply function f() to each element of the varray. Useful for
// freeing the elements. Do not modify the contents of the varray from
// within the callback function.
void
varray_map (varray_t *va, void (*f)());

void
varray_sort (varray_t *va, int (*compare)(const void *, const void *));

#ifdef __cplusplus
}
#endif

#endif //__VARRAY_H__
