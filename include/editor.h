#ifndef EDITOR_H
#define EDITOR_H

#include "buffer.h"
#include "sizedstr.h"

#define HED_VERSION "0.2.0"
#define TAB_STOP 4
#define CTRL_KEY(k) ((k) & 0x1f)
#define MAX_BUFFERS 128

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
    int screen_rows;
    int screen_cols;
    int render_x;
    EditorMode mode;
    char status_msg[80];
    char command_buf[80];
    int command_len;
    /* Clipboard (shared across buffers) */
    SizedStr clipboard;
    /* Search (shared across buffers) */
    SizedStr search_query;
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
