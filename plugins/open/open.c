/* open plugin: hand a path or URL off to the system's default app.
 *
 *   :open [target]   xdg-open <target>; no arg = path/URL under cursor
 *   :open_file       open current buffer's file
 *   :open_dir        open directory containing current buffer's file
 *   gx (normal)      open path/URL under cursor
 *
 * Uses xdg-open on Linux, `open` on macOS. */

#include "hed.h"
#include "buf/buf_helpers.h"
#include "lib/sizedstr.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void sstr_cat(SizedStr *s, const char *cstr) {
    sstr_append(s, cstr, strlen(cstr));
}

#if defined(__APPLE__)
#define OPEN_CMD "open"
#else
#define OPEN_CMD "xdg-open"
#endif

static int is_url_char(unsigned char c) {
    if (isalnum(c)) return 1;
    switch (c) {
    case '/': case '.': case '_': case '-': case '~':
    case '+': case ':': case '?': case '=': case '&':
    case '#': case '%': case '@': case ',': case ';':
    case '!': case '$': case '\'': case '(': case ')':
    case '*':
        return 1;
    default:
        return 0;
    }
}

/* Extract a URL- or path-like token around the cursor. Falls back to
 * buf_get_path_under_cursor's notion of a path if no URL chars cluster. */
static int extract_target_under_cursor(SizedStr *out) {
    BUF(buf);
    WIN(win);
    if (!BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return 0;
    Row *row = &buf->rows[win->cursor.y];
    if (row->chars.len == 0) return 0;

    const char *s = row->chars.data;
    int len = (int)row->chars.len;
    int cx = win->cursor.x;
    if (cx >= len) cx = len - 1;
    if (cx < 0) return 0;

    if (!is_url_char((unsigned char)s[cx])) {
        int left = cx - 1;
        while (left >= 0 && !is_url_char((unsigned char)s[left])) left--;
        if (left < 0) return 0;
        cx = left;
    }

    int start = cx, end = cx + 1;
    while (start > 0 && is_url_char((unsigned char)s[start - 1])) start--;
    while (end < len && is_url_char((unsigned char)s[end])) end++;

    /* Trim common trailing punctuation that's usually sentence-glue,
     * not part of the URL. */
    while (end > start) {
        char c = s[end - 1];
        if (c == '.' || c == ',' || c == ';' || c == ':' ||
            c == ')' || c == '!' || c == '?' || c == '\'')
            end--;
        else
            break;
    }
    if (end <= start) return 0;

    sstr_free(out);
    *out = sstr_from(s + start, (size_t)(end - start));
    return out->data && out->len > 0;
}

/* Append `s` to `dst` single-quoted and safe for /bin/sh. */
static void append_shell_quoted(SizedStr *dst, const char *s) {
    sstr_cat(dst, "'");
    for (const char *p = s; *p; p++) {
        if (*p == '\'') {
            sstr_cat(dst, "'\\''");
        } else {
            sstr_append_char(dst, (unsigned char)*p);
        }
    }
    sstr_cat(dst, "'");
}

static void open_with_xdg(const char *target) {
    if (!target || !*target) {
        ed_set_status_message("open: nothing to open");
        return;
    }
    SizedStr cmd = sstr_new();
    sstr_cat(&cmd, OPEN_CMD " ");
    append_shell_quoted(&cmd, target);
    sstr_cat(&cmd, " >/dev/null 2>&1 &");
    /* Background — fire and forget; don't block the editor on viewer
     * startup. system() is fine here, no raw-mode dance needed. */
    int rc = system(cmd.data);
    (void)rc;
    ed_set_status_message("open: %s", target);
    sstr_free(&cmd);
}

/* --- commands --- */

static void cmd_open(const char *args) {
    if (args && *args) {
        while (*args == ' ') args++;
        open_with_xdg(args);
        return;
    }
    SizedStr target = sstr_new();
    if (!extract_target_under_cursor(&target)) {
        ed_set_status_message("open: no path/URL under cursor");
        sstr_free(&target);
        return;
    }
    open_with_xdg(target.data);
    sstr_free(&target);
}

static void cmd_open_file(const char *args) {
    (void)args;
    BUF(buf);
    if (!buf->filename || !*buf->filename) {
        ed_set_status_message("open: buffer has no file");
        return;
    }
    open_with_xdg(buf->filename);
}

static void cmd_open_dir(const char *args) {
    (void)args;
    BUF(buf);
    const char *fn = (buf->filename && *buf->filename) ? buf->filename : ".";
    const char *slash = strrchr(fn, '/');
    if (!slash) {
        open_with_xdg(".");
        return;
    }
    SizedStr dir = sstr_from(fn, (size_t)(slash - fn));
    open_with_xdg(dir.data ? dir.data : ".");
    sstr_free(&dir);
}

/* --- keybind --- */

static void kb_open_under_cursor(void) {
    cmd_open(NULL);
}

/* --- lifecycle --- */

static int open_init(void) {
    cmd("open", cmd_open, "open path/URL (arg or under cursor) via " OPEN_CMD);
    cmd("open_file", cmd_open_file, "open current buffer's file via " OPEN_CMD);
    cmd("open_dir", cmd_open_dir, "open directory of current buffer via " OPEN_CMD);
    mapn("gx", kb_open_under_cursor, "open path/URL under cursor");
    return 0;
}

const Plugin plugin_open = {
    .name   = "open",
    .desc   = "open paths/URLs with the system's default application",
    .init   = open_init,
    .deinit = NULL,
};
