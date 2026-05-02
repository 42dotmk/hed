#ifndef CURSOR_H
#define CURSOR_H

#include "lib/vector.h"

typedef struct {
    int x;
    int y;
} Cursor;

/* Heap-allocated cursor list — entries are Cursor*, so callers can
 * hold stable pointers across vector growth. */
VEC_DEFINE(CursorVec, Cursor *);

#endif /* CURSOR_H */
