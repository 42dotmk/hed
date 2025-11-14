#include "hed.h"

#define MAX_HOOKS_PER_TYPE 16

/* Hook entry - stores callback with its filters */
typedef struct {
    void *callback;
    int mode;         /* EditorMode - filter by mode */
    char *filetype;   /* Filter by filetype, "*" for all */
} HookEntry;

/* Hook callback storage */
typedef struct {
    HookEntry entries[MAX_HOOKS_PER_TYPE];
    int count;
} HookList;

static HookList hooks[HOOK_TYPE_COUNT];

/* Helper: check if hook should fire based on filters */
static int hook_should_fire(const HookEntry *entry, int current_mode, const char *current_filetype) {
    /* Check mode filter */
    if (entry->mode != current_mode) {
        return 0;
    }

    /* Check filetype filter */
    if (strcmp(entry->filetype, "*") != 0) {
        if (!current_filetype || strcmp(entry->filetype, current_filetype) != 0) {
            return 0;
        }
    }

    return 1;
}

/* Initialize hook system */
void hook_init(void) {
    for (int i = 0; i < HOOK_TYPE_COUNT; i++) {
        hooks[i].count = 0;
    }

    /* Call user hooks initialization */
    user_hooks_init();
}

/* Registration functions */
void hook_register_char(HookType type, int mode, const char *filetype, HookCharCallback callback) {
    if (type >= HOOK_TYPE_COUNT || hooks[type].count >= MAX_HOOKS_PER_TYPE) {
        return;
    }
    char *ft_copy = strdup(filetype);
    if (!ft_copy) return;  /* OOM: fail gracefully */

    int idx = hooks[type].count++;
    hooks[type].entries[idx].callback = (void *)callback;
    hooks[type].entries[idx].mode = mode;
    hooks[type].entries[idx].filetype = ft_copy;
}

void hook_register_line(HookType type, int mode, const char *filetype, HookLineCallback callback) {
    if (type >= HOOK_TYPE_COUNT || hooks[type].count >= MAX_HOOKS_PER_TYPE) {
        return;
    }
    char *ft_copy = strdup(filetype);
    if (!ft_copy) return;  /* OOM: fail gracefully */

    int idx = hooks[type].count++;
    hooks[type].entries[idx].callback = (void *)callback;
    hooks[type].entries[idx].mode = mode;
    hooks[type].entries[idx].filetype = ft_copy;
}

void hook_register_buffer(HookType type, int mode, const char *filetype, HookBufferCallback callback) {
    if (type >= HOOK_TYPE_COUNT || hooks[type].count >= MAX_HOOKS_PER_TYPE) {
        return;
    }
    char *ft_copy = strdup(filetype);
    if (!ft_copy) return;  /* OOM: fail gracefully */

    int idx = hooks[type].count++;
    hooks[type].entries[idx].callback = (void *)callback;
    hooks[type].entries[idx].mode = mode;
    hooks[type].entries[idx].filetype = ft_copy;
}

void hook_register_mode(HookType type, HookModeCallback callback) {
    if (type >= HOOK_TYPE_COUNT || hooks[type].count >= MAX_HOOKS_PER_TYPE) {
        return;
    }
    char *ft_copy = strdup("*");
    if (!ft_copy) return;  /* OOM: fail gracefully */

    int idx = hooks[type].count++;
    hooks[type].entries[idx].callback = (void *)callback;
    hooks[type].entries[idx].mode = -1;  /* Mode change hooks don't filter by mode */
    hooks[type].entries[idx].filetype = ft_copy;
}

void hook_register_cursor(HookType type, int mode, const char *filetype, HookCursorCallback callback) {
    if (type >= HOOK_TYPE_COUNT || hooks[type].count >= MAX_HOOKS_PER_TYPE) {
        return;
    }
    char *ft_copy = strdup(filetype);
    if (!ft_copy) return;  /* OOM: fail gracefully */

    int idx = hooks[type].count++;
    hooks[type].entries[idx].callback = (void *)callback;
    hooks[type].entries[idx].mode = mode;
    hooks[type].entries[idx].filetype = ft_copy;
}

/* Hook firing functions */
void hook_fire_char(HookType type, const HookCharEvent *event) {
    if (type >= HOOK_TYPE_COUNT) return;

    const char *filetype = (event->buf && event->buf->filetype) ? event->buf->filetype : "txt";

    for (int i = 0; i < hooks[type].count; i++) {
        if (hook_should_fire(&hooks[type].entries[i], E.mode, filetype)) {
            HookCharCallback cb = (HookCharCallback)hooks[type].entries[i].callback;
            cb(event);
        }
    }
}

void hook_fire_line(HookType type, const HookLineEvent *event) {
    if (type >= HOOK_TYPE_COUNT) return;

    const char *filetype = (event->buf && event->buf->filetype) ? event->buf->filetype : "txt";

    for (int i = 0; i < hooks[type].count; i++) {
        if (hook_should_fire(&hooks[type].entries[i], E.mode, filetype)) {
            HookLineCallback cb = (HookLineCallback)hooks[type].entries[i].callback;
            cb(event);
        }
    }
}

void hook_fire_buffer(HookType type, const HookBufferEvent *event) {
    if (type >= HOOK_TYPE_COUNT) return;

    const char *filetype = (event->buf && event->buf->filetype) ? event->buf->filetype : "txt";

    for (int i = 0; i < hooks[type].count; i++) {
        if (hook_should_fire(&hooks[type].entries[i], E.mode, filetype)) {
            HookBufferCallback cb = (HookBufferCallback)hooks[type].entries[i].callback;
            cb(event);
        }
    }
}

void hook_fire_mode(HookType type, const HookModeEvent *event) {
    if (type >= HOOK_TYPE_COUNT) return;

    /* Mode change hooks always fire (mode=-1 in registration) */
    for (int i = 0; i < hooks[type].count; i++) {
        HookModeCallback cb = (HookModeCallback)hooks[type].entries[i].callback;
        cb(event);
    }
}

void hook_fire_cursor(HookType type, const HookCursorEvent *event) {
    if (type >= HOOK_TYPE_COUNT) return;

    const char *filetype = (event->buf && event->buf->filetype) ? event->buf->filetype : "txt";

    for (int i = 0; i < hooks[type].count; i++) {
        if (hook_should_fire(&hooks[type].entries[i], E.mode, filetype)) {
            HookCursorCallback cb = (HookCursorCallback)hooks[type].entries[i].callback;
            cb(event);
        }
    }
}
