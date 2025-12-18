#ifndef FILE_HELPERS_H
#define FILE_HELPERS_H
#include "sizedstr.h"
bool path_dirname(char *filename, SizedStr *out);
void path_dirname_buf(const char *filename, char *out, size_t out_sz);
int path_is_absolute(const char *path);
int path_join_dir(char *out, size_t out_sz, const char *dir, const char *path);
bool path_exists(const char *path);
char* path_detect_filetype(const char *filename);

#endif // !FILE_HELPERS_H
