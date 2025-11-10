#include "hed.h"

static void qf_item_free(QfItem *it) {
    if (!it) return;
    free(it->text); it->text = NULL;
    free(it->filename); it->filename = NULL;
}

void qf_init(Qf *qf) {
    if (!qf) return;
    qf->open = 0; qf->focus = 0; qf->height = 8; qf->sel = 0; qf->scroll = 0;
    qf->items = NULL; qf->len = 0; qf->cap = 0;
}

void qf_free(Qf *qf) {
    if (!qf) return;
    for (int i = 0; i < qf->len; i++) qf_item_free(&qf->items[i]);
    free(qf->items); qf->items = NULL; qf->len = qf->cap = 0;
}

static void qf_reserve(Qf *qf, int need) {
    if (qf->cap >= need) return;
    int ncap = qf->cap ? qf->cap * 2 : 32;
    if (ncap < need) ncap = need;
    qf->items = realloc(qf->items, (size_t)ncap * sizeof(QfItem));
    qf->cap = ncap;
}

void qf_open(Qf *qf, int height) {
    if (!qf) return;
    if (height > 0) qf->height = height;
    qf->open = 1; qf->focus = 1;
    if (qf->sel >= qf->len) qf->sel = qf->len ? qf->len - 1 : 0;
}

void qf_close(Qf *qf) {
    if (!qf) return;
    qf->open = 0; qf->focus = 0;
}

void qf_toggle(Qf *qf, int height) {
    if (!qf) return;
    if (qf->open) qf_close(qf); else qf_open(qf, height);
}

void qf_focus(Qf *qf) { if (qf) qf->focus = 1; }
void qf_blur(Qf *qf)  { if (qf) qf->focus = 0; }

void qf_clear(Qf *qf) {
    if (!qf) return;
    for (int i = 0; i < qf->len; i++) qf_item_free(&qf->items[i]);
    qf->len = 0; qf->sel = 0; qf->scroll = 0;
}

int qf_add(Qf *qf, const char *filename, int line, int col, const char *text) {
    if (!qf) return -1;
    qf_reserve(qf, qf->len + 1);
    QfItem *it = &qf->items[qf->len++];
    it->text = text ? strdup(text) : strdup("");
    it->filename = filename ? strdup(filename) : NULL;
    it->line = line; it->col = col;
    return qf->len - 1;
}

void qf_move(Qf *qf, int delta) {
    if (!qf || qf->len == 0) return;
    int ns = qf->sel + delta;
    if (ns < 0) ns = 0;
    if (ns >= qf->len) ns = qf->len - 1;
    qf->sel = ns;
    /* scroll into view */
    if (qf->sel < qf->scroll) qf->scroll = qf->sel;
    if (qf->sel >= qf->scroll + qf->height) qf->scroll = qf->sel - qf->height + 1;
    if (qf->scroll < 0) qf->scroll = 0;
}

static void qf_jump_to(const QfItem *it) {
    if (!it) return;
    if (it->filename) {
        buf_open((char *)it->filename);
    }
    Buffer *b = buf_cur();
    if (!b) return;
    if (it->line > 0) {
        buf_goto_line(it->line);
    }
    if (it->col > 0) {
        if (b->cursor_y < b->num_rows) {
            int max = b->rows[b->cursor_y].chars.len;
            int cx = it->col - 1; if (cx < 0) cx = 0; if (cx > max) cx = max;
            b->cursor_x = cx;
        }
    }
}

void qf_open_selected(Qf *qf) {
    if (!qf || qf->len == 0) return;
    qf_jump_to(&qf->items[qf->sel]);
}

void qf_open_idx(Qf *qf, int idx) {
    if (!qf || idx < 0 || idx >= qf->len) return;
    qf->sel = idx;
    qf_jump_to(&qf->items[idx]);
}

