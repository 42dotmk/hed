#include "utils/recent_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RECENT_FILES_MAX
#define RECENT_FILES_MAX 100
#endif

static const char *recent_files_filename = ".hed_recent_files";

static char *recent_files_path(void) {
    const char *home = getenv("HOME");
    if (home && *home) {
        size_t len = strlen(home) + 1 + strlen(recent_files_filename) + 1;
        char *p = malloc(len);
        if (!p)
            return NULL;
        snprintf(p, len, "%s/%s", home, recent_files_filename);
        return p;
    }
    return strdup(recent_files_filename);
}

static void recent_files_clear_items(RecentFiles *rf) {
    if (!rf)
        return;
    for (ptrdiff_t i = 0; i < arrlen(rf->items); i++)
        free(rf->items[i]);
    arrfree(rf->items);
    rf->items = NULL;
}

/* Drop any duplicate paths, keeping the first occurrence (newest). */
static void recent_files_dedup(RecentFiles *rf) {
    if (!rf)
        return;
    for (ptrdiff_t i = 0; i < arrlen(rf->items); i++) {
        for (ptrdiff_t j = i + 1; j < arrlen(rf->items);) {
            if (strcmp(rf->items[i], rf->items[j]) == 0) {
                free(rf->items[j]);
                arrdel(rf->items, j);
            } else {
                j++;
            }
        }
    }
}

static void recent_files_insert_front(RecentFiles *rf, const char *filepath) {
    if (!filepath || !*filepath)
        return;

    char *path_copy = strdup(filepath);
    /* arrins expands stb_ds's grow macros which gcc flags with
     * -Wsign-compare on the size_t/ptrdiff_t ternary inside arraddnindex. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
    arrins(rf->items, 0, path_copy);
#pragma GCC diagnostic pop

    if (arrlen(rf->items) > RECENT_FILES_MAX) {
        char *oldest = arrpop(rf->items);
        free(oldest);
    }
}

static void recent_files_append(RecentFiles *rf, const char *filepath) {
    if (!filepath || !*filepath)
        return;
    if ((int)arrlen(rf->items) >= RECENT_FILES_MAX)
        return;

    char *path_copy = strdup(filepath);
    arrput(rf->items, path_copy);
}

static void recent_files_save(const RecentFiles *rf) {
    if (!rf)
        return;

    char *path = recent_files_path();
    if (!path)
        return;

    char tmppath[4096];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);

    FILE *fp = fopen(tmppath, "w");
    if (!fp) {
        fp = fopen(path, "w");
        if (!fp) {
            free(path);
            return;
        }
    }

    for (ptrdiff_t i = 0; i < arrlen(rf->items); i++) {
        fprintf(fp, "%s\n", rf->items[i]);
    }

    fclose(fp);

    if (strstr(tmppath, ".tmp")) {
        rename(tmppath, path);
    }

    free(path);
}

void recent_files_init(RecentFiles *rf) {
    if (!rf)
        return;

    rf->items = NULL;

    char *path = recent_files_path();
    if (!path)
        return;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        free(path);
        return;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t r;

    while ((r = getline(&line, &cap, fp)) != -1) {
        while (r > 0 && (line[r - 1] == '\n' || line[r - 1] == '\r'))
            r--;
        line[r] = '\0';

        if (r > 0) {
            recent_files_append(rf, line);
        }

        if ((int)arrlen(rf->items) >= RECENT_FILES_MAX)
            break;
    }

    free(line);
    fclose(fp);
    free(path);

    recent_files_dedup(rf);
}

void recent_files_free(RecentFiles *rf) { recent_files_clear_items(rf); }

void recent_files_add(RecentFiles *rf, const char *filepath) {
    if (!rf || !filepath || !*filepath)
        return;

    for (ptrdiff_t i = 0; i < arrlen(rf->items); i++) {
        if (strcmp(rf->items[i], filepath) == 0) {
            free(rf->items[i]);
            arrdel(rf->items, i);
            break;
        }
    }

    recent_files_insert_front(rf, filepath);
    recent_files_save(rf);
}

const char *recent_files_get(const RecentFiles *rf, int idx) {
    if (!rf || idx < 0 || (ptrdiff_t)idx >= arrlen(rf->items))
        return NULL;
    return rf->items[idx];
}

int recent_files_len(const RecentFiles *rf) {
    return rf ? (int)arrlen(rf->items) : 0;
}
