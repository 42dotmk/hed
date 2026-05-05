#include "utils/jump_list.h"
#include <stdlib.h>
#include <string.h>

#ifndef JUMP_LIST_MAX
#define JUMP_LIST_MAX 100
#endif

void jump_list_init(JumpList *jl) {
    if (!jl) return;
    jl->entries = NULL;
    jl->current = -1;
}

void jump_list_free(JumpList *jl) {
    if (!jl) return;
    for (ptrdiff_t i = 0; i < arrlen(jl->entries); i++)
        free(jl->entries[i].filepath);
    arrfree(jl->entries);
    jl->entries = NULL;
    jl->current = -1;
}

void jump_list_add(JumpList *jl, char *filepath, int cursor_x, int cursor_y) {
    if (!jl)
        return;

    if (arrlen(jl->entries) > 0) {
        JumpEntry *last = &jl->entries[arrlen(jl->entries) - 1];
        if (strcmp(last->filepath, filepath) == 0 &&
            last->cursor_x == cursor_x && last->cursor_y == cursor_y) {
            return;
        }
    }

    /* If navigating, truncate everything after current position. */
    if (jl->current != -1 && jl->current < (int)arrlen(jl->entries) - 1) {
        for (ptrdiff_t i = jl->current + 1; i < arrlen(jl->entries); i++)
            free(jl->entries[i].filepath);
        arrsetlen(jl->entries, jl->current + 1);
    }

    /* Drop oldest at capacity. */
    if (arrlen(jl->entries) >= JUMP_LIST_MAX) {
        free(jl->entries[0].filepath);
        arrdel(jl->entries, 0);
    }

    JumpEntry new_entry = {
        .filepath = strdup(filepath),
        .cursor_x = cursor_x,
        .cursor_y = cursor_y
    };
    arrput(jl->entries, new_entry);

    jl->current = -1;
}

int jump_list_backward(JumpList *jl, char **filepath, int *out_x, int *out_y) {
    if (!jl || arrlen(jl->entries) == 0)
        return 0;

    if (jl->current == -1) {
        jl->current = (int)arrlen(jl->entries) - 1;
    }

    if (jl->current > 0) {
        jl->current--;
        JumpEntry *entry = &jl->entries[jl->current];
        *filepath = strdup(entry->filepath);
        *out_x = entry->cursor_x;
        *out_y = entry->cursor_y;
        return 1;
    }

    return 0;
}

int jump_list_forward(JumpList *jl, char **filepath, int *out_x, int *out_y) {
    if (!jl || arrlen(jl->entries) == 0)
        return 0;

    if (jl->current == -1)
        return 0;

    if (jl->current < (int)arrlen(jl->entries) - 1) {
        jl->current++;
        JumpEntry *entry = &jl->entries[jl->current];
        *filepath = strdup(entry->filepath);
        *out_x = entry->cursor_x;
        *out_y = entry->cursor_y;
        return 1;
    }

    jl->current = -1;
    return 0;
}

void jump_list_reset_navigation(JumpList *jl) {
    if (!jl)
        return;
    jl->current = -1;
}
