#ifndef RECENT_FILES_H
#define RECENT_FILES_H

/* Recent files tracking - no duplicates, newest first */

#include "stb_ds.h"

typedef struct {
    char **items; /* stb_ds dynamic array; newest at index 0 */
} RecentFiles;

/* Initialize and load from file */
void recent_files_init(RecentFiles *rf);
/* Free memory */
void recent_files_free(RecentFiles *rf);
/* Add a new file (removes duplicate if exists, then adds to front) */
void recent_files_add(RecentFiles *rf, const char *filepath);
/* Get item at index */
const char *recent_files_get(const RecentFiles *rf, int idx);
/* Get count */
int recent_files_len(const RecentFiles *rf);

#endif /* RECENT_FILES_H */
