#include "hed.h"
#include "cmd_lsp.h"
#include "lsp.h"
#include <ctype.h>
#include <string.h>

/* Skip leading whitespace; return pointer into s. */
static const char *skip_ws(const char *s) {
    while (s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Extract next whitespace-delimited token into dst (max dst_sz-1 chars).
 * Returns pointer past the token in the source string. */
static const char *next_token(const char *s, char *dst, int dst_sz) {
    s = skip_ws(s);
    int i = 0;
    while (*s && !isspace((unsigned char)*s) && i < dst_sz - 1)
        dst[i++] = *s++;
    dst[i] = '\0';
    return s;
}

/* :lsp_connect <lang> tcp <host>:<port>          — TCP
 * :lsp_connect <lang> <to_pipe> <from_pipe>      — named pipes
 * Optional trailing root_uri for both forms.
 */
void cmd_lsp_connect(const char *args) {
    if (!args || !*args) {
        ed_set_status_message(
            "Usage: lsp_connect <lang> tcp <host>:<port>  "
            "or  lsp_connect <lang> <to_pipe> <from_pipe>");
        return;
    }

    char lang[64]     = {0};
    char arg2[512]    = {0};
    char arg3[512]    = {0};
    char root[512]    = {0};

    const char *p = args;
    p = next_token(p, lang,  sizeof(lang));
    p = next_token(p, arg2,  sizeof(arg2));
    p = next_token(p, arg3,  sizeof(arg3));
    p = next_token(p, root,  sizeof(root));

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

/* :lsp_disconnect <lang> */
void cmd_lsp_disconnect(const char *args) {
    char lang[64] = {0};
    next_token(skip_ws(args ? args : ""), lang, sizeof(lang));
    if (!lang[0]) { ed_set_status_message("LSP: specify a language"); return; }
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
    if (!buf) { ed_set_status_message("LSP: no buffer"); return; }
    lsp_request_hover(buf, buf->cursor.y, buf->cursor.x);
}

/* :lsp_definition */
void cmd_lsp_definition(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) { ed_set_status_message("LSP: no buffer"); return; }
    lsp_request_definition(buf, buf->cursor.y, buf->cursor.x);
}

/* :lsp_completion */
void cmd_lsp_completion(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) { ed_set_status_message("LSP: no buffer"); return; }
    lsp_request_completion(buf, buf->cursor.y, buf->cursor.x);
}
