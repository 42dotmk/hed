#ifndef QUICKFIX_H
#define QUICKFIX_H

#include "buffer.h"

typedef struct {
    char *text;
    char *filename; /* optional */
    int line;       /* 1-based, optional (0 if unknown) */
    int col;        /* 1-based, optional (0 if unknown) */
} QfItem;

typedef struct {
    int open;    /* 0/1 */
    int focus;   /* 0/1: when focused, keypresses navigate quickfix */
    int height;  /* number of lines used for the pane */
    int sel;     /* selected index (0-based) */
    int scroll;  /* first visible index */
    QfItem *items;
    int len;
    int cap;
} Qf;

/* Quickfix buffer helpers
 *
 * The quickfix list is now backed by a real Buffer so the quickfix
 * window renders like any other buffer window. These helpers let
 * the UI/layout code access that buffer.
 */

/* Return the existing quickfix buffer if present, else NULL. */
Buffer *qf_get_buffer(Qf *qf);
/* Ensure a quickfix buffer exists, creating it on demand. */
Buffer *qf_get_or_create_buffer(Qf *qf);
/* True if the given buffer is the quickfix buffer. */
int qf_is_quickfix_buffer(const Buffer *buf);

/* API */
void qf_init(Qf *qf);
void qf_free(Qf *qf);

void qf_open(Qf *qf, int height);
void qf_close(Qf *qf);
void qf_toggle(Qf *qf, int height);

void qf_focus(Qf *qf);
void qf_blur(Qf *qf);

void qf_clear(Qf *qf);
int  qf_add(Qf *qf, const char *filename, int line, int col, const char *text);

void qf_move(Qf *qf, int delta);
void qf_open_selected(Qf *qf);
void qf_open_idx(Qf *qf, int idx);

#endif /* QUICKFIX_H */
