#include "hed.h"
#include "keybinds.h"
#include "safe_string.h"
#include <time.h>

/* Internal buffer row helpers (non-public, defined in buffer.c) */
void buf_row_del_in(Buffer *buf, int at);

#define MAX_KEYBINDS 256
#define KEY_BUFFER_SIZE 16
#define SEQUENCE_TIMEOUT_MS 1000  /* 1 second timeout for multi-key sequences */

/* Keybinding entry */
typedef struct {
    char *sequence;
    KeybindCallback callback;
    CommandCallback command_callback; /* optional: invoked with cmdline */
    int mode;
    char *desc; /* stores command line when using command_callback */
} Keybind;

/* Global keybinding storage */
static Keybind keybinds[MAX_KEYBINDS];
static int keybind_count = 0;

/* Input buffer for multi-key sequences */
static char key_buffer[KEY_BUFFER_SIZE];
static int key_buffer_len = 0;
static struct timespec last_key_time;
static int pending_count = 0; /* numeric prefix */
static int have_count = 0;

/* Visual selection helpers */
static void visual_clear(Window *win) {
    if (!win) return;
    win->sel.type = SEL_NONE;
}

Selection kb_make_selection(Window *win, Buffer *buf, SelectionType forced_type) {
    Selection s = (Selection){ .type = SEL_NONE };
    if (!win || !buf) return s;
    if (forced_type != SEL_NONE) {
        s.type = forced_type;
    } else if (win->sel.type != SEL_NONE) {
        s = win->sel;
    }
    if (s.type == SEL_CHAR) {
        s.anchor_y = win->sel.anchor_y;
        s.anchor_x = win->sel.anchor_x;
        s.cursor_y = win->cursor.y;
        s.cursor_x = win->cursor.x;
    } else if (s.type == SEL_BLOCK) {
        s.anchor_y = win->sel.anchor_y;
        s.cursor_y = win->cursor.y;
        s.anchor_rx = win->sel.anchor_rx;
        s.block_start_rx = win->sel.anchor_rx;
        s.block_end_rx = buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
        if (s.block_start_rx > s.block_end_rx) {
            int t = s.block_start_rx; s.block_start_rx = s.block_end_rx; s.block_end_rx = t;
        }
    } else if (s.type == SEL_LINE) {
        s.anchor_y = win->cursor.y;
        s.cursor_y = win->cursor.y;
    }
    return s;
}

static void visual_begin(int block) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;
    if (win->cursor.y < 0 || win->cursor.y >= buf->num_rows) return;
    win->sel.type = block ? SEL_BLOCK : SEL_CHAR;
    win->sel.anchor_y = win->cursor.y;
    win->sel.anchor_x = win->cursor.x;
    win->sel.anchor_rx = buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
    win->sel.block_start_rx = win->sel.anchor_rx;
    win->sel.block_end_rx = win->sel.anchor_rx;
    ed_set_mode(block ? MODE_VISUAL_BLOCK : MODE_VISUAL);
}

