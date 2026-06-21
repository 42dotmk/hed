#include "fs/fs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/path_limits.h"
#include "lib/strbuf.h"
#include "lib/strutil.h"

/* =====================================================================
 * Internal helpers
 * ===================================================================== */

static EdError errno_to_ed(int err, EdError default_err) {
    switch (err) {
    case 0:       return ED_OK;
    case ENOENT:  return ED_ERR_FILE_NOT_FOUND;
    case EACCES:
    case EPERM:   return ED_ERR_FILE_PERM;
    case ENOMEM:  return ED_ERR_NOMEM;
    case EINVAL:  return ED_ERR_INVALID_ARG;
    default:      return default_err;
    }
}

/* =====================================================================
 * Paths
 * ===================================================================== */

bool fs_path_is_absolute(const char *path) {
    if (!path || !*path)
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    if (((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':')
        return true;
    if (path[0] == '~')
        return true;
    return false;
}

static const char *fs__find_last_sep(const char *path) {
    if (!path)
        return NULL;
    const char *slash  = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash))
        return bslash;
    return slash;
}

bool fs_path_dirname(const char *path, StrBuf *out) {
    if (!path || !out)
        return false;
    const char *sep = fs__find_last_sep(path);
    if (!sep)
        return false;
    size_t len = (size_t)(sep - path);
    strbuf_free(out);
    *out = strbuf_from(path, len);
    return true;
}

void fs_path_dirname_buf(const char *path, char *out, size_t out_sz) {
    if (!out || out_sz == 0)
        return;
    out[0] = '\0';
    if (!path)
        return;
    const char *sep = fs__find_last_sep(path);
    if (!sep)
        return;
    size_t len = (size_t)(sep - path);
    if (len >= out_sz)
        len = out_sz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

const char *fs_path_basename(const char *path) {
    if (!path)
        return "";
    const char *sep = fs__find_last_sep(path);
    return sep ? sep + 1 : path;
}

const char *fs_path_extension(const char *path) {
    if (!path)
        return "";
    const char *base = fs_path_basename(path);
    const char *dot  = strrchr(base, '.');
    if (!dot || dot == base)
        return "";
    return dot + 1;
}

int fs_path_join(char *out, size_t out_sz, const char *dir, const char *path) {
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

void fs_path_expand_tilde(const char *in, char *out, size_t out_sz) {
    str_expand_tilde(in, out, out_sz);
}

bool fs_path_home_join(const char *name, char *out, size_t out_sz) {
    if (!out || out_sz == 0 || !name)
        return false;
    out[0] = '\0';
    const char *home = getenv("HOME");
    if (home && *home) {
        int n = snprintf(out, out_sz, "%s/%s", home, name);
        return n > 0 && (size_t)n < out_sz;
    }
    int n = snprintf(out, out_sz, "%s", name);
    return n > 0 && (size_t)n < out_sz;
}

char *fs_path_detect_filetype(const char *path) {
    if (!path)
        return strdup("txt");

    /* Special filenames first. */
    const char *base = fs_path_basename(path);
    if (strcmp(base, "makefile") == 0 || strcmp(base, "Makefile") == 0)
        return strdup("Makefile");

    const char *ext = fs_path_extension(path);
    if (!*ext)
        return strdup("txt");

    if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0)         return strdup("c");
    if (strcmp(ext, "cpp") == 0 || strcmp(ext, "cc") == 0 ||
        strcmp(ext, "cxx") == 0)                                 return strdup("cpp");
    if (strcmp(ext, "hpp") == 0 || strcmp(ext, "hh") == 0 ||
        strcmp(ext, "hxx") == 0)                                 return strdup("cpp");
    if (strcmp(ext, "py") == 0)                                  return strdup("python");
    if (strcmp(ext, "js") == 0)                                  return strdup("javascript");
    if (strcmp(ext, "ts") == 0)                                  return strdup("typescript");
    if (strcmp(ext, "tsx") == 0)                                 return strdup("typescript");
    if (strcmp(ext, "rs") == 0)                                  return strdup("rust");
    if (strcmp(ext, "sh") == 0)                                  return strdup("shell");
    if (strcmp(ext, "md") == 0)                                  return strdup("markdown");
    if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0)     return strdup("html");
    if (strcmp(ext, "cs") == 0 )     							 return strdup("csharp");

    return strdup(ext);
}

