/* man plugin: view and search man pages.
 *
 *   :man <topic>          render man page into a read-only buffer
 *   :man <sect> <topic>   pick a specific section (e.g. ":man 3 printf")
 *   :man                  fzf-pick from "apropos ." then open the page
 *
 * The buffer is titled "[man <topic>(<sect>)]"; re-running :man on the
 * same topic switches to the existing buffer instead of re-rendering. */

#include "hed.h"
#include "man.h"
#include "utils/term_cmd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Global helper from buf/buffer.c; not exposed in any header. */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

#define MAN_FILETYPE "man"

static int find_man_buffer(const char *title) {
    for (int i = 0; i < (int)arrlen(E.buffers); i++) {
        Buffer *b = &E.buffers[i];
        if (!b || !b->title || !b->filetype) continue;
        if (strcmp(b->filetype, MAN_FILETYPE) == 0 &&
            strcmp(b->title, title) == 0)
            return i;
    }
    return -1;
}

/* Run cmd, slurp its stdout. Caller frees *out. Returns 1 if output is
 * non-empty (we don't trust the exit code through a `| col` pipeline). */
static int run_capture(const char *cmd, char **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    if (!buf) { pclose(fp); return 0; }

    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (len + n + 1 > cap) {
            while (cap < len + n + 1) cap *= 2;
            char *r = realloc(buf, cap);
            if (!r) { free(buf); pclose(fp); return 0; }
            buf = r;
        }
        memcpy(buf + len, chunk, n);
        len += n;
    }
    pclose(fp);
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return len > 0;
}

static void insert_lines(Buffer *dst, const char *text, size_t len) {
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
        len--;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            size_t llen = i - start;
            if (llen && text[start + llen - 1] == '\r') llen--;
            buf_row_insert_in(dst, dst->num_rows, text + start, llen);
            start = i + 1;
        }
    }
    if (dst->num_rows == 0)
        buf_row_insert_in(dst, 0, "", 0);
}

/* Shell-escape into a fixed buffer; truncates on overflow. */
static void shell_quote(const char *in, char *out, size_t outsz) {
    size_t o = 0;
    if (o + 1 < outsz) out[o++] = '\'';
    for (const char *p = in; *p && o + 4 < outsz; p++) {
        if (*p == '\'') {
            const char *esc = "'\\''";
            for (int k = 0; k < 4 && o + 1 < outsz; k++) out[o++] = esc[k];
        } else {
            out[o++] = *p;
        }
    }
    if (o + 1 < outsz) out[o++] = '\'';
    out[o] = '\0';
}

static void open_man_page(const char *topic, const char *section) {
    if (!topic || !*topic) {
        ed_set_status_message("man: missing topic");
        return;
    }

    char title[256];
    if (section && *section)
        snprintf(title, sizeof(title), "[man %s(%s)]", topic, section);
    else
        snprintf(title, sizeof(title), "[man %s]", topic);

    int existing = find_man_buffer(title);
    if (existing >= 0) {
        buf_switch(existing);
        ed_set_status_message("man: %s", title);
        return;
    }

    int cols = E.screen_cols > 40 ? E.screen_cols : 100;
    if (cols > 200) cols = 200;

    char qtopic[256], qsect[32];
    shell_quote(topic, qtopic, sizeof(qtopic));
    if (section && *section)
        shell_quote(section, qsect, sizeof(qsect));
    else
        qsect[0] = '\0';

    char cmd[1024];
    if (qsect[0])
        snprintf(cmd, sizeof(cmd),
                 "MANWIDTH=%d MANPAGER=cat man %s %s 2>/dev/null | col -bx",
                 cols, qsect, qtopic);
    else
        snprintf(cmd, sizeof(cmd),
                 "MANWIDTH=%d MANPAGER=cat man %s 2>/dev/null | col -bx",
                 cols, qtopic);

    char *out = NULL;
    size_t out_len = 0;
    if (!run_capture(cmd, &out, &out_len)) {
        free(out);
        ed_set_status_message("man: no entry for %s", topic);
        return;
    }

    int idx = -1;
    if (buf_new(NULL, &idx) != ED_OK) {
        free(out);
        ed_set_status_message("man: failed to create buffer");
        return;
    }
    Buffer *buf = &E.buffers[idx];
    free(buf->title);    buf->title    = strdup(title);
    free(buf->filename); buf->filename = NULL;
    free(buf->filetype); buf->filetype = strdup(MAN_FILETYPE);
    buf->readonly = 1;

    insert_lines(buf, out, out_len);
    free(out);
    buf->dirty = 0;

    buf_switch(idx);
    ed_set_status_message("man: %s (%d lines)", title, buf->num_rows);
}

