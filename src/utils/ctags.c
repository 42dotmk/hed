#include "ctags.h"
#include "../hed.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern Ed E;

/* Helper: Find the tags file in current directory or buffer directory */
static int find_tags_file(char *out_path, size_t size) {
    Buffer *buf = buf_cur();

    /* Try current working directory first */
    if (E.cwd[0]) {
        snprintf(out_path, size, "%s/tags", E.cwd);
        FILE *f = fopen(out_path, "r");
        if (f) {
            fclose(f);
            return 1;
        }
    }

    /* Try buffer's directory if available */
    if (buf && buf->filename) {
        char *last_slash = strrchr(buf->filename, '/');
        if (last_slash) {
            size_t dir_len = last_slash - buf->filename;
            if (dir_len + 6 < size) { /* +6 for "/tags\0" */
                memcpy(out_path, buf->filename, dir_len);
                strcpy(out_path + dir_len, "/tags");
                FILE *f = fopen(out_path, "r");
                if (f) {
                    fclose(f);
                    return 1;
                }
            }
        }
    }

    /* Try plain "tags" in current directory */
    snprintf(out_path, size, "tags");
    FILE *f = fopen(out_path, "r");
    if (f) {
        fclose(f);
        return 1;
    }

    return 0;
}

/* Helper: Parse a ctags line into a TagEntry */
static TagEntry *parse_tag_line(const char *line) {
    if (!line || !*line)
        return NULL;

    /* Make a working copy */
    char *work = strdup(line);
    if (!work)
        return NULL;

    /* Format: TAG\tFILE\t/PATTERN/;"\tREST */
    //Split 
    char *tag = strtok(work, "\t");
    char *file = strtok(NULL, "\t");
    char *pattern = strtok(NULL, "\t");
    if (!tag || !file || !pattern) {
        free(work);
        return NULL;
    }
    //remove the trailing ;" and anything after it
    char *semicolon = strstr(pattern, ";\"");
    if (semicolon) {
        *semicolon = '\0';
    }

    /* Allocate TagEntry */
    TagEntry *entry = malloc(sizeof(TagEntry));
    if (!entry) {
        free(work);
        return NULL;
    }

    entry->tag = strdup(tag);
    entry->file = strdup(file);
    entry->pattern = strdup(pattern);

    log_msg("Parsed tag: %s, file: %s, pattern: %s", entry->tag, entry->file, entry->pattern);

    free(work);

    if (!entry->tag || !entry->file || !entry->pattern) {
        tag_entry_free(entry);
        return NULL;
    }

    return entry;
}

/* Helper: Strip regex delimiters and escape sequences */
static void normalize_pattern(char *pattern) {
    if (!pattern || !*pattern)
        return;

    size_t len = strlen(pattern);

    /* Remove leading /^ or / */
    if (pattern[0] == '/') {
        memmove(pattern, pattern + 1, len);
        len--;
        if (len > 0 && pattern[0] == '^') {
            memmove(pattern, pattern + 1, len);
            len--;
        }
    }

    /* Remove trailing $/ or / */
    if (len > 0 && pattern[len - 1] == '/') {
        pattern[len - 1] = '\0';
        len--;
    }
    if (len > 0 && pattern[len - 1] == '$') {
        pattern[len - 1] = '\0';
        len--;
    }

    /* Remove ;" suffix if present */
    char *semicolon = strstr(pattern, ";\"");
    if (semicolon) {
        *semicolon = '\0';
    }
}

/* Helper: Search for pattern in buffer and position cursor */
static int search_and_position(Buffer *buf, Window *win, const char *pattern) {
    if (!buf || !win || !pattern || !*pattern)
        return 0;

    /* Make a working copy and normalize it */
    char *search_pat = strdup(pattern);
    if (!search_pat)
        return 0;

    normalize_pattern(search_pat);

    /* Search through buffer rows */
    for (int y = 0; y < buf->num_rows; y++) {
        Row *row = &buf->rows[y];
        if (!row->chars.data)
            continue;

        /* Simple substring search */
        char *found = strstr(row->chars.data, search_pat);
        if (found) {
            /* Position cursor at the match */
            win->cursor.y = y;
            win->cursor.x = found - row->chars.data;

            /* Adjust row offset to make the line visible */
            if (win->cursor.y < win->row_offset) {
                win->row_offset = win->cursor.y;
            }
            if (win->cursor.y >= win->row_offset + win->height) {
                win->row_offset = win->cursor.y - win->height + 1;
            }

            free(search_pat);
            return 1;
        }
    }

    free(search_pat);
    return 0;
}

TagEntry *find_tag(const char *tag_name) {
    if (!tag_name || !*tag_name)
        return NULL;

    /* Find tags file */
    char tags_path[1024];
    if (!find_tags_file(tags_path, sizeof(tags_path))) {
        ed_set_status_message("tags file not found");
        return NULL;
    }

    /* Build rg command to search for the tag */
    /* We search for lines starting with "tag_name\t" */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "rg --no-heading --color=never --max-count=1 '^%s\t' %s",
             tag_name, tags_path);

    /* Run the command */
    char **lines = NULL;
    int count = 0;
    if (!term_cmd_run(cmd, &lines, &count) || count == 0) {
        term_cmd_free(lines, count);
        return NULL;
    }

    /* Parse the first result */
    TagEntry *entry = parse_tag_line(lines[0]);
    term_cmd_free(lines, count);

    return entry;
}

int goto_tag(const char *tag_name) {
    char tag_buf[256];

    /* If no tag_name provided, get word under cursor */
    if (!tag_name || !*tag_name) {
        SizedStr word = sstr_new();
        if (!buf_get_word_under_cursor(&word) || word.len == 0) {
            sstr_free(&word);
            ed_set_status_message("No tag name provided and no word under cursor");
            return 0;
        }

        size_t copy_len = word.len;
        if (copy_len >= sizeof(tag_buf))
            copy_len = sizeof(tag_buf) - 1;
        memcpy(tag_buf, word.data, copy_len);
        tag_buf[copy_len] = '\0';
        sstr_free(&word);

        tag_name = tag_buf;
    }

    /* Find the tag */
    TagEntry *entry = find_tag(tag_name);
    if (!entry) {
        ed_set_status_message("Tag not found: %s", tag_name);
        return 0;
    }

    /* Open the file */
    buf_open_or_switch(entry->file, true);

    Buffer *buf = buf_cur();
    Window *win = window_cur();

    if (!buf || !win) {
        ed_set_status_message("Failed to open file: %s", entry->file);
        tag_entry_free(entry);
        return 0;
    }

    /* Search for the pattern and position cursor */
    if (search_and_position(buf, win, entry->pattern)) {
        ed_set_status_message("Found tag: %s in %s", tag_name, entry->file);
    } else {
        ed_set_status_message("Tag found but pattern not matched: %s", tag_name);
    }

    tag_entry_free(entry);
    return 1;
}

int tag_exists(const char *tag_name) {
    TagEntry *entry = find_tag(tag_name);
    if (entry) {
        tag_entry_free(entry);
        return 1;
    }
    return 0;
}

void tag_entry_free(TagEntry *entry) {
    if (!entry)
        return;

    if (entry->tag)
        free(entry->tag);
    if (entry->file)
        free(entry->file);
    if (entry->pattern)
        free(entry->pattern);
    free(entry);
}
