#include "../hed.h"
#include "autocomplete.h"
#include "../ui/winmodal.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    Window *modal;          /* Modal window for completions */
    int completion_buffer;  /* Buffer index containing completions */
    int selected_index;     /* Currently selected completion (0-based) */
    char prefix[256];       /* The partial word being completed */
    int prefix_len;         /* Length of prefix */
    int prefix_start_x;     /* Column where prefix starts */
} AutocompleteState;

static AutocompleteState ac_state = {0};

void autocomplete_init(void) {
    ac_state.modal = NULL;
    ac_state.completion_buffer = -1;
    ac_state.selected_index = 0;
    ac_state.prefix[0] = '\0';
    ac_state.prefix_len = 0;
    ac_state.prefix_start_x = 0;
}

/* Check if character is part of a word */
static int is_word_char(char c) {
    return isalnum(c) || c == '_';
}

/* Extract the word prefix before the cursor */
static void extract_prefix(Buffer *buf, char *prefix, int max_len, int *start_x) {
    if (buf->cursor.y >= buf->num_rows) {
        prefix[0] = '\0';
        *start_x = 0;
        return;
    }

    Row *row = &buf->rows[buf->cursor.y];
    int x = buf->cursor.x;

    /* Find start of word */
    int start = x;
    while (start > 0 && is_word_char(row->chars.data[start - 1])) {
        start--;
    }

    *start_x = start;
    int len = x - start;
    if (len >= max_len)
        len = max_len - 1;

    if (len > 0) {
        memcpy(prefix, &row->chars.data[start], len);
    }
    prefix[len] = '\0';
}

/* Find unique completions in the buffer */
static void find_completions(Buffer *src_buf, const char *prefix, Buffer *comp_buf) {
    if (!prefix || !*prefix)
        return;

    int prefix_len = strlen(prefix);

    /* Simple hash set to track unique words (using array of strings) */
    char **unique_words = NULL;
    int unique_count = 0;
    int unique_cap = 0;

    /* Search through all rows in source buffer */
    for (int y = 0; y < src_buf->num_rows; y++) {
        Row *row = &src_buf->rows[y];
        char *line = row->chars.data;
        int line_len = row->chars.len;

        /* Extract words from line */
        for (int i = 0; i < line_len; ) {
            /* Skip non-word characters */
            while (i < line_len && !is_word_char(line[i])) {
                i++;
            }

            if (i >= line_len)
                break;

            /* Extract word */
            int word_start = i;
            while (i < line_len && is_word_char(line[i])) {
                i++;
            }
            int word_len = i - word_start;

            /* Check if word matches prefix */
            if (word_len >= prefix_len &&
                memcmp(&line[word_start], prefix, prefix_len) == 0) {

                /* Check if word is not exactly the prefix */
                if (word_len == prefix_len)
                    continue;

                /* Check for duplicates */
                int is_duplicate = 0;
                for (int j = 0; j < unique_count; j++) {
                    if (strlen(unique_words[j]) == (size_t)word_len &&
                        memcmp(unique_words[j], &line[word_start], word_len) == 0) {
                        is_duplicate = 1;
                        break;
                    }
                }

                if (!is_duplicate) {
                    /* Add to unique words */
                    if (unique_count >= unique_cap) {
                        unique_cap = unique_cap == 0 ? 16 : unique_cap * 2;
                        unique_words = realloc(unique_words, unique_cap * sizeof(char *));
                    }

                    unique_words[unique_count] = malloc(word_len + 1);
                    memcpy(unique_words[unique_count], &line[word_start], word_len);
                    unique_words[unique_count][word_len] = '\0';
                    unique_count++;
                }
            }
        }
    }

    /* Add unique words to completion buffer */
    for (int i = 0; i < unique_count; i++) {
        buf_row_insert_in(comp_buf, comp_buf->num_rows, unique_words[i], strlen(unique_words[i]));
        free(unique_words[i]);
    }

    free(unique_words);
}

void autocomplete_trigger(void) {
    if (autocomplete_is_active()) {
        autocomplete_next();
    } else {
        autocomplete_show();
    }
}

