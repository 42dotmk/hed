#include "hed.h"

#ifndef JUMP_LIST_MAX
#define JUMP_LIST_MAX 100
#endif

void jump_list_init(JumpList *jl) {
    if (!jl)
        return;
    jl->entries = NULL;
    jl->len = 0;
    jl->cap = 0;
    jl->current = -1;
}

void jump_list_free(JumpList *jl) {
    if (!jl)
        return;
    free(jl->entries);
    jl->entries = NULL;
    jl->len = 0;
    jl->cap = 0;
    jl->current = -1;
}

void jump_list_add(JumpList *jl, char *filepath, int cursor_x, int cursor_y) {
    if (!jl)
        return;

    /* Don't add if it's the same as the last entry */
    if (jl->len > 0) {
        JumpEntry *last = &jl->entries[jl->len - 1];
        if (strcmp(last->filepath, filepath) == 0 &&
            last->cursor_x == cursor_x && last->cursor_y == cursor_y) {
            return;
        }
    }

    /* If we're navigating (current != -1), truncate everything after current
     * position */
    if (jl->current != -1 && jl->current < jl->len - 1) {
        jl->len = jl->current + 1;
    }

    /* Ensure capacity */
    if (jl->len + 1 > jl->cap) {
        int new_cap = jl->cap == 0 ? 32 : jl->cap * 2;
        if (new_cap > JUMP_LIST_MAX)
            new_cap = JUMP_LIST_MAX;
        JumpEntry *new_entries =
            realloc(jl->entries, new_cap * sizeof(JumpEntry));
        if (!new_entries)
            return;
        jl->entries = new_entries;
        jl->cap = new_cap;
    }

    /* If at max capacity, shift everything down (remove oldest) */
    if (jl->len >= JUMP_LIST_MAX) {
        memmove(&jl->entries[0], &jl->entries[1],
                (JUMP_LIST_MAX - 1) * sizeof(JumpEntry));
        jl->len = JUMP_LIST_MAX - 1;
    }

    /* Add new entry */
    jl->entries[jl->len].filepath = strdup(filepath);
    jl->entries[jl->len].cursor_x = cursor_x;
    jl->entries[jl->len].cursor_y = cursor_y;
    jl->len++;

    /* Reset navigation state */
    jl->current = -1;
}

int jump_list_backward(JumpList *jl, char **filepath, int *out_x, int *out_y) {
    if (!jl || jl->len == 0)
        return 0;

    /* Initialize current position if not navigating */
    if (jl->current == -1) {
        jl->current = jl->len - 1;
    }

    /* Try to move backward */
    if (jl->current > 0) {
        jl->current--;
        JumpEntry *entry = &jl->entries[jl->current];
        char * copy = strdup(entry->filepath);
        *filepath = copy;
        *out_x = entry->cursor_x;
        *out_y = entry->cursor_y;
        return 1;
    }

    /* Already at the beginning */
    return 0;
}

int jump_list_forward(JumpList *jl, char **filepath, int *out_x, int *out_y) {
    if (!jl || jl->len == 0)
        return 0;

    /* Can only move forward if we're navigating */
    if (jl->current == -1)
        return 0;

    /* Try to move forward */
    if (jl->current < jl->len - 1) {
        jl->current++;
        JumpEntry *entry = &jl->entries[jl->current];
        char * copy = strdup(entry->filepath);
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
