#ifndef CURSOR_H
#define CURSOR_H

#include "stb_ds.h"

typedef struct {
    int x;
    int y;
} Cursor;

/* Heap-allocated cursor list — entries are Cursor*, so callers can
 * hold stable pointers across vector growth. Stored as stb_ds dynamic
 * array of `Cursor *` (use arrlen / arrput / arrfree). */
typedef Cursor **CursorVec;

#endif /* CURSOR_H */
