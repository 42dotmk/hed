#ifndef VECTOR_H
#define VECTOR_H

#include <stddef.h>

/*
 * GENERIC DYNAMIC VECTOR
 * ======================
 *
 * A type-safe growable array that automatically resizes.
 *
 * Usage:
 *   typedef struct { int x; } Item;
 *   VEC_DEFINE(ItemVec, Item);  // Defines ItemVec type
 *
 *   ItemVec vec = vec_new(Item);  // Create empty vector
 *   vec_push(&vec, Item, item);   // Add element
 *   Item *it = vec_get(&vec, Item, index);  // Access element
 *   vec_free(&vec, Item);  // Free memory
 *
 * Benefits:
 *   - No fixed limits
 *   - Automatic growth (doubles capacity when full)
 *   - Type-safe macros
 *   - Efficient memory usage
 */

/* Generic vector structure */
typedef struct {
    void *data;       /* Pointer to elements */
    size_t len;       /* Number of elements */
    size_t cap;       /* Allocated capacity */
    size_t elem_size; /* Size of each element */
} Vector;

/* Define a typed vector */
#define VEC_DEFINE(name, type)                                                 \
    typedef struct {                                                           \
        type *data;                                                            \
        size_t len;                                                            \
        size_t cap;                                                            \
    } name

/* Initialize a new vector */
#define vec_new(type) ((Vector){NULL, 0, 0, sizeof(type)})

/* Free vector memory */
#define vec_free(vec, type)                                                    \
    do {                                                                       \
        if ((vec)->data) {                                                     \
            free((vec)->data);                                                 \
            (vec)->data = NULL;                                                \
        }                                                                      \
        (vec)->len = 0;                                                        \
        (vec)->cap = 0;                                                        \
    } while (0)

/* Reserve capacity for typed vectors (internal helper) */
static inline int vec_reserve_typed(void *vec_ptr, size_t new_cap,
                                    size_t elem_size) {
    /* Works with typed vectors like BufferVec, WindowVec */
    typedef struct {
        void *data;
        size_t len;
        size_t cap;
    } GenericVec;

    GenericVec *vec = (GenericVec *)vec_ptr;
    if (!vec || new_cap <= vec->cap)
        return 1;

    void *new_data = realloc(vec->data, new_cap * elem_size);
    if (!new_data)
        return 0; /* OOM */

    vec->data = new_data;
    vec->cap = new_cap;
    return 1;
}

/* Reserve capacity (internal helper for untyped Vector) */
static inline int vec_reserve_internal(Vector *vec, size_t new_cap) {
    if (!vec || new_cap <= vec->cap)
        return 1;

    void *new_data = realloc(vec->data, new_cap * vec->elem_size);
    if (!new_data)
        return 0; /* OOM */

    vec->data = new_data;
    vec->cap = new_cap;
    return 1;
}

/* Ensure capacity for at least one more element */
static inline int vec_ensure_cap(Vector *vec) {
    if (vec->len < vec->cap)
        return 1;

    size_t new_cap = vec->cap == 0 ? 8 : vec->cap * 2;
    return vec_reserve_internal(vec, new_cap);
}

/* Push an element (grows if needed) */
#define vec_push(vec, type, value)                                             \
    do {                                                                       \
        if (!vec_ensure_cap((Vector *)(vec)))                                  \
            break;                                                             \
        ((type *)(vec)->data)[(vec)->len++] = (value);                         \
    } while (0)

/* Get element at index (no bounds checking) */
#define vec_get(vec, type, index) (&((type *)(vec)->data)[index])

/* Get element at index with bounds checking */
#define vec_get_safe(vec, type, index)                                         \
    ((index) < (vec)->len ? &((type *)(vec)->data)[index] : NULL)

/* Remove element at index (shifts remaining elements left) */
#define vec_remove(vec, type, index)                                           \
    do {                                                                       \
        if ((index) < (vec)->len) {                                            \
            for (size_t _i = (index); _i < (vec)->len - 1; _i++) {             \
                ((type *)(vec)->data)[_i] = ((type *)(vec)->data)[_i + 1];     \
            }                                                                  \
            (vec)->len--;                                                      \
        }                                                                      \
    } while (0)

/* Clear vector (keeps capacity) */
#define vec_clear(vec) ((vec)->len = 0)

/* Reserve specific capacity */
#define vec_reserve(vec, type, capacity)                                       \
    vec_reserve_internal((Vector *)(vec), (capacity))

#endif /* VECTOR_H */
