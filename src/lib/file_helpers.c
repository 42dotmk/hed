#include "file_helpers.h"
#include "sizedstr.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
int path_is_absolute(const char *path) {
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

bool path_dirname(char *filename, SizedStr *out) {
    if (!filename || !out)
        return false;
    const char *slash = strrchr(filename, '/');
    const char *bslash = strrchr(filename, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
    if (!slash)
        return false;
    size_t len = (size_t)(slash - filename);
    sstr_free(out);
    *out = sstr_from(filename, len);
    return true;
}

void path_dirname_buf(const char *filename, char *out, size_t out_sz) {
    if (!out || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    if (!filename)
        return;

    const char *slash = strrchr(filename, '/');
    const char *bslash = strrchr(filename, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
    if (!slash)
        return;

    size_t len = (size_t)(slash - filename);
    if (len >= out_sz)
        len = out_sz - 1;
    memcpy(out, filename, len);
    out[len] = '\0';
}

bool path_exists(const char *path) {
    if (!path || !*path)
        return false;
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

int path_join_dir(char *out, size_t out_sz, const char *dir, const char *path) {
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
