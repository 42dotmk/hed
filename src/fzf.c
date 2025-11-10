#include "hed.h"
#include "fzf.h"
#include <limits.h>

static void trim_eol(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

int fzf_run_opts(const char *input_cmd, const char *fzf_opts, int multi, char ***out_lines, int *out_count) {
    if (out_lines) *out_lines = NULL;
    if (out_count) *out_count = 0;
    if (!input_cmd) return 0;

    char pipebuf[4096];
    snprintf(pipebuf, sizeof(pipebuf), "%s | fzf%s %s",
             input_cmd, multi ? " -m" : "", fzf_opts ? fzf_opts : "");

    disable_raw_mode();
    FILE *fp = popen(pipebuf, "r");
    if (!fp) {
        enable_raw_mode();
        return 0;
    }

    int cap = 0, cnt = 0; char **list = NULL;
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        trim_eol(line);
        if (cnt + 1 > cap) { cap = cap ? cap * 2 : 8; list = realloc(list, (size_t)cap * sizeof(char*)); }
        list[cnt++] = strdup(line);
    }
    pclose(fp);
    enable_raw_mode();

    if (out_lines) *out_lines = list; else { for (int i = 0; i < cnt; i++) free(list[i]); free(list); }
    if (out_count) *out_count = cnt;
    return 1;
}

int fzf_run(const char *input_cmd, int multi, char ***out_lines, int *out_count) {
    return fzf_run_opts(input_cmd, NULL, multi, out_lines, out_count);
}

/* Helper to escape with single quotes for printf */
static void shell_escape_single(const char *in, char *out, size_t outsz) {
    size_t o = 0;
    if (o < outsz) out[o++] = '\'';
    for (const char *p = in; *p && o + 4 < outsz; p++) {
        if (*p == '\'') { out[o++] = '\''; out[o++] = '\\'; out[o++] = '\''; out[o++] = '\''; }
        else out[o++] = *p;
    }
    if (o < outsz) out[o++] = '\'';
    if (o < outsz) out[o] = '\0'; else out[outsz - 1] = '\0';
}

int fzf_pick_list(const char **items, int count, int multi, char ***out_lines, int *out_count) {
    if (!items || count <= 0) return 0;
    char pipebuf[8192]; size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "printf '%%s\\n' ");
    for (int i = 0; i < count; i++) {
        char esc[256]; shell_escape_single(items[i], esc, sizeof(esc));
        size_t elen = strlen(esc);
        if (off + elen + 2 >= sizeof(pipebuf)) break;
        memcpy(pipebuf + off, esc, elen); off += elen;
        pipebuf[off++] = ' ';
    }
    pipebuf[off] = '\0';
    return fzf_run(pipebuf, multi, out_lines, out_count);
}

void fzf_free(char **lines, int count) {
    if (!lines) return;
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
}
