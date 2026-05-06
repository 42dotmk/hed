#ifndef HED_LIB_VECTOR_H
#define HED_LIB_VECTOR_H

#include "stb_ds.h"

/* Zero the length of an stb_ds vector while keeping its capacity, so the
 * next push doesn't realloc. Sidesteps the always-false comparison gcc
 * emits when stb_ds's arrsetlen(x, 0) macro expands to `cap < (size_t)0`. */
#define arr_reset(a) \
    do { if (a) stbds_header(a)->length = 0; } while (0)

#endif