static int visual_char_range(Buffer *buf, Window *win,
                             int *sy, int *sx, int *ey, int *ex_excl) {
    if (!buf || !win || win->sel.type != SEL_CHAR) return 0;
    if (!BOUNDS_CHECK(win->sel.anchor_y, buf->num_rows) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return 0;
    int ay = win->sel.anchor_y, ax = win->sel.anchor_x;
    int cy = win->cursor.y, cx = win->cursor.x;
    int top_y = ay, top_x = ax, bot_y = cy, bot_x = cx;
    if (ay > cy || (ay == cy && ax > cx)) {
        top_y = cy; top_x = cx;
        bot_y = ay; bot_x = ax;
    }
    if (top_y < 0) top_y = 0;
    if (bot_y >= buf->num_rows) bot_y = buf->num_rows - 1;
    Row *top_row = &buf->rows[top_y];
    Row *bot_row = &buf->rows[bot_y];
    if (top_x > (int)top_row->chars.len) top_x = (int)top_row->chars.len;
    if (bot_x > (int)bot_row->chars.len) bot_x = (int)bot_row->chars.len;
    if (sy) *sy = top_y;
    if (sx) *sx = top_x;
    if (ey) *ey = bot_y;
    if (ex_excl) *ex_excl = bot_x + 1;
    return 1;
}

static int visual_block_range(Buffer *buf, Window *win,
                              int *sy, int *ey, int *start_rx, int *end_rx_excl) {
    if (!buf || !win || win->sel.type != SEL_BLOCK) return 0;
    if (!BOUNDS_CHECK(win->sel.anchor_y, buf->num_rows) ||
        !BOUNDS_CHECK(win->cursor.y, buf->num_rows)) return 0;
    int top_y = win->sel.anchor_y < win->cursor.y ? win->sel.anchor_y : win->cursor.y;
    int bot_y = win->sel.anchor_y > win->cursor.y ? win->sel.anchor_y : win->cursor.y;
    int cur_rx = buf_row_cx_to_rx(&buf->rows[win->cursor.y], win->cursor.x);
    int start = win->sel.anchor_rx < cur_rx ? win->sel.anchor_rx : cur_rx;
    int end = win->sel.anchor_rx > cur_rx ? win->sel.anchor_rx : cur_rx;
    if (sy) *sy = top_y;
    if (ey) *ey = bot_y;
    if (start_rx) *start_rx = start;
    if (end_rx_excl) *end_rx_excl = end + 1;
    return 1;
}

static void sstr_delete_range(SizedStr *s, int start, int end_excl) {
    if (!s || !s->data) return;
    if (start < 0) start = 0;
    if (end_excl < start) end_excl = start;
    if (end_excl > (int)s->len) end_excl = (int)s->len;
    if (start > (int)s->len) start = (int)s->len;
    int rem = (int)s->len - end_excl;
    memmove(s->data + start, s->data + end_excl, (size_t)rem);
    s->len -= (end_excl - start);
    if (s->data) s->data[s->len] = '\0';
}

static int visual_yank(Buffer *buf, Window *win, int block_mode) {
    if (!buf || !win || win->sel.type == SEL_NONE) return 0;
    if (block_mode != 0) block_mode = 1;
    if (win->sel.type == SEL_BLOCK) block_mode = 1;
    SizedStr clip = sstr_new();
    if (!block_mode) {
        int sy, sx, ey, ex_excl;
        if (!visual_char_range(buf, win, &sy, &sx, &ey, &ex_excl)) { sstr_free(&clip); return 0; }
        for (int y = sy; y <= ey; y++) {
            Row *r = &buf->rows[y];
            int start = (y == sy) ? sx : 0;
            int end_excl = (y == ey) ? ex_excl : (int)r->chars.len;
            if (start < 0) start = 0;
            if (end_excl < start) end_excl = start;
            if (end_excl > (int)r->chars.len) end_excl = (int)r->chars.len;
            sstr_append(&clip, r->chars.data + start, (size_t)(end_excl - start));
            if (y != ey) sstr_append_char(&clip, '\n');
        }
    } else {
        int sy, ey, start_rx, end_rx_excl;
        if (!visual_block_range(buf, win, &sy, &ey, &start_rx, &end_rx_excl)) { sstr_free(&clip); return 0; }
        for (int y = sy; y <= ey; y++) {
            Row *r = &buf->rows[y];
            int cx0 = buf_row_rx_to_cx(r, start_rx);
            int cx1 = buf_row_rx_to_cx(r, end_rx_excl);
            if (cx0 < 0) cx0 = 0;
            if (cx1 < cx0) cx1 = cx0;
            if (cx1 > (int)r->chars.len) cx1 = (int)r->chars.len;
            sstr_append(&clip, r->chars.data + cx0, (size_t)(cx1 - cx0));
            if (y != ey) sstr_append_char(&clip, '\n');
        }
    }
    sstr_free(&E.clipboard);
    E.clipboard = clip;
    E.clipboard_is_block = block_mode ? 1 : 0;
    regs_set_yank_block(E.clipboard.data, E.clipboard.len, E.clipboard_is_block);
    ed_set_status_message("Yanked");
    return 1;
}

static int visual_delete(Buffer *buf, Window *win, int block_mode) {
    if (!buf || !win || win->sel.type == SEL_NONE) return 0;
    if (buf->readonly) { ed_set_status_message("Buffer is read-only"); return 0; }
    if (block_mode != 0) block_mode = 1;
    if (win->sel.type == SEL_BLOCK) block_mode = 1;
    if (!visual_yank(buf, win, block_mode)) return 0;

    if (!block_mode) {
        int sy, sx, ey, ex_excl;
        if (!visual_char_range(buf, win, &sy, &sx, &ey, &ex_excl)) return 0;
        if (!undo_is_applying()) {
            undo_begin_group();
            undo_push_delete(sy, sx,
                             E.clipboard.data, E.clipboard.len,
                             win->cursor.y, win->cursor.x,
                             sy, sx);
            undo_commit_group();
        }
        Row *start = &buf->rows[sy];
        int start_len = (int)start->chars.len;
        if (sx > start_len) sx = start_len;

        if (sy == ey) {
            int end_ex = ex_excl;
            if (end_ex > start_len) end_ex = start_len;
            sstr_delete_range(&start->chars, sx, end_ex);
            buf_row_update(start);
        } else {
            Row *end = &buf->rows[ey];
            int end_ex = ex_excl;
            if (end_ex > (int)end->chars.len) end_ex = (int)end->chars.len;
            SizedStr tail = sstr_from(end->chars.data + end_ex,
                                      end->chars.len - (size_t)end_ex);
            start->chars.len = (size_t)sx;
            if (start->chars.data) start->chars.data[sx] = '\0';
            sstr_append(&start->chars, tail.data, tail.len);
            sstr_free(&tail);
            buf_row_update(start);
            /* Remove intermediate rows including end row */
            for (int y = ey; y > sy; y--) {
                buf_row_del_in(buf, y);
            }
        }
        buf->dirty++;
        win->cursor.y = sy;
        win->cursor.x = sx;
    } else {
        int sy, ey, start_rx, end_rx_excl;
        if (!visual_block_range(buf, win, &sy, &ey, &start_rx, &end_rx_excl)) return 0;
        int made_group = 0;
        if (!undo_is_applying()) { undo_begin_group(); made_group = 1; }
        for (int y = sy; y <= ey; y++) {
            Row *r = &buf->rows[y];
            int cx0 = buf_row_rx_to_cx(r, start_rx);
            int cx1 = buf_row_rx_to_cx(r, end_rx_excl);
            if (cx0 < 0) cx0 = 0;
            if (cx1 < cx0) cx1 = cx0;
            if (cx1 > (int)r->chars.len) cx1 = (int)r->chars.len;
            if (cx0 == cx1) continue;
            if (!undo_is_applying()) {
                SizedStr seg = sstr_from(r->chars.data + cx0, (size_t)(cx1 - cx0));
                undo_push_delete(y, cx0, seg.data, seg.len,
                                 win->cursor.y, win->cursor.x,
                                 y, cx0);
                sstr_free(&seg);
            }
            sstr_delete_range(&r->chars, cx0, cx1);
            buf_row_update(r);
        }
        if (made_group && !undo_is_applying()) undo_commit_group();
        buf->dirty++;
        win->cursor.y = sy;
        Row *r = &buf->rows[win->cursor.y];
        win->cursor.x = buf_row_rx_to_cx(r, start_rx);
    }
    visual_clear(win);
    ed_set_mode(MODE_NORMAL);
    return 1;
}

/* Expose small wrappers for other modules */
void kb_visual_clear(Window *win) { visual_clear(win); }
void kb_visual_begin(int block) { visual_begin(block); }
int kb_visual_yank(Buffer *buf, Window *win, int block_mode) { return visual_yank(buf, win, block_mode); }
int kb_visual_delete(Buffer *buf, Window *win, int block_mode) { return visual_delete(buf, win, block_mode); }

/* Helper: convert key code to string representation */
static void key_to_string(int key, char *buf, size_t bufsize) {
    if (key >= 32 && key < 127) {
        /* Printable ASCII */
        snprintf(buf, bufsize, "%c", key);
    } else if (key == 127) {
        snprintf(buf, bufsize, "<BS>");
    } else if (key == '\r') {
        snprintf(buf, bufsize, "<CR>");
    } else if (key == '\n') {
        snprintf(buf, bufsize, "<NL>");
    } else if (key == '\t') {
        snprintf(buf, bufsize, "<Tab>");
    } else if (key == '\x1b') {
        snprintf(buf, bufsize, "<Esc>");
    } else if (key >= 1 && key <= 26) {
        /* Ctrl+letter */
        snprintf(buf, bufsize, "<C-%c>", key + 'a' - 1);
    } else {
        /* Unknown key */
        snprintf(buf, bufsize, "<%d>", key);
    }
}

/* Helper: check if elapsed time exceeds timeout */
static int timeout_exceeded(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long elapsed_ms = (now.tv_sec - last_key_time.tv_sec) * 1000 +
                      (now.tv_nsec - last_key_time.tv_nsec) / 1000000;

    return elapsed_ms > SEQUENCE_TIMEOUT_MS;
}

/*** Default keybinding callbacks ***/

/* Normal mode - mode switching */

void kb_enter_insert_mode(void) {
    ed_set_mode(MODE_INSERT);
}

void kb_append_mode(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;

    ed_set_mode(MODE_INSERT);
    if (win->cursor.y < buf->num_rows) {
        Row *row = &buf->rows[win->cursor.y];
        if (win->cursor.x < (int)row->chars.len) win->cursor.x++;
    }
}

void kb_enter_command_mode(void) {
    extern Ed E;
    ed_set_mode(MODE_COMMAND);
    E.command_len = 0;
}

void kb_visual_toggle(void) {
    Window *win = window_cur();
    if (!win) return;
    if (E.mode == MODE_VISUAL && win->sel.type == SEL_CHAR) {
        visual_clear(win);
        ed_set_mode(MODE_NORMAL);
        return;
    }
    visual_begin(0);
}

void kb_visual_block_toggle(void) {
    Window *win = window_cur();
    if (!win) return;
    if (E.mode == MODE_VISUAL_BLOCK && win->sel.type == SEL_BLOCK) {
        visual_clear(win);
        ed_set_mode(MODE_NORMAL);
        return;
    }
    visual_begin(1);
}

/* Normal mode - text operations */
void kb_delete_line(void) {
    Buffer *buf = buf_cur(); if (!buf) return;
    buf_delete_line_in(buf);
}

void kb_yank_line(void) {
    Buffer *buf = buf_cur(); if (!buf) return;
    buf_yank_line_in(buf);
    ed_set_status_message("Yanked");
}

void kb_paste(void) {
    Buffer *buf = buf_cur(); if (!buf) return;
    buf_paste_in(buf);
}

void kb_delete_char(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;
    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return;
    }
    if (win->cursor.y >= buf->num_rows) return;

    Row *row = &buf->rows[win->cursor.y];
    int cx = win->cursor.x;
    if (cx >= (int)row->chars.len) return;

    int y = win->cursor.y;
    char deleted_char = row->chars.data[cx];
    if (!undo_is_applying()) {
        undo_begin_group();
        undo_push_delete(y, cx, &deleted_char, 1, y, cx, y, cx);
        undo_commit_group();
    }
    /* Delete the character under the cursor, not before it */
    sstr_delete_char(&row->chars, cx);
    buf_row_update(row);
    buf->dirty++;

    /* Fire hook */
    HookCharEvent event = {buf, y, cx, deleted_char};
    hook_fire_char(HOOK_CHAR_DELETE, &event);
}

