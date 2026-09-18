#include "cmd_lsp.h"
#include "hed.h"
#include "lsp.h"

/* :lsp_connect <lang> tcp <host>:<port>          — TCP
 * :lsp_connect <lang> <to_pipe> <from_pipe>      — named pipes
 * Optional trailing root_uri for both forms.
 */
void cmd_lsp_connect(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: lsp_connect <lang> tcp <host>:<port>  "
                              "or  lsp_connect <lang> <to_pipe> <from_pipe>");
        return;
    }

    char lang[64] = {0};
    char arg2[512] = {0};
    char arg3[512] = {0};
    char root[512] = {0};

    const char *p = args;
    p = args_next_token(p, lang, sizeof(lang));
    p = args_next_token(p, arg2, sizeof(arg2));
    p = args_next_token(p, arg3, sizeof(arg3));
    p = args_next_token(p, root, sizeof(root));

    if (!lang[0] || !arg2[0]) {
        ed_set_status_message("LSP: missing arguments");
        return;
    }

    const char *root_uri = root[0] ? root : NULL;

    if (strcmp(arg2, "tcp") == 0) {
        /* :lsp_connect <lang> tcp <host>:<port> [root_uri] */
        if (!arg3[0]) {
            ed_set_status_message("LSP: tcp mode requires host:port");
            return;
        }
        lsp_cmd_connect(lang, "tcp", arg3, root_uri);
    } else {
        /* :lsp_connect <lang> <to_pipe> <from_pipe> [root_uri] */
        if (!arg3[0]) {
            ed_set_status_message("LSP: pipe mode requires two paths");
            return;
        }
        lsp_cmd_connect(lang, arg2, arg3, root_uri);
    }
}

/* :lsp_start [lang]
 * Spawn a server from the registry. If lang is omitted, use the
 * current buffer's filetype. Root is auto-detected from the buffer's
 * filename, falling back to E.cwd. */
void cmd_lsp_start(const char *args) {
    char lang[64] = {0};
    args_next_token(args_skip_ws(args ? args : ""), lang, sizeof(lang));

    Buffer *buf = buf_cur();
    const char *use_lang =
        lang[0] ? lang : (buf && buf->filetype ? buf->filetype : NULL);
    if (!use_lang) {
        ed_set_status_message(
            "LSP: usage: lsp_start <lang>  (no filetype on current buffer)");
        return;
    }
    const char *hint = (buf && buf->filename) ? buf->filename : NULL;
    lsp_cmd_start(use_lang, hint);
}

/* :lsp_disconnect <lang> */
void cmd_lsp_disconnect(const char *args) {
    char lang[64] = {0};
    args_next_token(args_skip_ws(args ? args : ""), lang, sizeof(lang));
    if (!lang[0]) {
        ed_set_status_message("LSP: specify a language");
        return;
    }
    lsp_cmd_disconnect(lang);
}

/* :lsp_status */
void cmd_lsp_status(const char *args) {
    (void)args;
    lsp_cmd_status();
}

/* :lsp_autostart [on|off|toggle] */
void cmd_lsp_autostart(const char *args) {
    int v = args_tristate(args, lsp_get_autostart());
    if (v < 0) {
        ed_set_status_message("usage: lsp_autostart on|off|toggle");
        return;
    }
    lsp_set_autostart(v);
    ed_set_status_message("LSP auto-start %s", v ? "on" : "off");
}

/* :lsp_hover */
void cmd_lsp_hover(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("LSP: no buffer");
        return;
    }
    lsp_request_hover(buf, buf->cursor->y, buf->cursor->x);
}

/* :lsp_definition */
void cmd_lsp_definition(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("LSP: no buffer");
        return;
    }
    lsp_request_definition(buf, buf->cursor->y, buf->cursor->x);
}

/* :lsp_diagnostics — dump the stored diagnostics into the quickfix list. */
void cmd_lsp_diagnostics(const char *args) {
    (void)args;
    lsp_cmd_diagnostics();
}

