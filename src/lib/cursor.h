#ifndef CURSOR_H
#define CURSOR_H

#include "vector.h"

typedef struct {
    int x;
    int y;
} Cursor;

/* Vector of cursors for multi-cursor support */
VEC_DEFINE(CursorVec, Cursor);

#endif /* CURSOR_H */
