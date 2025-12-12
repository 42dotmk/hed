#ifndef FILE_HELPERS_H
#define FILE_HELPERS_H
#include "sizedstr.h"
bool path_dirname(char *filename, SizedStr *out);
int path_is_absolute(const char *path);
bool path_exists(const char *path);

#endif // !FILE_HELPERS_H
