#ifndef VECTOR_H
#define VECTOR_H

#include <stddef.h>
#include <stdlib.h>

/*
 * GENERIC DYNAMIC VECTOR
 * ======================
 *
 * A type-safe growable array that automatically resizes.
 *
 * Two vector types are available:
 *
 * 1. Generic Vector (for ad-hoc usage):
 *    Vector vec = vec_new(int);
 *    vec_push(&vec, int, value);
 *    int val = vec_pop(&vec, int);
 *    vec_free(&vec, int);
 *
 * 2. Typed Vector (for struct members, recommended):
 *    VEC_DEFINE(IntVec, int);
 *    IntVec vec = {0};
 *    vec_push_typed(&vec, int, value);
 *    int val = vec_pop_typed(&vec, int);
 *    free(vec.data);
 *
 * Available operations (both types):
 *   - vec_push[_typed] - Add element to end
 *   - vec_pop[_typed] - Remove and return last element
 *   - vec_push_start[_typed] - Add element to beginning
 *   - vec_pop_start[_typed] - Remove and return first element
 *   - vec_get, vec_get_safe - Access elements
 *   - vec_remove - Remove element at index
 *   - vec_find - Find element matching predicate
 *
 * Benefits:
 *   - No fixed limits
 *   - Automatic growth (doubles capacity when full)
 *   - Type-safe macros
 *   - Efficient memory usage
 *   - Stack and queue operations (push/pop from both ends)
 */

/* Generic vector structure */
typedef struct {
    void *data;       /* Pointer to elements */
    size_t len;       /* Number of elements */
    size_t cap;       /* Allocated capacity */
    size_t elem_size; /* Size of each element */
} Vector;

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
static inline int __vec_reserve(Vector *vec, size_t new_cap) {
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
    return __vec_reserve(vec, new_cap);
}

/* Push an element (grows if needed) */
#define vec_push(vec, type, value)                                             \
    do {                                                                       \
        if (!vec_ensure_cap((Vector *)(vec)))                                  \
            break;                                                             \
        ((type *)(vec)->data)[(vec)->len++] = (value);                         \
    } while (0)

/* Pop and return the last element (returns default-initialized value if empty) */
#define vec_pop(vec, type)                                                     \
    __extension__ ({                                                           \
        type __result = {0};                                                   \
        if ((vec)->len > 0) {                                                  \
            __result = ((type *)(vec)->data)[--(vec)->len];                    \
        }                                                                      \
        __result;                                                              \
    })

/* Push an element at the beginning (shifts all elements right) */
#define vec_push_start(vec, type, value)                                       \
    do {                                                                       \
        if (!vec_ensure_cap((Vector *)(vec)))                                  \
            break;                                                             \
        for (size_t _i = (vec)->len; _i > 0; _i--) {                           \
            ((type *)(vec)->data)[_i] = ((type *)(vec)->data)[_i - 1];         \
        }                                                                      \
        ((type *)(vec)->data)[0] = (value);                                    \
        (vec)->len++;                                                          \
    } while (0)

/* Pop and return the first element (shifts remaining elements left) */
#define vec_pop_start(vec, type)                                               \
    __extension__ ({                                                           \
        type __result = {0};                                                   \
        if ((vec)->len > 0) {                                                  \
            __result = ((type *)(vec)->data)[0];                               \
            for (size_t _i = 0; _i < (vec)->len - 1; _i++) {                   \
                ((type *)(vec)->data)[_i] = ((type *)(vec)->data)[_i + 1];     \
            }                                                                  \
            (vec)->len--;                                                      \
        }                                                                      \
        __result;                                                              \
    })

/* Typed vector helpers (for VEC_DEFINE'd types like BufferVec, QfItemVec) */
/* These use vec_reserve_typed instead of vec_ensure_cap */

/* Push element to typed vector (use for VEC_DEFINE'd types) */
#define vec_push_typed(vec, type, value)                                       \
    do {                                                                       \
        if (!vec_reserve_typed((vec), (vec)->len + 1, sizeof(type)))           \
            break;                                                             \
        (vec)->data[(vec)->len++] = (value);                                   \
    } while (0)

/* Pop from typed vector */
#define vec_pop_typed(vec, type)                                               \
    __extension__ ({                                                           \
        type __result = {0};                                                   \
        if ((vec)->len > 0) {                                                  \
            __result = (vec)->data[--(vec)->len];                              \
        }                                                                      \
        __result;                                                              \
    })

/* Push element to beginning of typed vector */
#define vec_push_start_typed(vec, type, value)                                 \
    do {                                                                       \
        if (!vec_reserve_typed((vec), (vec)->len + 1, sizeof(type)))           \
            break;                                                             \
        for (size_t _i = (vec)->len; _i > 0; _i--) {                           \
            (vec)->data[_i] = (vec)->data[_i - 1];                             \
        }                                                                      \
        (vec)->data[0] = (value);                                              \
        (vec)->len++;                                                          \
    } while (0)

/* Pop from beginning of typed vector */
#define vec_pop_start_typed(vec, type)                                         \
    __extension__ ({                                                           \
        type __result = {0};                                                   \
        if ((vec)->len > 0) {                                                  \
            __result = (vec)->data[0];                                         \
            for (size_t _i = 0; _i < (vec)->len - 1; _i++) {                   \
                (vec)->data[_i] = (vec)->data[_i + 1];                         \
            }                                                                  \
            (vec)->len--;                                                      \
        }                                                                      \
        __result;                                                              \
    })

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
    __vec_reserve((Vector *)(vec), (capacity))

/* Find first element matching predicate. Predicate can use __elem pointer. */
#define vec_find(vec, type, predicate, out_index)                              \
    __extension__ ({                                                           \
        type *__result = NULL;                                                 \
        size_t __idx = 0;                                                      \
        for (; __idx < (vec)->len; __idx++) {                                  \
            type *__elem = &((vec)->data[__idx]);                              \
            if (predicate) {                                                   \
                __result = __elem;                                             \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        if ((out_index) != NULL) {                                             \
            *(out_index) = __result ? __idx : (size_t)-1;                      \
        }                                                                      \
        __result;                                                              \
    })

#endif /* VECTOR_H */