/* Normal mode - cursor movement */
void kb_cursor_line_start(void) {
    Window *win = window_cur();
    if (!win) return;
    win->cursor.x = 0;
}

void kb_cursor_line_end(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;
    if (win->cursor.y < buf->num_rows) {
        win->cursor.x = buf->rows[win->cursor.y].chars.len;
    }
}

void kb_cursor_top(void) {
    Window *win = window_cur();
    if (!win) return;
    win->cursor.y = 0;
    win->cursor.x = 0;
}

void kb_cursor_bottom(void) {
    Buffer *buf = buf_cur();
    Window *win = window_cur();
    if (!buf || !win) return;
    win->cursor.y = buf->num_rows - 1;
    if (win->cursor.y < 0) win->cursor.y = 0;
    win->cursor.x = 0;
}

/* Normal mode - search */
void kb_search_next(void) {
    Buffer *buf = buf_cur(); if (!buf) return;
    buf_find_in(buf);
}

void kb_find_under_cursor(void) {
    SizedStr w = sstr_new();
    if (!buf_get_word_under_cursor(&w)) { sstr_free(&w); return; }
    sstr_free(&E.search_query);
    E.search_query = sstr_from(w.data, w.len);
    ed_set_status_message("* %.*s", (int)(w.len > 40 ? 40 : w.len), w.data);
    sstr_free(&w);
    buf_find_in(buf_cur());
}

