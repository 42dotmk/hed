#ifndef EDITOR_H
#define EDITOR_H

#include <limits.h>
#include <linux/limits.h>

#include "buffer.h"
#include "history.h"
#include "jump_list.h"
#include "quickfix.h"
#include "recent_files.h"
#include "sizedstr.h"
#include "vector.h"
#include "window.h"

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

/* Typed vectors for editor arrays */
VEC_DEFINE(BufferVec, Buffer);
VEC_DEFINE(WindowVec, Window);

/* Editor configuration - global state */
typedef struct {
    EditorMode mode;
    BufferVec buffers; /* Growable buffer array */
    WindowVec windows; /* Growable window array */
    int current_buffer;
    int current_window;

    int window_layout; /* 0=single, 1=vertical split, 2=horizontal split */
    int screen_rows;
    int screen_cols;
    int render_x;

    int show_line_numbers;     /* 0=off, 1=on */
    int relative_line_numbers; /* 0=absolute only, 1=relative (abs on current)
                                */

    char status_msg[256];
    char command_buf[128];
    int command_len;

    SizedStr clipboard;
    SizedStr search_query;
    int search_is_regex; /* 1=regex search, 0=literal */
    int clipboard_is_block;
    Qf qf;
    Window *modal_window; /* Current modal window (NULL if none) */
    struct WLayoutNode *wlayout_root;
    CmdHistory history;
    RecentFiles recent_files;
    JumpList jump_list;
    int stay_in_command;
    int default_wrap;   /* 0=unwrap windows by default, 1=wrap */
    int expand_tab;     /* 0=insert '\t', 1=insert spaces */
    int tab_size;       /* visual tab size (defaults to TAB_STOP) */
    char cwd[PATH_MAX]; /* editor working directory (logical cwd) */
    struct {
        char **items;
        int count;
        int index;
        char base[256];
        char prefix[128];
        int active; /* 1 when a completion set is active */
    } cmd_complete;

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
    int search_prompt_active; /* 1 while interactive / search prompt is open */
} Ed;

/* Global editor state */
extern Ed E;

/* Input handling */
int ed_read_key(void);
void ed_process_keypress(void);
void ed_move_cursor(int key);
void ed_process_command(void);

void ed_set_mode(EditorMode new_mode);
void ed_set_status_message(const char *fmt, ...);

void ed_init(int create_default_buffer);
void ed_change_cursor_shape(void);

#endif
