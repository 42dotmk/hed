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

char* path_detect_filetype(const char *filename) {
    if (!filename)
        return strdup("txt");

    const char *ext = strrchr(filename, '.');
    if (!ext || ext == filename)
        return strdup("txt");

    ext++; /* Skip the dot */
    if (strcmp(filename, "makefile") == 0 || strcmp(filename, "Makefile") == 0)
        return strdup("Makefile");
    /* Common filetypes */
    if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0)
        return strdup("c");
    if (strcmp(ext, "cpp") == 0 || strcmp(ext, "cc") == 0 ||
        strcmp(ext, "cxx") == 0)
        return strdup("cpp");
    if (strcmp(ext, "hpp") == 0 || strcmp(ext, "hh") == 0 ||
        strcmp(ext, "hxx") == 0)
        return strdup("cpp");
    if (strcmp(ext, "py") == 0)
        return strdup("python");
    if (strcmp(ext, "js") == 0)
        return strdup("javascript");
    if (strcmp(ext, "ts") == 0)
        return strdup("typescript");
    if (strcmp(ext, "java") == 0)
        return strdup("java");
    if (strcmp(ext, "rs") == 0)
        return strdup("rust");
    if (strcmp(ext, "go") == 0)
        return strdup("go");
    if (strcmp(ext, "sh") == 0)
        return strdup("shell");
    if (strcmp(ext, "md") == 0)
        return strdup("markdown");
    if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0)
        return strdup("html");
    if (strcmp(ext, "css") == 0)
        return strdup("css");
    if (strcmp(ext, "json") == 0)
        return strdup("json");
    if (strcmp(ext, "xml") == 0)
        return strdup("xml");
    if (strcmp(ext, "txt") == 0)
        return strdup("txt");

    /* Default: use the extension as-is */
    return strdup(ext);
}
