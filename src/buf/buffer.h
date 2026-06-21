#ifndef BUFFER_H
#define BUFFER_H

#define CURSOR_STYLE_NONE "\x1b[0 q"
#define CURSOR_STYLE_BLOCK "\x1b[1 q"
#define CURSOR_STYLE_UNDERLINE "\x1b[3 q"
#define CURSOR_STYLE_BEAM "\x1b[5 q"

#include "lib/cursor.h"
#include "lib/errors.h"
#include "buf/attrspan.h"
#include "buf/row.h"
#include "buf/virtual_text.h"
#include "utils/fold.h"
#include "utils/undo.h"

struct Window; /* ui/window.h — cursor sets are keyed by window id */

/* One multicursor set, owned by a (buffer, window) pair. The set a
 * focused window is working with lives in Buffer.all_cursors/cursor;
 * sets belonging to other windows showing the same buffer are parked
 * here until their window becomes the focused pair again. */
typedef struct CursorSet {
    int win_id;       /* owning Window.id */
    CursorVec cursors;
    Cursor *active;   /* points into cursors */
} CursorSet;

/* Buffer structure - represents a single file/document */
typedef struct Buffer {
    Row *rows;
    int num_rows;
    /* all_cursors holds every cursor (incl. the active one) as heap-
     * allocated entries, so plugins/collab layers can keep stable
     * Cursor* refs. cursor points to one element of all_cursors.data.
     *
     * This is the LIVE cursor set of the (buffer, window) pair named by
     * cursor_win_id. Sets of other windows showing this buffer wait in
     * cursor_sets; buf_cursors_bind_window() swaps them in and out. If
     * the owning window disappears, the live set stays put — the buffer
     * keeps the cursors of its last window and the next window that
     * shows it adopts them. */
    CursorVec all_cursors;
    Cursor *cursor;
    int cursor_win_id;      /* Window.id owning the live set; 0 = none */
    CursorSet *cursor_sets; /* stb_ds array of parked sets */
    char *cursor_style;

    char *filename;
    char *title; /* Display title: filename or "[No Name]" */
    char *filetype;
    int dirty;
    int readonly; /* Read-only flag (default: 0/false) */

    FoldList folds; /* Code folding regions */
    /* Active fold detection method as a registry name (heap-owned).
     * NULL means "not yet chosen" — the BUFFER_OPEN hook may apply a
     * filetype default. Plugin-registered methods round-trip here. */
    char *fold_method;
    /* Last fold level applied by the <S-Tab> cycle (0=all closed,
     * 100=all open). Seeds the next step of the 1→2→100→0 cycle. */
    int fold_level;

    UndoState undo; /* Undo/redo state */

    VtTable vtext; /* Virtual text annotations (display-only) */

    /* Per-frame attribute spans, populated by HOOK_RENDER_PRE handlers
     * and consumed by the renderer. Cleared at the start of each frame. */
    AttrSpans render_spans;
} Buffer;

/* Buffer management */
Buffer *buf_cur(void);

/* Buffer creation and management - all return EdError */
EdError buf_new(const char *filename, int *out_idx);

/* Create an unnamed scratch buffer titled `title` (filename = NULL,
 * dirty = 0); `title` may be NULL. Index returned via *out_idx. */
EdError buf_new_scratch(const char *title, int *out_idx);

/* Create a read-only buffer titled `title` (filetype optional),
 * populated from `text` (len bytes, split into rows). dirty = 0,
 * readonly = 1. Index returned via *out_idx. */
EdError buf_open_readonly(const char *title, const char *filetype,
                          const char *text, size_t len, int *out_idx);

EdError buf_close(int index);
EdError buf_switch(int index);
EdError buf_open_file(const char *filename, Buffer **out);

int buf_find_by_filename(
    const char
        *filename); /* Find buffer index by filename, returns -1 if not found */
void buf_next(void);
void buf_prev(void);
void buf_open_or_switch(const char *filename, bool add_to_jumplist); /* Open file or switch to it if already open */
void buf_insert_char_in(Buffer *buf, int c);
void buf_insert_newline_in(Buffer *buf);
void buf_del_char_in(Buffer *buf);
void buf_delete_line_in(Buffer *buf);
void buf_yank_line_in(Buffer *buf);
void buf_find_in(Buffer *buf);
/* Reload this buffer's file content from disk (discard changes) */
void buf_reload(Buffer *buf);

/* Multi-cursor API. all_cursors always has >= 1 entry; buf->cursor
 * always points to one of them. Adding/removing extras leaves
 * buf->cursor itself untouched unless the caller passes it in. */
Cursor *buf_cursor_add(Buffer *buf, int y, int x);  /* heap entry, stable ptr */
int     buf_cursor_remove(Buffer *buf, Cursor *c);  /* fails if c is active */
void    buf_cursor_clear_extras(Buffer *buf);       /* keep only active */
int     buf_cursor_set_active(Buffer *buf, Cursor *c);
int     buf_cursor_count(const Buffer *buf);

/* Sync the active cursor's stored position from the focused window's
 * cursor. Edit primitives call this after mutating win->cursor so
 * buf->cursor stays in step. */
void    buf_cursor_sync_from_window(Buffer *buf);

/* Make `win` the owner of buf's live cursor set. Parks the previous
 * owner's set (when that window still shows the buffer), restores the
 * set previously parked for `win`, or — when the previous owner is
 * gone — lets `win` adopt the live set as-is ("the buffer keeps the
 * cursors of its last window"). Idempotent and cheap when `win`
 * already owns the live set. Does not touch win->cursor. */
void    buf_cursors_bind_window(Buffer *buf, struct Window *win);

/* The cursor set to draw for (buf, win): the live set when win owns it
 * (*skip_active = buf->cursor — the hardware cursor covers it), else
 * that window's parked set (*skip_active = NULL — parked sets render
 * whole). Returns NULL when the window has no set for this buffer. */
CursorVec buf_cursors_for_window(Buffer *buf, const struct Window *win,
                                 Cursor **skip_active);
#endif
