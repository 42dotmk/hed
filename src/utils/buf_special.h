#ifndef BUF_SPECIAL_H
#define BUF_SPECIAL_H

#include "buf/buffer.h"
#include "ui/window.h"
#include <stddef.h>

/* Plugin-owned display buffers (mail lists, dired, man pages, panes,
 * pickers, hover popups). One shared spelling of the flow every such
 * plugin used to hand-roll: find-or-create, retitle, clear, append,
 * then show in the current window / a split / a modal.
 *
 * Typical use:
 *     BufSpecial spec = {.name = "[man]", .filetype = "man",
 *                        .readonly = 1};
 *     int idx = buf_special_get(&spec, NULL);
 *     Buffer *b = &E.buffers[idx];
 *     buf_special_clear(b);
 *     buf_special_addf(b, "%s(%d)", topic, section);
 *     buf_special_show(idx);
 */

typedef struct BufSpecial {
    /* Identity + default title. Matched against buffer filenames
     * first, then titles. NULL = never reuse, always create. */
    const char *name;
    const char *title;    /* display title; NULL -> name */
    const char *filetype; /* drives ft keybinds/commands/render hooks */
    int readonly;
    /* Store `name` as the buffer's filename (mail://…, dired paths)
     * instead of creating a nameless scratch buffer. */
    int as_filename;
} BufSpecial;

/* Find a buffer by filename, then by title. Returns index or -1. */
int buf_special_find(const char *name);

/* Find-or-create per `spec` and (re)apply title/filetype/readonly.
 * Rows are left untouched on reuse. Returns the buffer index or -1;
 * *created (optional) is set to 1 when a new buffer was made. */
int buf_special_get(const BufSpecial *spec, int *created);

/* Drop all rows and reset the buffer cursor. No hooks fire. */
void buf_special_clear(Buffer *b);

/* Append one row / a printf-formatted row / a NULL-safe line array
 * (the term_cmd_capture shape). */
void buf_special_add(Buffer *b, const char *line, size_t len);
void buf_special_addf(Buffer *b, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void buf_special_add_lines(Buffer *b, char **lines, int n);

/* Presentation. All mark the buffer non-dirty first.
 *   show        — attach in the current window, cursor to 0,0.
 *   show_split  — focus the window already showing it, else open a
 *                 vertical (1) / horizontal (0) split and attach.
 *   show_modal  — size a modal from the content (clamped to the
 *                 screen) and show it; anchor at the given screen cell
 *                 or pass -1,-1 to center. Returns the modal (NULL on
 *                 failure — the buffer is closed for you).
 * Return 0 on success, -1 on a bad index. */
int buf_special_show(int idx);
int buf_special_show_split(int idx, int vertical);
Window *buf_special_show_modal(int idx, int anchor_x, int anchor_y);

/* Un-dirty and close, tearing down the modal showing it (if any) —
 * the step every modal site used to have to remember by hand. */
void buf_special_close(int idx);

/* Standard read-only-popup keys on `modal`: j/k/arrows move the
 * modal's cursor line by line (the render loop's scroll-follow does
 * the scrolling), g/G jump to the ends and, when allow_close,
 * q/<Esc> close via buf_special_close. Returns 2 when the modal was
 * closed, 1 when the key navigated, 0 when it isn't one of the
 * standard keys. Callers swallow the event themselves. */
int buf_special_modal_key(Window *modal, int key, int allow_close);

#endif /* BUF_SPECIAL_H */