void kb_line_number_toggle(void) {
    cmd_ln(NULL);
}
/* Undo/Redo */
void kb_undo(void) {
    if (undo_perform()) {
        ed_set_status_message("Undid");
    } else {
        ed_set_status_message("Nothing to undo");
    }
}

void kb_redo(void) {
    if (redo_perform()) {
        ed_set_status_message("Redid");
    } else {
        ed_set_status_message("Nothing to redo");
    }
}

/* Helper: perform jump in specified direction */
static void kb_jump(int direction) {
    int buffer_idx, cursor_x, cursor_y;
    int success;
    const char *direction_str;
    const char *limit_msg;

    if (direction < 0) {
        success = jump_list_backward(&E.jump_list, &buffer_idx, &cursor_x, &cursor_y);
        direction_str = "back";
        limit_msg = "Already at oldest jump position";
    } else {
        success = jump_list_forward(&E.jump_list, &buffer_idx, &cursor_x, &cursor_y);
        direction_str = "forward";
        limit_msg = "Already at newest jump position";
    }

    if (success) {
        /* Switch to the buffer without adding to jump list */
        if (buffer_idx >= 0 && buffer_idx < (int)E.buffers.len) {
            E.current_buffer = buffer_idx;
            Window *win = window_cur();
            if (win) {
                win->buffer_index = buffer_idx;
                win->cursor.x = cursor_x;
                win->cursor.y = cursor_y;
            }

            Buffer *buf = buf_cur();
            ed_set_status_message("Jumped %s to buffer %d: %s", direction_str,
                buffer_idx + 1, buf->title);
        } else {
            ed_set_status_message("Jump target buffer no longer exists");
        }
    } else {
        ed_set_status_message("%s", limit_msg);
    }
}

