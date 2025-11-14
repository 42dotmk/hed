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
        if (!p) return NULL;
        snprintf(p, len, "%s/%s", home, recent_files_filename);
        return p;
    }
    return strdup(recent_files_filename);
}

static void recent_files_clear_items(RecentFiles *rf) {
    if (!rf) return;
    for (int i = 0; i < rf->len; i++) free(rf->items[i]);
    free(rf->items);
    rf->items = NULL;
    rf->len = 0;
    rf->cap = 0;
}

/* Remove item at index, shifting remaining items */
static void recent_files_remove_at(RecentFiles *rf, int idx) {
    if (!rf || idx < 0 || idx >= rf->len) return;
    free(rf->items[idx]);
    /* Shift remaining items down */
    for (int i = idx; i < rf->len - 1; i++) {
        rf->items[i] = rf->items[i + 1];
    }
    rf->len--;
}

/* Insert at front (index 0) */
static void recent_files_insert_front(RecentFiles *rf, const char *filepath) {
    if (!filepath || !*filepath) return;

    /* Ensure capacity */
    if (rf->len + 1 > rf->cap) {
        int ncap = rf->cap == 0 ? 64 : rf->cap * 2;
        char **n = realloc(rf->items, ncap * sizeof(char*));
        if (!n) return;
        rf->items = n;
        rf->cap = ncap;
    }

    /* Shift all items down */
    memmove(&rf->items[1], &rf->items[0], sizeof(char*) * rf->len);
    rf->items[0] = strdup(filepath);
    rf->len++;

    /* Enforce max limit */
    if (rf->len > RECENT_FILES_MAX) {
        free(rf->items[RECENT_FILES_MAX]);
        rf->len = RECENT_FILES_MAX;
    }
}

/* Append (used when loading from file) */
static void recent_files_append(RecentFiles *rf, const char *filepath) {
    if (!filepath || !*filepath) return;
    if (rf->len >= RECENT_FILES_MAX) return;

    if (rf->len + 1 > rf->cap) {
        int ncap = rf->cap == 0 ? 64 : rf->cap * 2;
        char **n = realloc(rf->items, ncap * sizeof(char*));
        if (!n) return;
        rf->items = n;
        rf->cap = ncap;
    }

    rf->items[rf->len++] = strdup(filepath);
}

/* Save entire list to file */
static void recent_files_save(const RecentFiles *rf) {
    if (!rf) return;

    char *path = recent_files_path();
    if (!path) return;

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
    for (int i = 0; i < rf->len; i++) {
        fprintf(fp, "%s\n", rf->items[i]);
    }

    fclose(fp);

    /* Atomic rename if we used temp file */
    if (strstr(tmppath, ".tmp")) {
        rename(tmppath, path);
    }

    free(path);
}

void recent_files_init(RecentFiles *rf) {
    if (!rf) return;

    rf->items = NULL;
    rf->len = 0;
    rf->cap = 0;

    char *path = recent_files_path();
    if (!path) return;

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
        while (r > 0 && (line[r-1] == '\n' || line[r-1] == '\r')) r--;
        line[r] = '\0';

        if (r > 0) {
            recent_files_append(rf, line);
        }

        if (rf->len >= RECENT_FILES_MAX) break;
    }

    free(line);
    fclose(fp);
    free(path);
}

void recent_files_free(RecentFiles *rf) {
    recent_files_clear_items(rf);
}

void recent_files_add(RecentFiles *rf, const char *filepath) {
    if (!rf || !filepath || !*filepath) return;

    /* Check if file already exists in list and remove it */
    for (int i = 0; i < rf->len; i++) {
        if (strcmp(rf->items[i], filepath) == 0) {
            recent_files_remove_at(rf, i);
            break;
        }
    }

    /* Add to front */
    recent_files_insert_front(rf, filepath);

    /* Persist to file */
    recent_files_save(rf);
}

const char *recent_files_get(const RecentFiles *rf, int idx) {
    if (!rf || idx < 0 || idx >= rf->len) return NULL;
    return rf->items[idx];
}

int recent_files_len(const RecentFiles *rf) {
    return rf ? rf->len : 0;
}
