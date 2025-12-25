#include "hed.h"

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
    for (size_t i = 0; i < rf->items.len; i++)
        free(rf->items.data[i]);
    free(rf->items.data);
    rf->items.data = NULL;
    rf->items.len = 0;
    rf->items.cap = 0;
}

/* Drop any duplicate paths, keeping the first occurrence (newest). */
static void recent_files_dedup(RecentFiles *rf) {
    if (!rf)
        return;
    for (int i = 0; i < (int)rf->items.len; i++) {
        for (int j = i + 1; j < (int)rf->items.len;) {
            if (strcmp(rf->items.data[i], rf->items.data[j]) == 0) {
                free(rf->items.data[j]);
                vec_remove(&rf->items, char *, j);
            } else {
                j++;
            }
        }
    }
}

/* Insert at front (index 0) */
static void recent_files_insert_front(RecentFiles *rf, const char *filepath) {
    if (!filepath || !*filepath)
        return;

    char *path_copy = strdup(filepath);
    vec_push_start_typed(&rf->items, char *, path_copy);

    /* Enforce max limit */
    if (rf->items.len > RECENT_FILES_MAX) {
        char *oldest = vec_pop_typed(&rf->items, char *);
        free(oldest);
    }
}

/* Append (used when loading from file) */
static void recent_files_append(RecentFiles *rf, const char *filepath) {
    if (!filepath || !*filepath)
        return;
    if ((int)rf->items.len >= RECENT_FILES_MAX)
        return;

    char *path_copy = strdup(filepath);
    vec_push_typed(&rf->items, char *, path_copy);
}

/* Save entire list to file */
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
        /* Try direct write if temp fails */
        fp = fopen(path, "w");
        if (!fp) {
            free(path);
            return;
        }
    }

    /* Write all items, newest first */
    for (size_t i = 0; i < rf->items.len; i++) {
        fprintf(fp, "%s\n", rf->items.data[i]);
    }

    fclose(fp);

    /* Atomic rename if we used temp file */
    if (strstr(tmppath, ".tmp")) {
        rename(tmppath, path);
    }

    free(path);
}

void recent_files_init(RecentFiles *rf) {
    if (!rf)
        return;

    rf->items.data = NULL;
    rf->items.len = 0;
    rf->items.cap = 0;

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
        /* Trim newlines */
        while (r > 0 && (line[r - 1] == '\n' || line[r - 1] == '\r'))
            r--;
        line[r] = '\0';

        if (r > 0) {
            recent_files_append(rf, line);
        }

        if ((int)rf->items.len >= RECENT_FILES_MAX)
            break;
    }

    free(line);
    fclose(fp);
    free(path);

    /* Clean up any duplicates that may have been persisted previously. */
    recent_files_dedup(rf);
}

void recent_files_free(RecentFiles *rf) { recent_files_clear_items(rf); }

void recent_files_add(RecentFiles *rf, const char *filepath) {
    if (!rf || !filepath || !*filepath)
        return;

    /* Check if file already exists in list and remove it */
    for (int i = 0; i < (int)rf->items.len; i++) {
        if (strcmp(rf->items.data[i], filepath) == 0) {
            free(rf->items.data[i]);
            vec_remove(&rf->items, char *, i);
            break;
        }
    }

    /* Add to front */
    recent_files_insert_front(rf, filepath);

    /* Persist to file */
    recent_files_save(rf);
}

const char *recent_files_get(const RecentFiles *rf, int idx) {
    if (!rf || idx < 0 || idx >= (int)rf->items.len)
        return NULL;
    return rf->items.data[idx];
}

int recent_files_len(const RecentFiles *rf) {
    return rf ? (int)rf->items.len : 0;
}
