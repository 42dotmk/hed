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

#define MAN_FILETYPE "man"

/* Absolute path to hed's bundled man pages, baked in at build time (see
 * makefile MAN_DIR). When set, it's prepended to MANPATH with a trailing
 * empty entry so man-db still appends the system search path after it —
 * this is what lets `:man hed-tmux` resolve the in-tree pages. */
#ifndef HED_MAN_DIR
#define HED_MAN_DIR ""
#endif

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
    shell_escape_single(topic, qtopic, sizeof(qtopic));
    if (section && *section)
        shell_escape_single(section, qsect, sizeof(qsect));
    else
        qsect[0] = '\0';

    /* Prepend hed's bundled man dir to MANPATH (trailing ':' keeps the
     * system pages reachable) so :man hed-<plugin> finds the in-tree docs. */
    const char *man_dir = HED_MAN_DIR;
    char manpath[512];
    manpath[0] = '\0';
    if (man_dir[0])
        snprintf(manpath, sizeof(manpath), "MANPATH='%s:' ", man_dir);

    char cmd[1024];
    if (qsect[0])
        snprintf(cmd, sizeof(cmd),
                 "%sMANWIDTH=%d MANPAGER=cat man %s %s 2>/dev/null | col -bx",
                 manpath, cols, qsect, qtopic);
    else
        snprintf(cmd, sizeof(cmd),
                 "%sMANWIDTH=%d MANPAGER=cat man %s 2>/dev/null | col -bx",
                 manpath, cols, qtopic);

    char **lines = NULL;
    int lcnt = 0;
    if (!term_cmd_capture(cmd, &lines, &lcnt) || lcnt == 0) {
        term_cmd_free(lines, lcnt);
        ed_set_status_message("man: no entry for %s", topic);
        return;
    }

    int idx = -1;
    if (buf_open_readonly(title, MAN_FILETYPE, NULL, 0, &idx) != ED_OK) {
        term_cmd_free(lines, lcnt);
        ed_set_status_message("man: failed to create buffer");
        return;
    }
    Buffer *buf = &E.buffers[idx];
    for (int i = 0; i < lcnt; i++)
        buf_row_insert_in(buf, buf->num_rows, lines[i], strlen(lines[i]));
    term_cmd_free(lines, lcnt);
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
    StrView w;
    if (!buf_word_view_under_cursor(&w) || w.len == 0) {
        ed_set_status_message("man: no word under cursor");
        return;
    }
    char topic[128];
    size_t n = w.len < sizeof(topic) - 1 ? w.len : sizeof(topic) - 1;
    memcpy(topic, w.data, n);
    topic[n] = '\0';
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