/* Parse "name (section) - desc" from apropos. Section may be missing. */
static void parse_apropos_line(const char *line,
                               char *topic, size_t tsize,
                               char *section, size_t ssize) {
    topic[0] = section[0] = '\0';
    while (*line == ' ' || *line == '\t') line++;

    size_t ti = 0;
    while (*line && *line != ' ' && *line != '\t' &&
           *line != '(' && ti + 1 < tsize)
        topic[ti++] = *line++;
    topic[ti] = '\0';

    while (*line == ' ' || *line == '\t') line++;
    if (*line == '(') {
        line++;
        size_t si = 0;
        while (*line && *line != ')' && si + 1 < ssize)
            section[si++] = *line++;
        section[si] = '\0';
    }
}

static void cmd_man_fzf(void) {
    /* Enumerate via apropos, hand the lines to the picker — keeps this
     * plugin free of any direct fzf coupling. */
    char **lines = NULL;
    int lcnt = 0;
    if (!term_cmd_capture("apropos . 2>/dev/null | sort -u",
                          &lines, &lcnt) || lcnt == 0) {
        term_cmd_free(lines, lcnt);
        ed_set_status_message("man: no apropos output (is `apropos` available?)");
        return;
    }

    char **sel = NULL;
    int cnt = 0;
    int ok = picker_list((const char **)lines, lcnt, 0, &sel, &cnt);
    term_cmd_free(lines, lcnt);
    if (!ok || cnt == 0 || !sel || !sel[0]) {
        picker_list_free(sel, cnt);
        ed_set_status_message("man: nothing selected");
        return;
    }

    char topic[128], section[16];
    parse_apropos_line(sel[0], topic, sizeof(topic),
                       section, sizeof(section));
    picker_list_free(sel, cnt);

    if (!topic[0]) {
        ed_set_status_message("man: couldn't parse picker selection");
        return;
    }
    open_man_page(topic, section[0] ? section : NULL);
}

static void cmd_man(const char *args) {
    if (!args || !*args) {
        cmd_man_fzf();
        return;
    }

    while (*args == ' ') args++;

    /* Pull the first whitespace-separated word. */
    char first[64];
    size_t fi = 0;
    while (*args && *args != ' ' && *args != '\t' && fi + 1 < sizeof(first))
        first[fi++] = *args++;
    first[fi] = '\0';
    while (*args == ' ' || *args == '\t') args++;

    if (*args && first[0] && isdigit((unsigned char)first[0])) {
        /* ":man <section> <topic>" */
        open_man_page(args, first);
    } else {
        open_man_page(first, NULL);
    }
}

static void kb_man_word_under_cursor(void) {
    SizedStr w = sstr_new();
    if (!buf_get_word_under_cursor(&w) || w.len == 0) {
        sstr_free(&w);
        ed_set_status_message("man: no word under cursor");
        return;
    }
    char topic[128];
    size_t n = w.len < sizeof(topic) - 1 ? w.len : sizeof(topic) - 1;
    memcpy(topic, w.data, n);
    topic[n] = '\0';
    sstr_free(&w);
    open_man_page(topic, NULL);
}

static int man_init(void) {
    cmd("man", cmd_man, "view man page (no args = fzf apropos)");
    mapn_ft(MAN_FILETYPE, "gd", kb_man_word_under_cursor,
            "man: open page for word under cursor");
    return 0;
}

const Plugin plugin_man = {
    .name   = "man",
    .desc   = "view and search man pages",
    .init   = man_init,
    .deinit = NULL,
};