void kb_jump_backward(void) {
    kb_jump(-1);
}

void kb_jump_forward(void) {
    kb_jump(1);
}

/* Tmux integration: send current paragraph to tmux runner pane */
void kb_tmux_send_line(void) {
    SizedStr para = sstr_new();
    if (!buf_get_paragraph_under_cursor(&para) || para.len == 0) {
        sstr_free(&para);
        ed_set_status_message("tmux: no paragraph to send");
        return;
    }

    char *cmd = malloc(para.len + 1);
    if (!cmd) {
        sstr_free(&para);
        ed_set_status_message("tmux: out of memory");
        return;
    }

    memcpy(cmd, para.data, para.len);
    cmd[para.len] = '\0';

    tmux_send_command(cmd);

    free(cmd);
    sstr_free(&para);
}


/* Initialize keybinding system */
void keybind_init(void) {
    keybind_count = 0;
    key_buffer_len = 0;
    key_buffer[0] = '\0';
    clock_gettime(CLOCK_MONOTONIC, &last_key_time);
    user_keybinds_init();
}

/* Register a keybinding */
void keybind_register(int mode, const char *sequence, KeybindCallback callback) {
    if (keybind_count >= MAX_KEYBINDS) {
        return;
    }

    keybinds[keybind_count].sequence = strdup(sequence);
    keybinds[keybind_count].callback = callback;
    keybinds[keybind_count].mode = mode;
    keybinds[keybind_count].command_callback = NULL;
    keybinds[keybind_count].desc = NULL;
    keybind_count++;
}