bool fs_find_root_marker(const char *start, const char *const *markers,
                         char *out, size_t out_sz) {
    if (!start || !markers || !out || out_sz == 0)
        return false;

    char dir[PATH_MAX];
    int n = snprintf(dir, sizeof(dir), "%s", start);
    if (n <= 0 || (size_t)n >= sizeof(dir))
        return false;

    /* If start is a regular file, begin at its parent directory. */
    if (fs_is_file(dir)) {
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) *slash = '\0';
        else if (slash == dir)     dir[1]  = '\0';
    }

    for (;;) {
        for (int i = 0; markers[i]; i++) {
            char probe[PATH_MAX];
            if (fs_path_join(probe, sizeof(probe), dir, markers[i]) &&
                fs_exists(probe)) {
                snprintf(out, out_sz, "%s", dir);
                return true;
            }
        }
        if (dir[0] == '/' && dir[1] == '\0')
            return false; /* reached root, no marker */
        char *slash = strrchr(dir, '/');
        if (!slash)
            return false;
        if (slash == dir) dir[1] = '\0';
        else              *slash = '\0';
    }
}

/* =====================================================================
 * Queries
 * ===================================================================== */

bool fs_exists(const char *path) {
    if (!path || !*path)
        return false;
    struct stat st;
    return stat(path, &st) == 0;
}

bool fs_is_dir(const char *path) {
    if (!path || !*path)
        return false;
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

bool fs_is_file(const char *path) {
    if (!path || !*path)
        return false;
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    return S_ISREG(st.st_mode);
}

bool fs_is_executable(const char *path) {
    if (!path || !*path)
        return false;
    return access(path, X_OK) == 0;
}

long fs_mtime(const char *path) {
    if (!path || !*path)
        return 0;
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return (long)st.st_mtime;
}

/* =====================================================================
 * Whole-file I/O
 * ===================================================================== */

EdError fs_file_read(const char *path, char **out, size_t *out_len) {
    if (!out)
        return ED_ERR_INVALID_ARG;
    *out = NULL;
    if (out_len)
        *out_len = 0;
    if (!path || !*path)
        return ED_ERR_INVALID_ARG;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return errno_to_ed(errno, ED_ERR_FILE_OPEN);

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return ED_ERR_FILE_READ;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return ED_ERR_FILE_READ;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return ED_ERR_FILE_READ;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return ED_ERR_NOMEM;
    }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (n != (size_t)sz) {
        free(buf);
        return ED_ERR_FILE_READ;
    }
    buf[sz] = '\0';
    *out = buf;
    if (out_len)
        *out_len = (size_t)sz;
    return ED_OK;
}

EdError fs_file_write(const char *path, const void *data, size_t len) {
    if (!path || !*path || (!data && len > 0))
        return ED_ERR_INVALID_ARG;
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return errno_to_ed(errno, ED_ERR_FILE_OPEN);
    if (len > 0 && fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        return ED_ERR_FILE_WRITE;
    }
    if (fclose(fp) != 0)
        return ED_ERR_FILE_WRITE;
    return ED_OK;
}

EdError fs_file_write_atomic(const char *path, const void *data, size_t len) {
    if (!path || !*path)
        return ED_ERR_INVALID_ARG;
    char tmp[PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp))
        return ED_ERR_INVALID_ARG;

    EdError err = fs_file_write(tmp, data, len);
    if (err != ED_OK) {
        unlink(tmp);
        return err;
    }
    if (rename(tmp, path) != 0) {
        EdError mapped = errno_to_ed(errno, ED_ERR_FILE_WRITE);
        unlink(tmp);
        return mapped;
    }
    return ED_OK;
}

