#ifndef HOOKS_H
#define HOOKS_H

#include <stddef.h>
#include <stdbool.h>

typedef struct Buffer Buffer;
struct KeybindMatchView;

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

    /* Fires after every keybind_feed() call. Payload is the feed
     * snapshot (active sequence, count, exact/partial state, matches
     * array). Used by which-key style plugins to render an overlay
     * when the sequence is partial. */
    HOOK_KEYBIND_FEED,

    /* Fires from keybind_invoke(), just before the callback runs.
     * Payload is the binding being invoked plus the repeat count. */
    HOOK_KEYBIND_INVOKE,

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

typedef struct {
    const char *active_sequence;
    int  active_len;
    int  count;
    bool has_count;
    bool exact;
    bool partial;
    bool consumed_count_only;
    /* Pointer into the feed result's stb_ds matches array. Lives only
     * for the duration of the hook firing — do not retain. */
    const struct KeybindMatchView *matches;
    int  match_count;
} HookKeybindFeedEvent;

typedef struct {
    const struct KeybindMatchView *match;
    int repeat;
} HookKeybindInvokeEvent;

/* Callback function pointer types */
typedef void (*HookCharCallback)(const HookCharEvent *event);
typedef void (*HookLineCallback)(const HookLineEvent *event);
typedef void (*HookBufferCallback)(HookBufferEvent *event); /* non-const: handler may set consumed */
typedef void (*HookModeCallback)(const HookModeEvent *event);
typedef void (*HookCursorCallback)(const HookCursorEvent *event);
typedef void (*HookKeyCallback)(HookKeyEvent *event); /* non-const: handler may set consumed */
typedef void (*HookSimpleCallback)(void);              /* payload-free hooks (e.g., HOOK_STARTUP_DONE) */
typedef void (*HookKeybindFeedCallback)(const HookKeybindFeedEvent *event);
typedef void (*HookKeybindInvokeCallback)(const HookKeybindInvokeEvent *event);

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
/* Keybind dispatch hooks always fire regardless of mode or filetype. */
void hook_register_keybind_feed(HookType type, HookKeybindFeedCallback cb);
void hook_register_keybind_invoke(HookType type, HookKeybindInvokeCallback cb);

/* Remove every registration of `callback` from the given hook type.
 * Match is by callback pointer; mode/filetype filters are ignored.
 * Returns the number of entries removed. */
int hook_unregister(HookType type, void *callback);

/* Hook firing functions */
void hook_fire_char(HookType type, const HookCharEvent *event);
void hook_fire_line(HookType type, const HookLineEvent *event);
void hook_fire_buffer(HookType type, HookBufferEvent *event);
void hook_fire_mode(HookType type, const HookModeEvent *event);
void hook_fire_cursor(HookType type, const HookCursorEvent *event);
void hook_fire_key(HookType type, HookKeyEvent *event);
void hook_fire_simple(HookType type);
void hook_fire_keybind_feed(HookType type, const HookKeybindFeedEvent *event);
void hook_fire_keybind_invoke(HookType type, const HookKeybindInvokeEvent *event);


#endif
