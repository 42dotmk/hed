#include "hed.h"

#ifndef JUMP_LIST_MAX
#define JUMP_LIST_MAX 100
#endif

void jump_list_init(JumpList *jl) {
    if (!jl) return;
	jl->entries = (JumpEntryVec){0};
    jl->current = -1;
}

void jump_list_free(JumpList *jl) {
    if (!jl) return;
    free(jl->entries.data);
	jl->entries = (JumpEntryVec){0};
    jl->current = -1;
}

void jump_list_add(JumpList *jl, char *filepath, int cursor_x, int cursor_y) {
    if (!jl)
        return;

    /* Don't add if it's the same as the last entry */
    if (jl->entries.len > 0) {
        JumpEntry *last = &jl->entries.data[jl->entries.len - 1];
        if (strcmp(last->filepath, filepath) == 0 &&
            last->cursor_x == cursor_x && last->cursor_y == cursor_y) {
            return;
        }
    }

    /* If we're navigating (current != -1), truncate everything after current
     * position */
    if (jl->current != -1 && jl->current < (int)jl->entries.len - 1) {
        jl->entries.len = (size_t)(jl->current + 1);
    }

    /* If at max capacity, remove oldest entry */
    if (jl->entries.len >= JUMP_LIST_MAX) {
        JumpEntry oldest = vec_pop_start_typed(&jl->entries, JumpEntry);
        free(oldest.filepath);
    }

    /* Add new entry using vec_push_typed */
    JumpEntry new_entry = {
        .filepath = strdup(filepath),
        .cursor_x = cursor_x,
        .cursor_y = cursor_y
    };
    vec_push_typed(&jl->entries, JumpEntry, new_entry);

    /* Reset navigation state */
    jl->current = -1;
}

int jump_list_backward(JumpList *jl, char **filepath, int *out_x, int *out_y) {
    if (!jl || jl->entries.len == 0)
        return 0;

    /* Initialize current position if not navigating */
    if (jl->current == -1) {
        jl->current = (int)jl->entries.len - 1;
    }

    /* Try to move backward */
    if (jl->current > 0) {
        jl->current--;
        JumpEntry *entry = &jl->entries.data[jl->current];
        char *copy = strdup(entry->filepath);
        *filepath = copy;
        *out_x = entry->cursor_x;
        *out_y = entry->cursor_y;
        return 1;
    }

    /* Already at the beginning */
    return 0;
}

int jump_list_forward(JumpList *jl, char **filepath, int *out_x, int *out_y) {
    if (!jl || jl->entries.len == 0)
        return 0;

    /* Can only move forward if we're navigating */
    if (jl->current == -1)
        return 0;

    /* Try to move forward */
    if (jl->current < (int)jl->entries.len - 1) {
        jl->current++;
        JumpEntry *entry = &jl->entries.data[jl->current];
        char *copy = strdup(entry->filepath);
        *filepath = copy;
        *out_x = entry->cursor_x;
        *out_y = entry->cursor_y;
        return 1;
    }

    /* At the end, reset navigation */
    jl->current = -1;
    return 0;
}

void jump_list_reset_navigation(JumpList *jl) {
    if (!jl)
        return;
    jl->current = -1;
}
