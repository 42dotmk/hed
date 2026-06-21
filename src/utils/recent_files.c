#include "utils/recent_files.h"
#include "fs/fs.h"
#include "lib/path_limits.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RECENT_FILES_MAX
#define RECENT_FILES_MAX 100
#endif

static const char *recent_files_filename = ".hed_recent_files";

static bool recent_files_path(char *out, size_t out_sz) {
    return fs_path_home_join(recent_files_filename, out, out_sz);
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

    char path[PATH_MAX];
    if (!recent_files_path(path, sizeof(path)))
        return;

    /* Concatenate items + '\n' into one buffer, then write atomically. */
    size_t total = 0;
    for (ptrdiff_t i = 0; i < arrlen(rf->items); i++)
        total += strlen(rf->items[i]) + 1;

    char *buf = malloc(total + 1);
    if (!buf)
        return;
    size_t off = 0;
    for (ptrdiff_t i = 0; i < arrlen(rf->items); i++) {
        size_t n = strlen(rf->items[i]);
        memcpy(buf + off, rf->items[i], n);
        off += n;
        buf[off++] = '\n';
    }
    if (fs_file_write_atomic(path, buf, off) != ED_OK)
        fs_file_write(path, buf, off);
    free(buf);
}

void recent_files_init(RecentFiles *rf) {
    if (!rf)
        return;

    rf->items = NULL;

    char path[PATH_MAX];
    if (!recent_files_path(path, sizeof(path)))
        return;

    FsLines *r = NULL;
    if (fs_lines_open(&r, path) != ED_OK)
        return;

    const char *line;
    size_t      len;
    while (fs_lines_next(r, &line, &len)) {
        if (len > 0)
            recent_files_append(rf, line);
        if ((int)arrlen(rf->items) >= RECENT_FILES_MAX)
            break;
    }
    fs_lines_close(r);

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
