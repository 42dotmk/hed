#ifndef QUICKFIX_H
#define QUICKFIX_H

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

