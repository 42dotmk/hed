#include "hooks.h"
#include "editor.h"
#include "stb_ds.h"
#include <stdlib.h>
#include <string.h>

/* Hook entry - stores callback with its filters */
typedef struct {
    void *callback;
    int mode;       /* EditorMode - filter by mode */
    char *filetype; /* Filter by filetype, "*" for all */
} HookEntry;

/* Per-type stb_ds dynamic array of HookEntry. */
static HookEntry *hooks[HOOK_TYPE_COUNT];

/* Helper: check if hook should fire based on filters */
static int hook_should_fire(const HookEntry *entry, int current_mode,
                            const char *current_filetype) {
    if (entry->mode != current_mode) {
        return 0;
    }
    if (strcmp(entry->filetype, "*") != 0) {
        if (!current_filetype ||
            strcmp(entry->filetype, current_filetype) != 0) {
            return 0;
        }
    }
    return 1;
}

void hook_init(void) {
    for (int i = 0; i < HOOK_TYPE_COUNT; i++) {
        arrfree(hooks[i]);
        hooks[i] = NULL;
    }
}

/* Shared registration: append a new HookEntry. Returns 0 on OOM/invalid. */
static int hook_push(HookType type, int mode, const char *filetype,
                     void *callback) {
    if (type >= HOOK_TYPE_COUNT)
        return 0;
    char *ft_copy = strdup(filetype);
    if (!ft_copy)
        return 0;
    HookEntry e = {.callback = callback, .mode = mode, .filetype = ft_copy};
    arrput(hooks[type], e);
    return 1;
}

void hook_register_char(HookType type, int mode, const char *filetype,
                        HookCharCallback callback) {
    hook_push(type, mode, filetype, (void *)callback);
}

void hook_register_line(HookType type, int mode, const char *filetype,
                        HookLineCallback callback) {
    hook_push(type, mode, filetype, (void *)callback);
}

void hook_register_buffer(HookType type, int mode, const char *filetype,
                          HookBufferCallback callback) {
    hook_push(type, mode, filetype, (void *)callback);
}

void hook_register_mode(HookType type, HookModeCallback callback) {
    hook_push(type, -1, "*", (void *)callback);
}

void hook_register_cursor(HookType type, int mode, const char *filetype,
                          HookCursorCallback callback) {
    hook_push(type, mode, filetype, (void *)callback);
}

void hook_register_key(HookType type, HookKeyCallback callback) {
    hook_push(type, -1, "*", (void *)callback);
}

void hook_register_simple(HookType type, HookSimpleCallback callback) {
    hook_push(type, -1, "*", (void *)callback);
}

void hook_register_keybind_feed(HookType type, HookKeybindFeedCallback cb) {
    hook_push(type, -1, "*", (void *)cb);
}

void hook_register_keybind_invoke(HookType type, HookKeybindInvokeCallback cb) {
    hook_push(type, -1, "*", (void *)cb);
}

int hook_unregister(HookType type, void *callback) {
    if (type >= HOOK_TYPE_COUNT) return 0;
    int removed = 0;
    for (ptrdiff_t i = arrlen(hooks[type]) - 1; i >= 0; i--) {
        if (hooks[type][i].callback == callback) {
            free(hooks[type][i].filetype);
            arrdel(hooks[type], i);
            removed++;
        }
    }
    return removed;
}

void hook_fire_char(HookType type, const HookCharEvent *event) {
    if (type >= HOOK_TYPE_COUNT)
        return;
    const char *filetype =
        (event->buf && event->buf->filetype) ? event->buf->filetype : "txt";
    for (ptrdiff_t i = 0; i < arrlen(hooks[type]); i++) {
        if (hook_should_fire(&hooks[type][i], E.mode, filetype)) {
            ((HookCharCallback)hooks[type][i].callback)(event);
        }
    }
}

void hook_fire_line(HookType type, const HookLineEvent *event) {
    if (type >= HOOK_TYPE_COUNT)
        return;
    const char *filetype =
        (event->buf && event->buf->filetype) ? event->buf->filetype : "txt";
    for (ptrdiff_t i = 0; i < arrlen(hooks[type]); i++) {
        if (hook_should_fire(&hooks[type][i], E.mode, filetype)) {
            ((HookLineCallback)hooks[type][i].callback)(event);
        }
    }
}

void hook_fire_buffer(HookType type, HookBufferEvent *event) {
    if (type >= HOOK_TYPE_COUNT)
        return;
    const char *filetype =
        (event->buf && event->buf->filetype) ? event->buf->filetype : "txt";
    for (ptrdiff_t i = 0; i < arrlen(hooks[type]); i++) {
        if (hook_should_fire(&hooks[type][i], E.mode, filetype)) {
            ((HookBufferCallback)hooks[type][i].callback)(event);
            if (event->consumed)
                return;
        }
    }
}

void hook_fire_mode(HookType type, const HookModeEvent *event) {
    if (type >= HOOK_TYPE_COUNT)
        return;
    for (ptrdiff_t i = 0; i < arrlen(hooks[type]); i++) {
        ((HookModeCallback)hooks[type][i].callback)(event);
    }
}

void hook_fire_key(HookType type, HookKeyEvent *event) {
    if (type >= HOOK_TYPE_COUNT)
        return;
    for (ptrdiff_t i = 0; i < arrlen(hooks[type]); i++) {
        ((HookKeyCallback)hooks[type][i].callback)(event);
        if (event->consumed)
            return;
    }
}

void hook_fire_simple(HookType type) {
    if (type >= HOOK_TYPE_COUNT)
        return;
    for (ptrdiff_t i = 0; i < arrlen(hooks[type]); i++) {
        ((HookSimpleCallback)hooks[type][i].callback)();
    }
}

void hook_fire_keybind_feed(HookType type, const HookKeybindFeedEvent *event) {
    if (type >= HOOK_TYPE_COUNT)
        return;
    for (ptrdiff_t i = 0; i < arrlen(hooks[type]); i++) {
        ((HookKeybindFeedCallback)hooks[type][i].callback)(event);
    }
}

void hook_fire_keybind_invoke(HookType type,
                              const HookKeybindInvokeEvent *event) {
    if (type >= HOOK_TYPE_COUNT)
        return;
    for (ptrdiff_t i = 0; i < arrlen(hooks[type]); i++) {
        ((HookKeybindInvokeCallback)hooks[type][i].callback)(event);
    }
}

void hook_fire_cursor(HookType type, const HookCursorEvent *event) {
    if (type >= HOOK_TYPE_COUNT)
        return;
    const char *filetype =
        (event->buf && event->buf->filetype) ? event->buf->filetype : "txt";
    for (ptrdiff_t i = 0; i < arrlen(hooks[type]); i++) {
        if (hook_should_fire(&hooks[type][i], E.mode, filetype)) {
            ((HookCursorCallback)hooks[type][i].callback)(event);
        }
    }
}
