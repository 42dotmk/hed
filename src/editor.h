#ifndef EDITOR_H
#define EDITOR_H

#include "buffer.h"
#include "history.h"
#include "recent_files.h"
#include "jump_list.h"
#include "quickfix.h"
#include "window.h"
#include "sizedstr.h"
#include "vector.h"

#define HED_VERSION "0.2.0"
#define TAB_STOP 4
#define CTRL_KEY(k) ((k) & 0x1f)
#define CMD_HISTORY_MAX 1000

/* Initial capacities for growable vectors */
#define BUFFERS_INITIAL_CAP 8
#define WINDOWS_INITIAL_CAP 4
#define COMMANDS_INITIAL_CAP 64

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
    MODE_COMMAND
} EditorMode;

/* Typed vectors for editor arrays */
VEC_DEFINE(BufferVec, Buffer);
VEC_DEFINE(WindowVec, Window);

/* Editor configuration - global state */
typedef struct {
    EditorMode mode;
    BufferVec buffers;      /* Growable buffer array */
    int current_buffer;
    /* Windows */
    WindowVec windows;      /* Growable window array */
    int current_window;
    int window_layout; /* 0=single, 1=vertical split, 2=horizontal split */
    int screen_rows;
    int screen_cols;
    int render_x;

    int show_line_numbers; /* 0=off, 1=on */
    int relative_line_numbers; /* 0=absolute only, 1=relative (abs on current) */

    char status_msg[256];
    char command_buf[128];
    int command_len;

    SizedStr clipboard;
    SizedStr search_query;
    Qf qf;
    struct WLayoutNode *wlayout_root;
    CmdHistory history;
    RecentFiles recent_files;
    JumpList jump_list;
    int messages_buffer_index;
    int stay_in_command;
    int term_open;              /* 0/1: terminal pane visible */
    int term_height;            /* desired terminal pane height */
    int term_window_index;      /* window index for terminal pane, or -1 */
    struct {
        char **items;
        int count;
        int index;
        char base[256];
        char prefix[128];
        int active; /* 1 when a completion set is active */
    } cmd_complete;
} Ed;

/* Global editor state */
extern Ed E;

/* Convenience macros for accessing vectors */
#define buf_at(i) (&E.buffers.data[i])
#define win_at(i) (&E.windows.data[i])
#define num_buffers() (E.buffers.len)
#define num_windows() (E.windows.len)

/* Editor operations */

/* Input handling */
int ed_read_key(void);
void ed_process_keypress(void);
void ed_move_cursor(int key);
void ed_process_command(void);

void ed_set_mode(EditorMode new_mode);
void ed_set_status_message(const char *fmt, ...);

void ed_init(void);
void ed_change_cursor_shape(void);


#endif
