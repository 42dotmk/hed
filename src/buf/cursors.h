#ifndef BUF_CURSORS_H
#define BUF_CURSORS_H

#include "buf/buffer.h"

/*
 * Multi-cursor bookkeeping (implemented in cursors.c). The public
 * cursor API (buf_cursor_add & co, buf_cursors_bind_window) is
 * declared in buffer.h; this header carries the buf/-internal shift
 * hooks the row mutators in buffer.c call after each edit so every
 * cursor — live and parked — stays glued to its text.
 */

void cursors_after_insert_char(Buffer *buf, int iy, int ix);
void cursors_after_delete_char(Buffer *buf, int iy, int ix);
void cursors_after_insert_newline(Buffer *buf, int iy, int ix);
void cursors_after_join_lines(Buffer *buf, int iy, int join_at);
void cursors_after_delete_line(Buffer *buf, int iy);

#endif