/* Resolve the current window/buffer and the range a request applies
 * to: the visual selection when one is active (and leave visual mode,
 * the edit that follows would invalidate the anchor), else the cursor
 * as an empty range. */
static Buffer *lsp_cmd_range(int *sl, int *sc, int *el, int *ec, int *had_sel) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win)
        return NULL;
    TextSelection ts;
    if (kb_visual_to_textsel(buf, win, 0, &ts)) {
        *sl = ts.start.line;
        *sc = ts.start.col;
        *el = ts.end.line;
        *ec = ts.end.col;
        *had_sel = 1;
        kb_visual_escape();
    } else {
        *sl = *el = win->is_modal ? buf->cursor->y : win->cursor.y;
        *sc = *ec = win->is_modal ? buf->cursor->x : win->cursor.x;
        *had_sel = 0;
    }
    return buf;
}

/* :lsp_code_action [kind] — pick and run a code action for the cursor
 * or selection. With a kind filter (e.g. source.organizeImports) a
 * single match runs without asking. */
void cmd_lsp_code_action(const char *args) {
    char only[64] = {0};
    args_next_token(args_skip_ws(args ? args : ""), only, sizeof(only));
    int sl, sc, el, ec, had_sel;
    Buffer *buf = lsp_cmd_range(&sl, &sc, &el, &ec, &had_sel);
    if (!buf) {
        ed_set_status_message("LSP: no buffer");
        return;
    }
    lsp_request_code_action(buf, sl, sc, el, ec, only[0] ? only : NULL);
}

static void lsp_rename_answer(const char *answer, void *ud) {
    (void)ud;
    if (!answer || !*answer)
        return;
    Buffer *buf = buf_cur();
    if (!buf)
        return;
    lsp_request_rename(buf, buf->cursor->y, buf->cursor->x, answer);
}

/* :lsp_rename [new_name] — rename the symbol under the cursor; without
 * an argument the new name is asked for, pre-filled with the word. */
void cmd_lsp_rename(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("LSP: no buffer");
        return;
    }
    char name[256] = {0};
    args_next_token(args_skip_ws(args ? args : ""), name, sizeof(name));
    if (name[0]) {
        lsp_request_rename(buf, buf->cursor->y, buf->cursor->x, name);
        return;
    }
    StrBuf word = strbuf_new();
    buf_get_word_under_cursor(&word);
    ask("Rename to: ", word.data, lsp_rename_answer, NULL);
    strbuf_free(&word);
}

/* :lsp_format — format the selection (range formatting) or the whole
 * document through the server. */
void cmd_lsp_format(const char *args) {
    (void)args;
    int sl, sc, el, ec, had_sel;
    Buffer *buf = lsp_cmd_range(&sl, &sc, &el, &ec, &had_sel);
    if (!buf) {
        ed_set_status_message("LSP: no buffer");
        return;
    }
    if (had_sel && ec == 0 && el > sl)
        el--; /* exclusive end at column 0 = previous row */
    lsp_request_formatting(buf, had_sel ? sl : -1, had_sel ? el : -1);
}

/* :lsp_references — every reference to the symbol under the cursor,
 * declaration included, into the quickfix list. */
void cmd_lsp_references(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("LSP: no buffer");
        return;
    }
    lsp_request_references(buf, buf->cursor->y, buf->cursor->x);
}

/* :lsp_toggle [lang] — stop the server for the language (default: the
 * current buffer's filetype) if one is running, else start it. */
void cmd_lsp_toggle(const char *args) {
    char lang[64] = {0};
    args_next_token(args_skip_ws(args ? args : ""), lang, sizeof(lang));
    Buffer *buf = buf_cur();
    const char *use_lang =
        lang[0] ? lang : (buf && buf->filetype ? buf->filetype : NULL);
    if (!use_lang) {
        ed_set_status_message(
            "LSP: usage: lsp_toggle <lang>  (no filetype on current buffer)");
        return;
    }
    if (lsp_server_running(use_lang)) {
        lsp_cmd_disconnect(use_lang);
        return;
    }
    lsp_cmd_start(use_lang, buf && buf->filename ? buf->filename : NULL);
}