/* =====================================================================
 * Line scanner
 * ===================================================================== */

struct FsLines {
    FILE  *fp;
    char  *line;
    size_t cap;
};

EdError fs_lines_open(FsLines **out, const char *path) {
    if (!out)
        return ED_ERR_INVALID_ARG;
    *out = NULL;
    if (!path || !*path)
        return ED_ERR_INVALID_ARG;
    FILE *fp = fopen(path, "r");
    if (!fp)
        return errno_to_ed(errno, ED_ERR_FILE_OPEN);
    FsLines *r = calloc(1, sizeof(*r));
    if (!r) {
        fclose(fp);
        return ED_ERR_NOMEM;
    }
    r->fp = fp;
    *out  = r;
    return ED_OK;
}

bool fs_lines_next(FsLines *r, const char **out_line, size_t *out_len) {
    if (!r || !r->fp)
        return false;
    ssize_t n = getline(&r->line, &r->cap, r->fp);
    if (n < 0)
        return false;
    while (n > 0 && (r->line[n - 1] == '\n' || r->line[n - 1] == '\r'))
        n--;
    r->line[n] = '\0';
    if (out_line)
        *out_line = r->line;
    if (out_len)
        *out_len = (size_t)n;
    return true;
}

void fs_lines_close(FsLines *r) {
    if (!r)
        return;
    if (r->fp)
        fclose(r->fp);
    free(r->line);
    free(r);
}

/* =====================================================================
 * Directory iterator
 * ===================================================================== */

struct FsDir {
    DIR   *dp;
    char  *base;        /* owned: full directory path for is_dir fallback */
    char   namebuf[PATH_MAX];
    char   pathbuf[PATH_MAX];
};

EdError fs_dir_open(FsDir **out, const char *path) {
    if (!out)
        return ED_ERR_INVALID_ARG;
    *out = NULL;
    if (!path || !*path)
        return ED_ERR_INVALID_ARG;
    DIR *dp = opendir(path);
    if (!dp)
        return errno_to_ed(errno, ED_ERR_FILE_OPEN);
    FsDir *d = calloc(1, sizeof(*d));
    if (!d) {
        closedir(dp);
        return ED_ERR_NOMEM;
    }
    d->dp   = dp;
    d->base = strdup(path);
    *out    = d;
    return ED_OK;
}

bool fs_dir_next(FsDir *d, FsDirEntry *out) {
    if (!d || !d->dp || !out)
        return false;
    struct dirent *de;
    while ((de = readdir(d->dp)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.' && (name[1] == '\0' ||
                               (name[1] == '.' && name[2] == '\0')))
            continue;

        size_t nl = strlen(name);
        if (nl >= sizeof(d->namebuf))
            nl = sizeof(d->namebuf) - 1;
        memcpy(d->namebuf, name, nl);
        d->namebuf[nl] = '\0';
        out->name = d->namebuf;

        bool is_dir = false;
#ifdef DT_DIR
        if (de->d_type == DT_DIR)
            is_dir = true;
        else if (de->d_type == DT_UNKNOWN || de->d_type == DT_LNK)
#endif
        {
            if (fs_path_join(d->pathbuf, sizeof(d->pathbuf),
                             d->base, d->namebuf)) {
                is_dir = fs_is_dir(d->pathbuf);
            }
        }
        out->is_dir = is_dir;
        return true;
    }
    return false;
}

void fs_dir_close(FsDir *d) {
    if (!d)
        return;
    if (d->dp)
        closedir(d->dp);
    free(d->base);
    free(d);
}

/* =====================================================================
 * Mutating operations
 * ===================================================================== */

EdError fs_mkdir(const char *path) {
    if (!path || !*path)
        return ED_ERR_INVALID_ARG;
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return errno_to_ed(errno, ED_ERR_FILE_WRITE);
    return ED_OK;
}

