#ifndef HED_FS_H
#define HED_FS_H

/*
 * fs — filesystem façade for the core and plugins.
 *
 * Everything that touches the filesystem (paths, whole-file I/O,
 * line scanning, directory iteration, mkdir/rename/unlink) should
 * go through this module. Keeping the platform-specific calls
 * (POSIX today, Win32 tomorrow) behind one boundary is what makes
 * a future Windows port realistic.
 *
 * Naming:
 *   fs_path_*    pure path-string operations (no I/O)
 *   fs_file_*    whole-file read/write
 *   fs_lines_*   streaming line reader
 *   fs_dir_*     directory iterator
 *   fs_*         everything else: cwd, mkdir, unlink, rename, ...
 */

#include <stdbool.h>
#include <stddef.h>

#include "lib/errors.h"
#include "lib/sizedstr.h"

/* =====================================================================
 * Paths — pure string operations, no I/O.
 * ===================================================================== */

/* Absolute path? Recognises Unix '/', Windows 'X:', and leading '~'. */
bool fs_path_is_absolute(const char *path);

/* Directory portion (everything before the final separator). */
bool fs_path_dirname(const char *path, SizedStr *out);
void fs_path_dirname_buf(const char *path, char *out, size_t out_sz);

/* Basename (everything after the final separator), or `path` itself
 * if there is no separator. Returns a pointer into `path`. */
const char *fs_path_basename(const char *path);

/* Extension excluding the dot (e.g. "tar.gz" → "gz"). Returns "" if
 * none, or if the dot is the first character (".hidden"). */
const char *fs_path_extension(const char *path);

/* Join `dir` + separator + `path` into `out`. Returns 1 on success,
 * 0 if `out` would overflow or args are invalid. */
int fs_path_join(char *out, size_t out_sz, const char *dir, const char *path);

/* Expand a leading "~" or "~/" to $HOME. Falls back to copying `in`. */
void fs_path_expand_tilde(const char *in, char *out, size_t out_sz);

/* "$HOME/<name>" into `out`. Falls back to "<name>" if HOME is unset. */
bool fs_path_home_join(const char *name, char *out, size_t out_sz);

/* "~/.cache/hed/<encoded-cwd>/<name>", creating the directory.
 * The cwd is encoded by replacing '/' with '%'. */
bool fs_path_cache_for_cwd(const char *name, char *out, size_t out_sz);

/* Filetype tag from filename. Returned string is malloc'd. */
char *fs_path_detect_filetype(const char *path);

/* =====================================================================
 * Queries — predicates that hit the filesystem.
 * ===================================================================== */

bool fs_exists(const char *path);
bool fs_is_dir(const char *path);
bool fs_is_file(const char *path);
bool fs_is_executable(const char *path);

/* Last-modified time as seconds since the Unix epoch. Returns 0 if
 * `path` doesn't exist or can't be stat'd — callers comparing two
 * paths' mtimes should check fs_exists first if they need to
 * distinguish "missing" from "ancient". */
long fs_mtime(const char *path);

/* =====================================================================
 * Whole-file I/O.
 * ===================================================================== */

/* Read entire file into a malloc'd buffer. Caller frees with free().
 * The returned buffer is nul-terminated for convenience; *out_len is
 * the byte count not counting that terminator. */
EdError fs_file_read(const char *path, char **out, size_t *out_len);

/* Truncate + write `len` bytes to `path`. Not atomic. */
EdError fs_file_write(const char *path, const void *data, size_t len);

/* Atomic write: writes to "<path>.tmp" then renames over `path`.
 * On any failure, `path` is left untouched. */
EdError fs_file_write_atomic(const char *path, const void *data, size_t len);

/* =====================================================================
 * Line-by-line scanning.
 *
 *   FsLines *r = NULL;
 *   if (fs_lines_open(&r, path) == ED_OK) {
 *       const char *line; size_t len;
 *       while (fs_lines_next(r, &line, &len)) {
 *           // line is owned by r, valid until next call.
 *       }
 *       fs_lines_close(r);
 *   }
 *
 * Trailing "\n" and "\r\n" are stripped from each line.
 * ===================================================================== */

typedef struct FsLines FsLines;

EdError fs_lines_open(FsLines **out, const char *path);
bool fs_lines_next(FsLines *r, const char **out_line, size_t *out_len);
void fs_lines_close(FsLines *r);

/* =====================================================================
 * Directory iteration.
 *
 *   FsDir *d = NULL;
 *   if (fs_dir_open(&d, ".") == ED_OK) {
 *       FsDirEntry e;
 *       while (fs_dir_next(d, &e)) {
 *           // e.name is owned by d, valid until next call.
 *       }
 *       fs_dir_close(d);
 *   }
 *
 * "." and ".." are skipped.
 * ===================================================================== */

typedef struct FsDir FsDir;

typedef struct {
    const char *name;
    bool        is_dir;
} FsDirEntry;

EdError fs_dir_open(FsDir **out, const char *path);
bool    fs_dir_next(FsDir *d, FsDirEntry *out);
void    fs_dir_close(FsDir *d);

/* =====================================================================
 * Mutating operations.
 * ===================================================================== */

/* mkdir(path, 0755). */
EdError fs_mkdir(const char *path);

/* mkdir -p (best effort). Returns ED_OK if dir exists at the end. */
EdError fs_mkdir_p(const char *path);

/* unlink — remove a regular file. */
EdError fs_unlink(const char *path);

/* rmdir — remove an empty directory. */
EdError fs_rmdir(const char *path);

/* Remove `path` whether file or empty dir. Does NOT recurse. */
EdError fs_remove(const char *path);

/* rename `from` → `to`. */
EdError fs_rename(const char *from, const char *to);

/* =====================================================================
 * Working directory.
 * ===================================================================== */

bool    fs_getcwd(char *out, size_t out_sz);
EdError fs_chdir(const char *path);

/* =====================================================================
 * Temp files.
 * ===================================================================== */

/* Build a unique temp-file path under $TMPDIR (or /tmp), create the
 * file empty so the name is reserved, and write the path into `out`.
 *
 * `prefix` is a short tag (e.g. "hed_picker") used to make the file
 * recognisable; six random characters are appended. The caller owns
 * the file from this point on (open it for writing, then fs_unlink). */
EdError fs_temp_path(const char *prefix, char *out, size_t out_sz);

#endif /* HED_FS_H */
