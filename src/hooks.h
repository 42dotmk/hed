#ifndef HOOKS_H
#define HOOKS_H

#include <stddef.h>

typedef struct Buffer Buffer;

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

    /* Intercept hooks: fire before the default action. A handler may set
     * event->consumed = 1 to claim ownership and prevent core fallback. */
    HOOK_BUFFER_OPEN_PRE,
    HOOK_BUFFER_SAVE_PRE,
    // HOOK_BUFFER_VISIBLE,
    // HOOK_BUFFER_HIDDEN,
    // HOOK_BUFFER_MODIFIED,
    // HOOK_BUFFER_FOCUSED,

    HOOK_MODE_CHANGE,

    HOOK_CURSOR_MOVE,

    /* Fires for every key before normal dispatch.
     * Set event->consumed = 1 to prevent further processing. */
    HOOK_KEYPRESS,

    /* Fires once, after ed_init + cli file opens + startup -c command,
     * just before the main loop begins. No event payload. Use this to
     * run plugin-side post-init logic that needs the editor fully set
     * up (e.g., session restore). */
    HOOK_STARTUP_DONE,

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
    /* For *_PRE intercept hooks: handler sets to 1 to claim ownership.
     * Ignored for non-intercept buffer hooks. */
    int consumed;
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

typedef struct {
    int key;      /* key code as returned by ed_read_key() */
    int consumed; /* set to 1 by a handler to cancel further processing */
} HookKeyEvent;

/* Callback function pointer types */
typedef void (*HookCharCallback)(const HookCharEvent *event);
typedef void (*HookLineCallback)(const HookLineEvent *event);
typedef void (*HookBufferCallback)(HookBufferEvent *event); /* non-const: handler may set consumed */
typedef void (*HookModeCallback)(const HookModeEvent *event);
typedef void (*HookCursorCallback)(const HookCursorEvent *event);
typedef void (*HookKeyCallback)(HookKeyEvent *event); /* non-const: handler may set consumed */
typedef void (*HookSimpleCallback)(void);              /* payload-free hooks (e.g., HOOK_STARTUP_DONE) */

/* Hook API */
void hook_init(void);

/* Registration functions - mode and filetype are always required
 * Use "*" for filetype to match all file types
 * Mode callbacks don't take mode/filetype filters as they fire on mode changes
 */
void hook_register_char(HookType type, int mode, const char *filetype,
                        HookCharCallback callback);
void hook_register_line(HookType type, int mode, const char *filetype,
                        HookLineCallback callback);
void hook_register_buffer(HookType type, int mode, const char *filetype,
                          HookBufferCallback callback);
void hook_register_mode(HookType type, HookModeCallback callback);
void hook_register_cursor(HookType type, int mode, const char *filetype,
                          HookCursorCallback callback);
/* Keypress hooks always fire regardless of mode or filetype. */
void hook_register_key(HookType type, HookKeyCallback callback);
/* Simple, payload-free hooks (always fire). */
void hook_register_simple(HookType type, HookSimpleCallback callback);

/* Hook firing functions */
void hook_fire_char(HookType type, const HookCharEvent *event);
void hook_fire_line(HookType type, const HookLineEvent *event);
void hook_fire_buffer(HookType type, HookBufferEvent *event);
void hook_fire_mode(HookType type, const HookModeEvent *event);
void hook_fire_cursor(HookType type, const HookCursorEvent *event);
void hook_fire_key(HookType type, HookKeyEvent *event);
void hook_fire_simple(HookType type);


#endif
