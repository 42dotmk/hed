#ifndef UNDO_H
#define UNDO_H

#include "cursor.h"
#include "sizedstr.h"
#include <stddef.h>

/*
 * Per-buffer undo/redo with per-row diff records grouped by command.
 *
 * Model
 * -----
 * - A "group" is one logical edit (one command, one insert session, …).
 * - A group holds an ordered list of records, each describing a change to
 *   exactly one row.
 * - Records are produced inside the buffer mutation primitives, so most
 *   callers don't need to enumerate the rows they touched.
 * - Undo applies a group's records in reverse; redo applies in original
 *   order.
 *
 * Lifecycle
 * ---------
 *   undo_begin(buf, "desc")     -- explicit group open
 *   ...mutation calls record...
 *   undo_end(buf)               -- close + push to undo stack
 *
 * If a record arrives without an explicit group open, an implicit "auto"
 * group is opened. The keypress dispatcher closes any open group at the
 * top of normal/visual-mode dispatch so each command becomes one undo.
 *
 * Cursor
 * ------
 * v1 does not restore cursor position on undo/redo. The cursor stays
 * where the row mutations left it.
 */

struct Buffer;

typedef enum {
    UR_REPLACE = 1, /* swap row.chars with rec.data */
    UR_INSERT  = 2, /* on do: insert row at idx with data; on undo: delete  */
    UR_DELETE  = 3  /* on do: delete row at idx; on undo: insert with data  */
} UndoKind;

typedef struct {
    UndoKind kind;
    int      row_idx;
    SizedStr data;
} UndoRec;

typedef struct UndoGroup {
    UndoRec *recs;
    int      len;
    int      cap;
    char     desc[24];
} UndoGroup;

typedef struct {
    UndoGroup **undo;
    int         undo_len, undo_cap;
    UndoGroup **redo;
    int         redo_len, redo_cap;
    UndoGroup  *open;
    int         applying; /* set while undo_apply / redo_apply is running */
} UndoState;

#define UNDO_MAX_DEPTH 500

void undo_state_init(UndoState *u);
void undo_state_free(UndoState *u);

/* Group lifecycle. begin closes any prior open group first. */
void undo_begin(struct Buffer *buf, const char *desc);
void undo_end(struct Buffer *buf);
int  undo_has_open(const struct Buffer *buf);
int  undo_is_applying(const struct Buffer *buf);

/* Per-row capture, called BEFORE the mutation by buffer primitives.
 * If no group is open, an implicit "auto" group is opened. */
void undo_record_replace(struct Buffer *buf, int row_idx);
void undo_record_insert(struct Buffer *buf, int row_idx,
                        const char *data, size_t len);
void undo_record_delete(struct Buffer *buf, int row_idx,
                        const char *data, size_t len);

/* Apply. Returns 1 on success, 0 if nothing to do. */
int undo_apply(struct Buffer *buf);
int redo_apply(struct Buffer *buf);

/* Register undo's mode-change hook (opens insert group on entry, closes
 * on exit). Call from config_init. */
void undo_register_hooks(void);

#endif /* UNDO_H */