static void kb_run_command(const char *cmdline) {
    if (!cmdline) return;
    while (*cmdline == ' ' || *cmdline == '\t' || *cmdline == ':') cmdline++;
    const char *p = cmdline;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (p == cmdline) return;
    char name[64];
    size_t n = (size_t)(p - cmdline); if (n >= sizeof(name)) n = sizeof(name) - 1;
    memcpy(name, cmdline, n); name[n] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    const char *args = (*p ? p : NULL);
    command_invoke(name, args);
}

void keybind_register_command(int mode, const char *sequence, const char *cmdline) {
    if (keybind_count >= MAX_KEYBINDS) return;
    keybinds[keybind_count].sequence = strdup(sequence);
    keybinds[keybind_count].callback = NULL;
    keybinds[keybind_count].command_callback = kb_run_command;
    keybinds[keybind_count].mode = mode;
    keybinds[keybind_count].desc = cmdline ? strdup(cmdline) : strdup("");
    keybind_count++;
}


/* Clear the key buffer */
void keybind_clear_buffer(void) {
    key_buffer_len = 0;
    key_buffer[0] = '\0';
    pending_count = 0;
    have_count = 0;
}

/* Process a key press through the keybinding system */
int keybind_process(int key, int mode) {
    /* Numeric prefix only applies in normal mode */
    if (mode != MODE_NORMAL) {
        pending_count = 0;
        have_count = 0;
    }

    /* Check timeout - if too much time passed, clear buffer */
    if (key_buffer_len > 0 && timeout_exceeded()) {
        keybind_clear_buffer();
    }

    /* Update timestamp */
    clock_gettime(CLOCK_MONOTONIC, &last_key_time);

    /* Numeric prefix handling (normal mode, idle buffer) */
    if (mode == MODE_NORMAL && key_buffer_len == 0) {
        if (key >= '0' && key <= '9') {
            if (have_count || key != '0') {
                int digit = key - '0';
                pending_count = pending_count * 10 + digit;
                if (pending_count > 1000000) pending_count = 1000000; /* cap runaway counts */
                have_count = 1;
                return 1; /* consume digit, wait for next key */
            }
        }
    }

    /* Convert key to string and append to buffer */
    char key_str[32];
    key_to_string(key, key_str, sizeof(key_str));

    /* Append to buffer if there's space */
    if (key_buffer_len + strlen(key_str) < KEY_BUFFER_SIZE - 1) {
        EdError err = safe_strcat(key_buffer, key_str, KEY_BUFFER_SIZE);
        if (err != ED_OK) {
            /* Shouldn't happen due to check above, but handle gracefully */
            keybind_clear_buffer();
            safe_strcpy(key_buffer, key_str, KEY_BUFFER_SIZE);
        }
        key_buffer_len = strlen(key_buffer);
    } else {
        /* Buffer full, clear and start over */
        keybind_clear_buffer();
        safe_strcpy(key_buffer, key_str, KEY_BUFFER_SIZE);
        key_buffer_len = strlen(key_buffer);
    }

    /* Check for exact matches */
    int exact_match = -1;
    int partial_match = 0;

    for (int i = 0; i < keybind_count; i++) {
        /* Skip if mode doesn't match */
        if (keybinds[i].mode != mode) {
            continue;
        }

        /* Check for exact match */
        if (strcmp(keybinds[i].sequence, key_buffer) == 0) {
            exact_match = i;
            break;
        }

        /* Check for partial match (sequence starts with buffer) */
        if (strncmp(keybinds[i].sequence, key_buffer, key_buffer_len) == 0) {
            partial_match = 1;
        }
    }

    /* Exact match found - execute action */
    if (exact_match >= 0) {
        int repeat = have_count ? pending_count : 1;
        if (repeat < 1) repeat = 1;
        if (keybinds[exact_match].callback) {
            for (int r = 0; r < repeat; r++) {
                keybinds[exact_match].callback();
            }
        } else if (keybinds[exact_match].command_callback) {
            for (int r = 0; r < repeat; r++) {
                keybinds[exact_match].command_callback(keybinds[exact_match].desc);
            }
        }
        keybind_clear_buffer();
        return 1;
    }

    /* Partial match - wait for more keys */
    if (partial_match) {
        return 1;  /* Consumed the key, waiting for more */
    }

    /* No match - clear buffer and return 0 (not handled) */
    keybind_clear_buffer();
    return 0;
}