void autocomplete_show(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win)
        return;

    /* Extract prefix */
    extract_prefix(buf, ac_state.prefix, sizeof(ac_state.prefix), &ac_state.prefix_start_x);
    ac_state.prefix_len = strlen(ac_state.prefix);

    /* Need at least one character to complete */
    if (ac_state.prefix_len == 0) {
        ed_set_status_message("No word to complete");
        return;
    }

    /* Create completion buffer */
    int comp_idx = -1;
    if (buf_new(NULL, &comp_idx) != ED_OK) {
        ed_set_status_message("Failed to create completion buffer");
        return;
    }

    Buffer *comp_buf = &E.buffers.data[comp_idx];
    comp_buf->readonly = 1;

    /* Find completions */
    find_completions(buf, ac_state.prefix, comp_buf);

    if (comp_buf->num_rows == 0) {
        buf_close(comp_idx);
        ed_set_status_message("No completions found");
        return;
    }

    /* Calculate modal position (below cursor) */
    /* Convert cursor position to screen position */
    int cursor_screen_x = buf->cursor.x - win->col_offset + win->left;
    int cursor_screen_y = buf->cursor.y - win->row_offset + win->top + 1; /* +1 for below */

    /* Add gutter width if enabled */
    if (win->gutter_mode) {
        cursor_screen_x += win->gutter_fixed_width;
    }

    /* Determine modal dimensions */
    int max_width = 40;
    int actual_width = ac_state.prefix_len + 10; /* Prefix + some extra */
    for (int i = 0; i < comp_buf->num_rows && i < 10; i++) {
        int row_len = comp_buf->rows[i].chars.len;
        if (row_len + 2 > actual_width)
            actual_width = row_len + 2;
    }
    if (actual_width > max_width)
        actual_width = max_width;

    int height = comp_buf->num_rows < 10 ? comp_buf->num_rows : 10;

    /* Adjust position if modal would go off screen */
    int modal_x = cursor_screen_x;
    int modal_y = cursor_screen_y;

    if (modal_x + actual_width > E.screen_cols)
        modal_x = E.screen_cols - actual_width;
    if (modal_x < 1)
        modal_x = 1;

    if (modal_y + height > E.screen_rows)
        modal_y = cursor_screen_y - height - 1; /* Show above cursor instead */
    if (modal_y < 1)
        modal_y = 1;

    /* Create modal */
    ac_state.modal = winmodal_create(modal_x, modal_y, actual_width, height);
    if (!ac_state.modal) {
        buf_close(comp_idx);
        ed_set_status_message("Failed to create modal");
        return;
    }

    /* Attach completion buffer to modal */
    ac_state.modal->buffer_index = comp_idx;
    ac_state.completion_buffer = comp_idx;
    ac_state.selected_index = 0;

    /* Highlight first item */
    comp_buf->cursor.y = 0;
    comp_buf->cursor.x = 0;

    /* Show modal */
    winmodal_show(ac_state.modal);
    ed_mark_dirty();
}

void autocomplete_hide(void) {
    if (!ac_state.modal)
        return;

    /* Delete completion buffer */
    if (ac_state.completion_buffer >= 0) {
        buf_close(ac_state.completion_buffer);
        ac_state.completion_buffer = -1;
    }

    /* Destroy modal */
    winmodal_destroy(ac_state.modal);
    ac_state.modal = NULL;
    ac_state.selected_index = 0;
    ac_state.prefix[0] = '\0';
    ac_state.prefix_len = 0;

    ed_mark_dirty();
}

int autocomplete_is_active(void) {
    return ac_state.modal != NULL && ac_state.modal->visible;
}

void autocomplete_next(void) {
    if (!autocomplete_is_active())
        return;

    Buffer *comp_buf = &E.buffers.data[ac_state.completion_buffer];

    ac_state.selected_index++;
    if (ac_state.selected_index >= comp_buf->num_rows)
        ac_state.selected_index = 0;

    comp_buf->cursor.y = ac_state.selected_index;
    comp_buf->cursor.x = 0;

    /* Adjust scroll if needed */
    if (comp_buf->cursor.y < ac_state.modal->row_offset) {
        ac_state.modal->row_offset = comp_buf->cursor.y;
    } else if (comp_buf->cursor.y >= ac_state.modal->row_offset + ac_state.modal->height) {
        ac_state.modal->row_offset = comp_buf->cursor.y - ac_state.modal->height + 1;
    }

    ed_mark_dirty();
}

void autocomplete_prev(void) {
    if (!autocomplete_is_active())
        return;

    Buffer *comp_buf = &E.buffers.data[ac_state.completion_buffer];

    ac_state.selected_index--;
    if (ac_state.selected_index < 0)
        ac_state.selected_index = comp_buf->num_rows - 1;

    comp_buf->cursor.y = ac_state.selected_index;
    comp_buf->cursor.x = 0;

    /* Adjust scroll if needed */
    if (comp_buf->cursor.y < ac_state.modal->row_offset) {
        ac_state.modal->row_offset = comp_buf->cursor.y;
    } else if (comp_buf->cursor.y >= ac_state.modal->row_offset + ac_state.modal->height) {
        ac_state.modal->row_offset = comp_buf->cursor.y - ac_state.modal->height + 1;
    }

    ed_mark_dirty();
}

void autocomplete_accept(void) {
    if (!autocomplete_is_active())
        return;

    Buffer *buf = buf_cur();
    Buffer *comp_buf = &E.buffers.data[ac_state.completion_buffer];

    if (!buf || ac_state.selected_index >= comp_buf->num_rows)
        return;

    /* Get selected completion */
    Row *selected_row = &comp_buf->rows[ac_state.selected_index];
    char *completion = selected_row->chars.data;
    int completion_len = selected_row->chars.len;

    /* Delete the prefix */
    buf->cursor.x = ac_state.prefix_start_x;
    for (int i = 0; i < ac_state.prefix_len; i++) {
        buf_del_char_in(buf);
    }

    /* Insert the completion */
    for (int i = 0; i < completion_len; i++) {
        buf_insert_char_in(buf, completion[i]);
    }

    /* Hide autocomplete */
    autocomplete_hide();
}

void autocomplete_cancel(void) {
    autocomplete_hide();
}