EdError fs_mkdir_p(const char *path) {
    if (!path || !*path)
        return ED_ERR_INVALID_ARG;
    if (fs_is_dir(path))
        return ED_OK;

    char buf[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(buf))
        return ED_ERR_INVALID_ARG;
    memcpy(buf, path, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST)
                return errno_to_ed(errno, ED_ERR_FILE_WRITE);
            buf[i] = '/';
        }
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST)
        return errno_to_ed(errno, ED_ERR_FILE_WRITE);
    return fs_is_dir(buf) ? ED_OK : ED_ERR_FILE_WRITE;
}

EdError fs_unlink(const char *path) {
    if (!path || !*path)
        return ED_ERR_INVALID_ARG;
    if (unlink(path) != 0)
        return errno_to_ed(errno, ED_ERR_FILE_WRITE);
    return ED_OK;
}

EdError fs_rmdir(const char *path) {
    if (!path || !*path)
        return ED_ERR_INVALID_ARG;
    if (rmdir(path) != 0)
        return errno_to_ed(errno, ED_ERR_FILE_WRITE);
    return ED_OK;
}

EdError fs_remove(const char *path) {
    if (!path || !*path)
        return ED_ERR_INVALID_ARG;
    return fs_is_dir(path) ? fs_rmdir(path) : fs_unlink(path);
}

EdError fs_rename(const char *from, const char *to) {
    if (!from || !*from || !to || !*to)
        return ED_ERR_INVALID_ARG;
    if (rename(from, to) != 0)
        return errno_to_ed(errno, ED_ERR_FILE_WRITE);
    return ED_OK;
}

/* =====================================================================
 * Working directory
 * ===================================================================== */

bool fs_getcwd(char *out, size_t out_sz) {
    if (!out || out_sz == 0)
        return false;
    out[0] = '\0';
    return getcwd(out, out_sz) != NULL;
}

EdError fs_chdir(const char *path) {
    if (!path || !*path)
        return ED_ERR_INVALID_ARG;
    if (chdir(path) != 0)
        return errno_to_ed(errno, ED_ERR_FILE_OPEN);
    return ED_OK;
}

/* =====================================================================
 * Temp files
 * ===================================================================== */

EdError fs_temp_path(const char *prefix, char *out, size_t out_sz) {
    if (!out || out_sz == 0)
        return ED_ERR_INVALID_ARG;
    out[0] = '\0';
    if (!prefix || !*prefix)
        prefix = "hed";

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";

    int n = snprintf(out, out_sz, "%s/%s.XXXXXX", tmpdir, prefix);
    if (n <= 0 || (size_t)n >= out_sz)
        return ED_ERR_INVALID_ARG;

    int fd = mkstemp(out);
    if (fd < 0) {
        out[0] = '\0';
        return errno_to_ed(errno, ED_ERR_FILE_OPEN);
    }
    close(fd);
    return ED_OK;
}

/* =====================================================================
 * Cache-file helper (depends on the rest of the module above).
 * ===================================================================== */

bool fs_path_cache_for_cwd(const char *name, char *out, size_t out_sz) {
    if (!out || out_sz == 0 || !name)
        return false;
    out[0] = '\0';

    const char *home = getenv("HOME");
    if (!home || !*home)
        return false;

    char cwd[PATH_MAX];
    if (!fs_getcwd(cwd, sizeof(cwd)))
        return false;

    char enc[PATH_MAX];
    size_t cwd_len = strlen(cwd);
    if (cwd_len >= sizeof(enc))
        return false;
    for (size_t i = 0; i < cwd_len; i++)
        enc[i] = (cwd[i] == '/') ? '%' : cwd[i];
    enc[cwd_len] = '\0';

    char dir[PATH_MAX];
    int n = snprintf(dir, sizeof(dir), "%s/.cache/hed/%s", home, enc);
    if (n <= 0 || (size_t)n >= sizeof(dir))
        return false;
    if (fs_mkdir_p(dir) != ED_OK)
        return false;

    n = snprintf(out, out_sz, "%s/%s", dir, name);
    return n > 0 && (size_t)n < out_sz;
}
