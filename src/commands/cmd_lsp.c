#include "hed.h"
#include "cmd_lsp.h"
#include "lsp.h"
#include <string.h>

/* Start an LSP server for a language
 * Usage: :lsp_start <lang> <command> [root_uri]
 * Example: :lsp_start c clangd
 * Example: :lsp_start python pylsp
 */
void cmd_lsp_start(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: lsp_start <lang> <command> [root_uri]");
        return;
    }

    char lang[64] = {0};
    char cmd[256] = {0};
    char root_uri[512] = {0};

    /* Parse arguments: lang command [root_uri] */
    const char *p = args;

    /* Skip leading whitespace */
    while (*p && isspace((unsigned char)*p))
        p++;

    /* Extract language */
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < (int)sizeof(lang) - 1) {
        lang[i++] = *p++;
    }
    lang[i] = '\0';

    if (!lang[0]) {
        ed_set_status_message("LSP: No language specified");
        return;
    }

    /* Skip whitespace */
    while (*p && isspace((unsigned char)*p))
        p++;

    /* Extract command */
    i = 0;
    while (*p && i < (int)sizeof(cmd) - 1) {
        /* Check if we're at root_uri marker */
        if (strncmp(p, "file://", 7) == 0) {
            break;
        }
        cmd[i++] = *p++;
    }

    /* Trim trailing whitespace from command */
    while (i > 0 && isspace((unsigned char)cmd[i - 1])) {
        i--;
    }
    cmd[i] = '\0';

    if (!cmd[0]) {
        ed_set_status_message("LSP: No command specified");
        return;
    }

    /* Extract optional root_uri */
    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p) {
        i = 0;
        while (*p && i < (int)sizeof(root_uri) - 1) {
            root_uri[i++] = *p++;
        }
        root_uri[i] = '\0';
    } else {
        /* Default to current working directory */
        snprintf(root_uri, sizeof(root_uri), "file://%s", E.cwd);
    }

    lsp_cmd_start(lang, cmd, root_uri);
}

/* Stop an LSP server for a language
 * Usage: :lsp_stop <lang>
 * Example: :lsp_stop c
 */
void cmd_lsp_stop(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: lsp_stop <lang>");
        return;
    }

    char lang[64] = {0};
    const char *p = args;

    /* Skip leading whitespace */
    while (*p && isspace((unsigned char)*p))
        p++;

    /* Extract language */
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < (int)sizeof(lang) - 1) {
        lang[i++] = *p++;
    }
    lang[i] = '\0';

    if (!lang[0]) {
        ed_set_status_message("LSP: No language specified");
        return;
    }

    lsp_cmd_stop(lang);
}

/* Request hover information at cursor
 * Usage: :lsp_hover
 */
void cmd_lsp_hover(const char *args) {
    BUF(buf)
    lsp_request_hover(buf, buf->cursor.y, buf->cursor.x);
}

/* Go to definition at cursor
 * Usage: :lsp_definition
 */
void cmd_lsp_definition(const char *args) {
    BUF(buf)
    lsp_request_definition(buf, buf->cursor.y, buf->cursor.x);
}

/* Request completion at cursor
 * Usage: :lsp_completion
 */
void cmd_lsp_completion(const char *args) {
    BUF(buf)
    lsp_request_completion(buf, buf->cursor.y, buf->cursor.x);
}
