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
