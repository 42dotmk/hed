#include "plugin.h"
#include "cmd_util.h"
#include "hed.h"
#include "strutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Filetype → command template. `%s` is replaced by the (escaped) path. */
typedef struct { const char *ft; const char *tmpl; } FmtRule;

static const FmtRule rules[] = {
    { "c",          "clang-format -i %s" },
    { "cpp",        "clang-format -i %s" },
    { "rust",       "rustfmt %s" },
    { "go",         "gofmt -w %s" },
    { "python",     "black %s" },
    { "javascript", "prettier --write %s" },
    { "typescript", "prettier --write %s" },
    { "json",       "prettier --parser json --write %s" },
    { "html",       "prettier --write %s" },
    { "css",        "prettier --write %s" },
    { "markdown",   "prettier --write %s" },
};

static const char *find_tmpl(const char *ft) {
    if (!ft) return NULL;
    for (size_t i = 0; i < sizeof(rules)/sizeof(rules[0]); i++) {
        if (strcmp(ft, rules[i].ft) == 0) return rules[i].tmpl;
    }
    return NULL;
}

static void cmd_fmt(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (!buf->filename || !*buf->filename) {
        ed_set_status_message("fmt: buffer has no filename");
        return;
    }

    const char *ft = buf->filetype ? buf->filetype : "txt";
    const char *tmpl = find_tmpl(ft);
    if (!tmpl) {
        ed_set_status_message("fmt: no formatter for filetype '%s'", ft);
        return;
    }

    EdError serr = buf_save_in(buf);
    if (serr != ED_OK) {
        ed_set_status_message("fmt: save failed: %s", ed_error_string(serr));
        return;
    }

    char esc_path[1024];
    shell_escape_single(buf->filename, esc_path, sizeof(esc_path));

    char cmd_str[1536];
    snprintf(cmd_str, sizeof(cmd_str), tmpl, esc_path);

    disable_raw_mode();
    int status = system(cmd_str);
    enable_raw_mode();

    if (status != 0) {
        ed_set_status_message("fmt: formatter exited with status %d", status);
        return;
    }

    buf_reload(buf);
    ed_set_status_message("fmt: formatted (%s)", buf->filename);
}

static int fmt_init(void) {
    cmd("fmt", cmd_fmt, "format buffer with external formatter");
    return 0;
}

const Plugin plugin_fmt = {
    .name   = "fmt",
    .desc   = "external code formatters (clang-format, rustfmt, prettier, ...)",
    .init   = fmt_init,
    .deinit = NULL,
};
