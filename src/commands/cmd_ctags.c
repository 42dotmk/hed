#include "cmd_ctags.h"
#include "../hed.h"
#include "cmd_util.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_absolute_path(const char *path) {
    if (!path || !*path)
        return 0;
    if (path[0] == '/' || path[0] == '\\')
        return 1;
    if (((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':')
        return 1;
    if (path[0] == '~')
        return 1;
    return 0;
}

static void path_dirname(const char *path, char *out, size_t out_sz) {
    if (!out || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    if (!path || !*path)
        return;

    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
    if (!slash)
        return;

    size_t len = (size_t)(slash - path);
    if (len >= out_sz)
        len = out_sz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

static int join_dir_and_path(char *out, size_t out_sz, const char *dir,
                             const char *path) {
    if (!out || out_sz == 0)
        return 0;
    out[0] = '\0';
    if (!path || !*path)
        return 0;
    if (!dir || !*dir) {
        int n = snprintf(out, out_sz, "%s", path);
        return n > 0 && n < (int)out_sz;
    }
    size_t dlen = strlen(dir);
    const char *sep = "";
    if (dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\')
        sep = "/";
    int n = snprintf(out, out_sz, "%s%s%s", dir, sep, path);
    return n > 0 && n < (int)out_sz;
}

static int file_exists(const char *path) {
    if (!path || !*path)
        return 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

static int find_tags_path(char *out, size_t out_sz) {
    if (!out || out_sz == 0)
        return 0;
    char candidate[PATH_MAX];

    Buffer *buf = buf_cur();
    if (buf && buf->filename) {
        char dir[PATH_MAX];
        path_dirname(buf->filename, dir, sizeof(dir));
        if (dir[0] &&
            join_dir_and_path(candidate, sizeof(candidate), dir, "tags") &&
            file_exists(candidate)) {
            return safe_strcpy(out, candidate, out_sz) == ED_OK;
        }
    }

    if (E.cwd[0] &&
        join_dir_and_path(candidate, sizeof(candidate), E.cwd, "tags") &&
        file_exists(candidate)) {
        return safe_strcpy(out, candidate, out_sz) == ED_OK;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) &&
        join_dir_and_path(candidate, sizeof(candidate), cwd, "tags") &&
        file_exists(candidate)) {
        return safe_strcpy(out, candidate, out_sz) == ED_OK;
    }

    if (file_exists("tags")) {
        return safe_strcpy(out, "tags", out_sz) == ED_OK;
    }
    return 0;
}

static int resolve_tag_path(const char *tags_path, const char *file_field,
                            char *out, size_t out_sz) {
    if (!file_field || !*file_field || !out || out_sz == 0)
        return 0;
    if (is_absolute_path(file_field)) {
        return safe_strcpy(out, file_field, out_sz) == ED_OK;
    }
    char dir[PATH_MAX];
    path_dirname(tags_path, dir, sizeof(dir));
    const char *base = dir[0] ? dir : NULL;
    return join_dir_and_path(out, out_sz, base, file_field);
}

static int parse_ctags_entry(char *entry, char *out_file, size_t file_sz,
                             char *out_excmd, size_t excmd_sz) {
    if (!entry || !out_file || !out_excmd)
        return 0;
    char *tab1 = strchr(entry, '\t');
    if (!tab1)
        return 0;
    char *file = tab1 + 1;
    *tab1 = '\0';

    char *tab2 = strchr(file, '\t');
    if (!tab2)
        return 0;
    *tab2 = '\0';
    char *excmd = tab2 + 1;

    char *tab3 = strchr(excmd, '\t');
    if (tab3)
        *tab3 = '\0';

    size_t elen = strlen(excmd);
    if (elen >= 2 && excmd[elen - 2] == ';' && excmd[elen - 1] == '"')
        excmd[elen - 2] = '\0';

    if (safe_strcpy(out_file, file, file_sz) != ED_OK)
        return 0;
    if (safe_strcpy(out_excmd, excmd, excmd_sz) != ED_OK)
        return 0;
    return 1;
}

static int rg_first_line_number(const char *pattern, const char *file_path,
                                const char *extra_flags) {
    if (!pattern || !*pattern || !file_path || !*file_path)
        return 0;
    char esc_pat[2048];
    shell_escape_single(pattern, esc_pat, sizeof(esc_pat));
    char esc_file[PATH_MAX * 2];
    shell_escape_single(file_path, esc_file, sizeof(esc_file));

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "rg --no-heading --color=never --line-number --max-count 1 %s "
             "-e %s %s",
             extra_flags ? extra_flags : "", esc_pat, esc_file);

    char **lines = NULL;
    int cnt = 0;
    int line_no = 0;
    if (term_cmd_run(cmd, &lines, &cnt) && cnt > 0 && lines[0]) {
        char *line = lines[0];
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            line_no = atoi(line);
        }
    }
    term_cmd_free(lines, cnt);
    return line_no;
}

void cmd_tag(const char *args) {
    char symbol[512];
    symbol[0] = '\0';
    if (args && *args) {
        str_trim_whitespace(args, symbol, sizeof(symbol));
    }

    if (!symbol[0]) {
        SizedStr w = sstr_new();
        if (!buf_get_word_under_cursor(&w) || w.len == 0) {
            sstr_free(&w);
            ed_set_status_message("gd: no word under cursor");
            return;
        }
        size_t n = w.len;
        if (n >= sizeof(symbol))
            n = sizeof(symbol) - 1;
        memcpy(symbol, w.data, n);
        symbol[n] = '\0';
        sstr_free(&w);
    }

    char tags_path[PATH_MAX];
    if (!find_tags_path(tags_path, sizeof(tags_path))) {
        ed_set_status_message("gd: tags file not found");
        return;
    }

    char tag_pattern[1024];
    int pat_len = snprintf(tag_pattern, sizeof(tag_pattern), "%s\t", symbol);
    if (pat_len <= 0 || pat_len >= (int)sizeof(tag_pattern)) {
        ed_set_status_message("gd: tag name too long");
        return;
    }

    char esc_pattern[2048];
    shell_escape_single(tag_pattern, esc_pattern, sizeof(esc_pattern));
    char esc_tags[PATH_MAX * 2];
    shell_escape_single(tags_path, esc_tags, sizeof(esc_tags));

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "rg --no-heading --color=never --line-number --max-count 1 "
             "--fixed-strings -- %s %s",
             esc_pattern, esc_tags);

    char **lines = NULL;
    int cnt = 0;
    if (!term_cmd_run(cmd, &lines, &cnt) || cnt <= 0) {
        term_cmd_free(lines, cnt);
        ed_set_status_message("gd: tag not found");
        return;
    }

    char *ctags_line = lines[0];
    char *entry = strchr(ctags_line, ':');
    if (entry && entry[1]) {
        entry++;
    } else {
        entry = ctags_line;
    }

    char file_field[PATH_MAX];
    char excmd[2048];
    if (!parse_ctags_entry(entry, file_field, sizeof(file_field), excmd,
                           sizeof(excmd))) {
        term_cmd_free(lines, cnt);
        ed_set_status_message("gd: failed to parse tag entry");
        return;
    }
    term_cmd_free(lines, cnt);

    char target_path[PATH_MAX];
    if (!resolve_tag_path(tags_path, file_field, target_path,
                          sizeof(target_path))) {
        ed_set_status_message("gd: invalid tag path");
        return;
    }

    int target_line = 0;
    if (isdigit((unsigned char)excmd[0])) {
        target_line = atoi(excmd);
    } else {
        char delim = excmd[0];
        const char *pattern = excmd;
        if ((delim == '/' || delim == '?') && excmd[1]) {
            pattern = excmd + 1;
            size_t len = strlen(pattern);
            if (len > 0 && pattern[len - 1] == delim) {
                ((char *)pattern)[len - 1] = '\0';
            }
        }

        if (pattern && *pattern) {
            target_line = rg_first_line_number(pattern, target_path, "");
        }
        if (target_line == 0 && symbol[0]) {
            target_line = rg_first_line_number(
                symbol, target_path, "--word-regexp --fixed-strings");
        }
    }

    buf_open_or_switch(target_path);
    if (target_line > 0) {
        buf_goto_line(target_line);
    }
    if (target_line > 0) {
        ed_set_status_message("gd: %s:%d", target_path, target_line);
    } else {
        ed_set_status_message("gd: opened %s", target_path);
    }
}
