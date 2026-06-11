#ifndef HED_INPUT_H
#define HED_INPUT_H

/*
 * Block on `fd` until one logical keypress is available, then return it as
 * an int with KEY_META/KEY_CTRL/KEY_SHIFT flags OR'd onto the base key
 * (KEY_* constants live in editor.h).
 *
 * Decodes one of:
 *   - plain byte / ASCII control char
 *   - bare ESC ('\x1b')
 *   - ESC + non-CSI byte         -> KEY_META | byte  (Alt/Meta + key)
 *   - SS3:  ESC O P/Q/R/S/H/F    -> F1-F4, Home, End  (xterm)
 *   - CSI:  ESC [ A/B/C/D/H/F    -> arrows, Home, End
 *   - CSI numeric: ESC [ <n> ~   -> Delete, PageUp/Down, F5-F12
 *   - CSI modified: ESC [1;<m><L> or ESC [<n>;<m>~ -- xterm modifier
 *     matrix (m=2..8) decoded into Shift/Meta/Ctrl flag combinations.
 * Truncated or unrecognized escapes degrade to bare ESC rather than
 * blocking for more bytes.
 *
 * Pure parser: no macro queue, no recording. The production caller
 * (`ed_read_key`) layers those concerns on top. The one side channel
 * is the mouse: SGR events (ESC [ < b ; x ; y M/m, DEC mode 1006)
 * don't fit in an int, so the parser decodes into a static MouseEvent
 * and returns KEY_MOUSE; read the payload with ed_last_mouse().
 */
int ed_parse_key_from_fd(int fd);

typedef enum {
    MOUSE_PRESS,
    MOUSE_DRAG,    /* motion with a button held (mode 1002) */
    MOUSE_RELEASE,
    MOUSE_WHEEL_UP,
    MOUSE_WHEEL_DOWN
} MouseEventType;

typedef struct MouseEvent {
    MouseEventType type;
    int button; /* 0=left 1=middle 2=right; -1 for wheel events */
    int x, y;   /* 1-based terminal column / row */
    int mods;   /* KEY_SHIFT / KEY_META / KEY_CTRL flags */
} MouseEvent;

/* Event behind the most recent KEY_MOUSE. Overwritten by the next
 * mouse sequence — consume it in the same dispatch, don't retain. */
const MouseEvent *ed_last_mouse(void);

#endif
