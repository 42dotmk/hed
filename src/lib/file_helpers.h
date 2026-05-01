#ifndef FILE_HELPERS_H
#define FILE_HELPERS_H
#include <stdbool.h>
#include "lib/sizedstr.h"
bool path_dirname(char *filename, SizedStr *out);
void path_dirname_buf(const char *filename, char *out, size_t out_sz);
int path_is_absolute(const char *path);
int path_join_dir(char *out, size_t out_sz, const char *dir, const char *path);
bool path_exists(const char *path);
bool path_is_dir(const char *path);
char* path_detect_filetype(const char *filename);

/* mkdir -p (best effort). Returns true if the directory exists at the
 * end, regardless of whether we created it. */
bool path_mkdir_p(const char *path);

/* Per-cwd cache file under ~/.cache/hed/<encoded-cwd>/<name>.
 * The cwd is encoded by replacing '/' with '%'. Ensures the
 * containing directory exists. Writes the full path into `out`. */
bool path_cache_file_for_cwd(const char *name, char *out, size_t out_sz);

#endif // !FILE_HELPERS_H
