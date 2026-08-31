/* smart_indent plugin: carry the previous line's leading whitespace
 * onto new lines, shifted by that line's bracket balance — one level
 * deeper after a net opener ('{'/'('/'[', python/yaml trailing ':'),
 * one level back after a net closer ("    2]") — and drop one level
 * when a closer ('}'/')'/']') is the first thing typed on a line.
 * Rules are per-filetype and registrable from user config via
 * smart_indent_register(). */

#include "smart_indent.h"
#include "hed.h"
#include "stb_ds.h"
#include <string.h>

typedef struct {
    char *filetype;     /* exact filetype, or "*" for the fallback */
    char *indent_after; /* line-ending chars that add one level */
    char *dedent_of;    /* closers that, typed at line start, drop one */
} IndentRule;

static IndentRule *rules; /* stb_ds array */

void smart_indent_register(const char *filetype, const char *indent_after,
                           const char *dedent_of) {
    if (!filetype || !indent_after || !dedent_of)
        return;
    /* Last-write-wins, same as keybinds: re-registering a filetype
     * replaces its rule. */
    for (ptrdiff_t i = 0; i < arrlen(rules); i++) {
        if (strcmp(rules[i].filetype, filetype) == 0) {
            free(rules[i].indent_after);
            free(rules[i].dedent_of);
            rules[i].indent_after = strdup(indent_after);
            rules[i].dedent_of = strdup(dedent_of);
            return;
        }
    }
    IndentRule r = {strdup(filetype), strdup(indent_after), strdup(dedent_of)};
    arrput(rules, r);
}

static const IndentRule *rule_for(const char *filetype) {
    const IndentRule *fallback = NULL;
    for (ptrdiff_t i = 0; i < arrlen(rules); i++) {
        if (filetype && strcmp(rules[i].filetype, filetype) == 0)
            return &rules[i];
        if (strcmp(rules[i].filetype, "*") == 0)
            fallback = &rules[i];
    }
    return fallback;
}

static int is_indent_char(char c) { return c == ' ' || c == '\t'; }

static char closer_of(char opener) {
    switch (opener) {
    case '{':
        return '}';
    case '(':
        return ')';
    case '[':
        return ']';
    default:
        return 0;
    }
}

/* One indent level, honoring :set-style tab settings. */
static void insert_level(Buffer *buf) {
    if (!E.expand_tab) {
        buf_insert_char_in(buf, '\t');
    } else {
        int tabw = (E.tab_size > 0) ? E.tab_size : TAB_STOP;
        for (int i = 0; i < tabw; i++)
            buf_insert_char_in(buf, ' ');
    }
}

static void insert_indent_copy(Buffer *buf, const Row *src, size_t len) {
    for (size_t i = 0; i < len; i++)
        buf_insert_char_in(buf, src->chars.data[i]);
}

/* Step past a quoted span ("..." or `...`, backslash escapes) so its
 * contents don't count as brackets. Single quotes are left alone: an
 * apostrophe or rust lifetime would swallow the rest of the line. */
static size_t skip_string(const Row *row, size_t i) {
    char q = row->chars.data[i++];
    while (i < row->chars.len) {
        char c = row->chars.data[i];
        if (c == '\\')
            i += 2;
        else if (c == q)
            return i + 1;
        else
            i++;
    }
    return i;
}

/* Bracket balance of a line: +1 per opener, -1 per closer, clamped to
 * one level. The first non-whitespace char is skipped if it's a closer
 * — its dedent already shaped this line's own indent (a standalone "}"
 * must not dedent the next line again). A trailing ':' counts as an
 * opener where the rule says so (python, yaml). */
static int line_balance(const Row *row, const IndentRule *rule) {
    int net = 0;
    size_t i = 0;
    while (i < row->chars.len && is_indent_char(row->chars.data[i]))
        i++;
    if (i < row->chars.len && strchr(rule->dedent_of, row->chars.data[i]))
        i++;
    while (i < row->chars.len) {
        char c = row->chars.data[i];
        if (c == '"' || c == '`') {
            i = skip_string(row, i);
            continue;
        }
        if (closer_of(c) && strchr(rule->indent_after, c))
            net++;
        else if (strchr(rule->dedent_of, c))
            net--;
        i++;
    }
    char last = 0;
    for (size_t j = row->chars.len; j > 0; j--) {
        if (!is_indent_char(row->chars.data[j - 1])) {
            last = row->chars.data[j - 1];
            break;
        }
    }
    if (last == ':' && strchr(rule->indent_after, ':'))
        net++;
    return net > 0 ? 1 : (net < 0 ? -1 : 0);
}

