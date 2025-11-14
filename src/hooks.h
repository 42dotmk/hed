#ifndef HOOKS_H
#define HOOKS_H

/* Hook event types */
typedef enum {
    /* Text modification hooks */
    HOOK_CHAR_INSERT,
    HOOK_CHAR_DELETE,
    HOOK_LINE_INSERT,
    HOOK_LINE_DELETE,

    /* Buffer lifecycle hooks */
    HOOK_BUFFER_OPEN,
    HOOK_BUFFER_CLOSE,
    HOOK_BUFFER_SWITCH,
    HOOK_BUFFER_SAVE,
    // HOOK_BUFFER_VISIBLE,
    // HOOK_BUFFER_HIDDEN,
    // HOOK_BUFFER_MODIFIED,
    // HOOK_BUFFER_FOCUSED,

    HOOK_MODE_CHANGE,

    HOOK_CURSOR_MOVE,

    HOOK_TYPE_COUNT
} HookType;

/* Event data structures */
typedef struct {
    Buffer *buf;
    int row;
    int col;
    int c;
} HookCharEvent;

typedef struct {
    Buffer *buf;
    int row;
    const char *text;
    size_t len;
} HookLineEvent;

typedef struct {
    Buffer *buf;
    const char *filename;
} HookBufferEvent;

typedef struct {
    int old_mode;
    int new_mode;
} HookModeEvent;

typedef struct {
    Buffer *buf;
    int old_x;
    int old_y;
    int new_x;
    int new_y;
} HookCursorEvent;

/* Callback function pointer types */
typedef void (*HookCharCallback)(const HookCharEvent *event);
typedef void (*HookLineCallback)(const HookLineEvent *event);
typedef void (*HookBufferCallback)(const HookBufferEvent *event);
typedef void (*HookModeCallback)(const HookModeEvent *event);
typedef void (*HookCursorCallback)(const HookCursorEvent *event);

/* Hook API */
void hook_init(void);

/* Registration functions - mode and filetype are always required
 * Use "*" for filetype to match all file types
 * Mode callbacks don't take mode/filetype filters as they fire on mode changes
 */
void hook_register_char(HookType type, int mode, const char *filetype, HookCharCallback callback);
void hook_register_line(HookType type, int mode, const char *filetype, HookLineCallback callback);
void hook_register_buffer(HookType type, int mode, const char *filetype, HookBufferCallback callback);
void hook_register_mode(HookType type, HookModeCallback callback);
void hook_register_cursor(HookType type, int mode, const char *filetype, HookCursorCallback callback);

/* Hook firing functions */
void hook_fire_char(HookType type, const HookCharEvent *event);
void hook_fire_line(HookType type, const HookLineEvent *event);
void hook_fire_buffer(HookType type, const HookBufferEvent *event);
void hook_fire_mode(HookType type, const HookModeEvent *event);
void hook_fire_cursor(HookType type, const HookCursorEvent *event);

/* User hooks initialization (implemented in user_hooks.c) */
void user_hooks_init(void);

#endif
