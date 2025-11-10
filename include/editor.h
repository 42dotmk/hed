#ifndef EDITOR_H
#define EDITOR_H

#include "buffer.h"
#include "history.h"
#include "quickfix.h"
#include "window.h"
#include "sizedstr.h"

#define HED_VERSION "0.2.0"
#define TAB_STOP 4
#define CTRL_KEY(k) ((k) & 0x1f)
#define MAX_BUFFERS 128
#define CMD_HISTORY_MAX 1000

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
    MODE_VISUAL,
    MODE_COMMAND
} EditorMode;

/* Editor configuration - global state */
typedef struct {
    Buffer buffers[MAX_BUFFERS];
    int num_buffers;
    int current_buffer;
    /* Windows */
    Window windows[8];
    int num_windows;
    int current_window;
    int window_layout; /* 0=single, 1=vertical split, 2=horizontal split */
    int screen_rows;
    int screen_cols;
    int render_x;
    EditorMode mode;
    int show_line_numbers; /* 0=off, 1=on */
    int relative_line_numbers; /* 0=absolute only, 1=relative (abs on current) */
    char status_msg[256];
    char command_buf[80];
    int command_len;
    /* Clipboard (shared across buffers) */
    SizedStr clipboard;
    /* Search (shared across buffers) */
    SizedStr search_query;
    /* Quickfix pane */
    Qf qf;
    /* Command history */
    CmdHistory history;
    /* If set by a command, remain in MODE_COMMAND after executing it */
    int stay_in_command;
    /* Command-line completion (Tab) */
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

/* Editor operations */

/* Input handling */
int ed_read_key(void);
void ed_process_keypress(void);
void ed_move_cursor(int key);
void ed_process_command(void);

/* Mode management */
void ed_set_mode(EditorMode new_mode);

/* Status messages */
void ed_set_status_message(const char *fmt, ...);

/* Init */
void ed_init(void);

void ed_change_cursor_shape(void);


#endif