/* Newline: copy the previous line's leading whitespace verbatim so a
 * tab-indented line yields a tab-indented continuation, shifted by the
 * line's bracket balance — one level deeper after "x = [", one level
 * back after "    2]". When the cursor sits between a pair ("{|}"),
 * the closer gets its own line at the base indent and the cursor stays
 * on the indented middle line. */
static void on_newline(Buffer *buf, Window *win, const IndentRule *rule) {
    if (!rule || win->cursor.y < 1 || win->cursor.y >= buf->num_rows)
        return;
    Row *prev = &buf->rows[win->cursor.y - 1];
    size_t base = 0;
    while (base < prev->chars.len && is_indent_char(prev->chars.data[base]))
        base++;
    int shift = line_balance(prev, rule);

    size_t copy = base;
    if (shift < 0 && copy > 0) {
        /* One level shallower: drop a tab, or up to tab-width spaces,
         * off the end of the carried indent. */
        if (prev->chars.data[copy - 1] == '\t') {
            copy--;
        } else {
            int tabw = (E.tab_size > 0) ? E.tab_size : TAB_STOP;
            for (int n = 0;
                 n < tabw && copy > 0 && prev->chars.data[copy - 1] == ' '; n++)
                copy--;
        }
    }
    insert_indent_copy(buf, prev, copy);
    if (shift <= 0)
        return;

    char last = 0;
    for (size_t i = prev->chars.len; i > 0; i--) {
        if (!is_indent_char(prev->chars.data[i - 1])) {
            last = prev->chars.data[i - 1];
            break;
        }
    }
    Row *row = &buf->rows[win->cursor.y];
    char next = ((size_t)win->cursor.x < row->chars.len)
                    ? row->chars.data[win->cursor.x]
                    : 0;
    if (last && next && next == closer_of(last)) {
        /* "{|}" + Enter: split again so the closer lands on its own
         * line at the base indent, then come back to the middle. */
        int mid_y = win->cursor.y;
        buf_insert_newline_in(buf);
        prev = &buf->rows[mid_y - 1]; /* rows may have been realloc'd */
        insert_indent_copy(buf, prev, base);
        win->cursor.y = mid_y;
        win->cursor.x = (int)buf->rows[mid_y].chars.len;
    }
    insert_level(buf);
}

/* Closer typed as the first non-whitespace char on the line: remove
 * one indent level (a tab, or up to tab-width spaces). */
static void on_closer(Buffer *buf, Window *win) {
    int at = win->cursor.x - 1; /* the closer just inserted */
    if (at < 1 || win->cursor.y >= buf->num_rows)
        return;
    Row *row = &buf->rows[win->cursor.y];
    for (int i = 0; i < at; i++) {
        if (!is_indent_char(row->chars.data[i]))
            return;
    }
    int remove = 1; /* a tab is one level */
    if (row->chars.data[at - 1] == ' ') {
        int tabw = (E.tab_size > 0) ? E.tab_size : TAB_STOP;
        remove = 0;
        while (remove < tabw && at - 1 - remove >= 0 &&
               row->chars.data[at - 1 - remove] == ' ')
            remove++;
    }
    /* Backspace the indent chars from just before the closer, then
     * step back over the (now shifted) closer. */
    win->cursor.x = at;
    for (int i = 0; i < remove; i++)
        buf_del_char_in(buf);
    win->cursor.x += 1;
}

static void hook_smart_indent(const HookCharEvent *event) {
    WIN(win)
    Buffer *buf = event->buf;
    if (!buf)
        return;
    const IndentRule *rule = rule_for(buf->filetype);
    if (event->c == '\n')
        on_newline(buf, win, rule);
    else if (rule && event->c < 0x80 && strchr(rule->dedent_of, (char)event->c))
        on_closer(buf, win);
}

static int smart_indent_init(void) {
    smart_indent_register("*", "{([", "}])");
    smart_indent_register("python", "{([:", "}])");
    smart_indent_register("yaml", "{([:", "}])");
    smart_indent_register("yml", "{([:", "}])"); /* .yml detects as "yml" */
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", hook_smart_indent);
    return 0;
}

const Plugin plugin_smart_indent = {
    .name = "smart_indent",
    .desc = "carry previous line's indentation onto new lines",
    .init = smart_indent_init,
    .deinit = NULL,
};
