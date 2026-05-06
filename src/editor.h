#ifndef EDITOR_H
#define EDITOR_H

#include <limits.h>
#include "buf/buffer.h"
#include "utils/history.h"
#include "utils/jump_list.h"
#include "utils/quickfix.h"
#include "utils/recent_files.h"
#include "lib/sizedstr.h"
#include "stb_ds.h"
#include "ui/window.h"

#define HED_VERSION "0.2.0"
#define TAB_STOP 4
#define CTRL_KEY(k) ((k) & 0x1f)
#define CMD_HISTORY_MAX 1000

/* Special key codes */
#define KEY_DELETE 127
#define KEY_PAGE_UP 1000
#define KEY_PAGE_DOWN 1001
#define KEY_ARROW_UP 1002
#define KEY_ARROW_DOWN 1003
#define KEY_ARROW_RIGHT 1004
#define KEY_ARROW_LEFT 1005
#define KEY_HOME 1006
#define KEY_END 1007
#define KEY_F1  1010
#define KEY_F2  1011
#define KEY_F3  1012
#define KEY_F4  1013
#define KEY_F5  1014
#define KEY_F6  1015
#define KEY_F7  1016
#define KEY_F8  1017
#define KEY_F9  1018
#define KEY_F10 1019
#define KEY_F11 1020
#define KEY_F12 1021

/* Modifier flags OR'd with the base key code. Out of range of the
 * special keys above and well above any byte value, so they never
 * overlap with raw key codes.
 *   KEY_META  — Alt / Meta modifier (e.g. <M-x> = KEY_META | 'x')
 *   KEY_CTRL  — Ctrl modifier on a SPECIAL key (e.g. <C-Left>).
 *               Plain Ctrl+letter is still encoded as bytes 1..26;
 *               KEY_CTRL is only used for non-byte keys (arrows etc.).
 */
#define KEY_META  0x10000
#define KEY_CTRL  0x20000
#define KEY_SHIFT 0x40000
#define KEY_IS_META(k)  (((k) & KEY_META)  != 0)
#define KEY_IS_CTRL(k)  (((k) & KEY_CTRL)  != 0)
#define KEY_IS_SHIFT(k) (((k) & KEY_SHIFT) != 0)
#define KEY_NO_META(k)  ((k) & 0xFFFF)
#define KEY_BASE(k)     ((k) & 0xFFFF)

#define CURSOR_STYLE_NONE "\x1b[0 q"
#define CURSOR_STYLE_BLOCK "\x1b[1 q"
#define CURSOR_STYLE_BLINKING_BLOCK "\x1b[2 q"
#define CURSOR_STYLE_UNDERLINE "\x1b[3 q"
#define CURSOR_STYLE_BLINKING_UNDERLINE "\x1b[4 q"
#define CURSOR_STYLE_BEAM "\x1b[5 q"

/* Editor modes */
typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND,
    MODE_VISUAL,
    MODE_VISUAL_LINE,
    MODE_VISUAL_BLOCK
} EditorMode;

/* Editor configuration - global state */
typedef struct {
    EditorMode mode;
    Buffer *buffers; /* stb_ds dynamic array */
    Window *windows; /* stb_ds dynamic array */
    int current_buffer;
    int current_window;

    int window_layout; /* 0=single, 1=vertical split, 2=horizontal split */
    int screen_rows;
    int screen_cols;
    int render_x;

    int show_line_numbers;     /* 0=off, 1=on */
    int relative_line_numbers; /* 0=absolute only, 1=relative (abs on current)
                                */

    char status_msg[4096];

    SizedStr search_query;
    int search_is_regex; /* 1=regex search, 0=literal */
    Qf qf;
    Window *modal_window; /* Current modal window (NULL if none) */
    struct WLayoutNode *wlayout_root;
    CmdHistory history;
    RecentFiles recent_files;
    JumpList jump_list;
    int default_wrap;   /* 0=unwrap windows by default, 1=wrap */
    int expand_tab;     /* 0=insert '\t', 1=insert spaces */
    int tab_size;       /* visual tab size (defaults to TAB_STOP) */
    char cwd[PATH_MAX]; /* editor working directory (logical cwd) */

    /* Macro replay queue - simulates keyboard input */
    struct {
        char *buffer;   /* String buffer with readable sequences like "dd<Esc>jj" */
        int capacity;   /* Allocated capacity */
        int length;     /* String length */
        int position;   /* Current read position in buffer */
    } macro_queue;

    /* Macro recording state */
    struct {
        int recording;        /* 1 if currently recording */
        char register_name;   /* Register being recorded to (a-z) */
        char last_played;     /* Last register played with @ (for @@) */
    } macro_recording;
} Ed;

/* Global editor state */
extern Ed E;

/* Input handling */
int ed_read_key(void);
/* Pure key parser lives in input.h (`ed_parse_key_from_fd`). */
void ed_process_keypress(void);
/* Run the per-mode dispatch for one key (the part of ed_process_keypress
 * after HOOK_KEYPRESS). Plugins use this to replay a key at multiple
 * cursors without firing HOOK_KEYPRESS again. */
void ed_dispatch_key(int c);
void ed_move_cursor(int key);

void ed_set_mode(EditorMode new_mode);

/* Modeless toggle. When on, NORMAL mode is unreachable — any attempt to
 * enter it is silently redirected to INSERT. Used by emacs/vscode-style
 * keymaps that are conceptually modeless. Visual and Command modes are
 * preserved. */
void ed_set_modeless(int on);
int  ed_is_modeless(void);
void ed_set_status_message(const char *fmt, ...);

void ed_init(int create_default_buffer);
void ed_change_cursor_shape(void);

#endif
