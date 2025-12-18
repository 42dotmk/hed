#include "cmd_misc.h"
#include "../hed.h"
#include "../macros.h"
#include "../registers.h"
#include "../utils/ctags.h"
#include "../utils/fold.h"
#include "../fold_methods/fold_methods.h"
#include "cmd_util.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper functions for escape sequence parsing */
static int hexval(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static char *unescape_string(const char *in) {
    if (!in) {
        char *empty = strdup("");
        return empty ? empty : NULL;
    }
    size_t n = strlen(in);
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    size_t oi = 0;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        if (c == '\\' && i + 1 < n) {
            char nxt = in[++i];
            switch (nxt) {
            case 'n':
                out[oi++] = '\n';
                break;
            case 'r':
                out[oi++] = '\r';
                break;
            case 't':
                out[oi++] = '\t';
                break;
            case '\\':
                out[oi++] = '\\';
                break;
            case '"':
                out[oi++] = '"';
                break;
            case '\'':
                out[oi++] = '\'';
                break;
            case 'x': {
                if (i + 2 < n) {
                    int h1 = hexval(in[i + 1]);
                    int h2 = hexval(in[i + 2]);
                    if (h1 >= 0 && h2 >= 0) {
                        out[oi++] = (char)((h1 << 4) | h2);
                        i += 2;
                        break;
                    }
                }
                out[oi++] = 'x';
                break;
            }
            default:
                out[oi++] = nxt;
                break;
            }
        } else {
            out[oi++] = c;
        }
    }
    out[oi] = '\0';
    return out;
}

void cmd_list_commands(const char *args) {
    (void)args;
    char msg[256] = "";
    int off = 0;
    for (int i = 0; i < command_count; i++) {
        const char *nm = commands[i].name ? commands[i].name : "";
        const char *ds = commands[i].desc ? commands[i].desc : "";
        int wrote = snprintf(msg + off, (int)sizeof(msg) - off,
                             i == 0 ? "%s: %s" : " | %s: %s", nm, ds);
        if (wrote < 0)
            break;
        off += wrote;
        if (off >= (int)sizeof(msg) - 1) {
            off = (int)sizeof(msg) - 1;
            break;
        }
    }
    if (off == 0)
        snprintf(msg, sizeof(msg), "No commands");
    ed_set_status_message("%s", msg);
}

void cmd_echo(const char *args) {
    char *msg = unescape_string(args);
    if (msg) {
        ed_set_status_message("%s", msg);
        free(msg);
    }
}

void cmd_history(const char *args) {
    int n = 20;
    if (args && *args) {
        char *end = NULL;
        long v = strtol(args, &end, 10);
        if (end != args && v > 0 && v < 100000)
            n = (int)v;
    }
    int hlen = hist_len(&E.history);
    if (n > hlen)
        n = hlen;
    if (n <= 0) {
        ed_set_status_message("(no history)");
        return;
    }

    char buf[256];
    int off = 0;
    for (int i = 0; i < n; i++) {
        const char *line = hist_get(&E.history, i);
        if (!line)
            continue;
        int wrote = snprintf(buf + off, (int)sizeof(buf) - off,
                             i == 0 ? "%s" : "\n%s", line);
        if (wrote < 0)
            break;
        off += wrote;
        if (off >= (int)sizeof(buf) - 1) {
            off = (int)sizeof(buf) - 1;
            break;
        }
    }
    buf[off] = '\0';
    ed_set_status_message("%s", buf);
}

void cmd_registers(const char *args) {
    (void)args;
    char out[256];
    int off = 0;
    const char regs_list[] = {'"', '0', '1', '2', '3', '4',
                              '5', '6', '7', '8', '9', ':'};

    for (size_t i = 0; i < sizeof(regs_list); i++) {
        const SizedStr *s = regs_get(regs_list[i]);
        if (!s)
            continue;
        int wrote =
            snprintf(out + off, (int)sizeof(out) - off,
                     i == 0 ? "%c %.*s" : "\n%c %.*s", regs_list[i],
                     (int)(s->len > 40 ? 40 : s->len), s->data ? s->data : "");
        if (wrote < 0)
            break;
        off += wrote;
        if (off >= (int)sizeof(out) - 1)
            break;
    }

    /* named registers */
    for (char c = 'a'; c <= 'z'; c++) {
        const SizedStr *s = regs_get(c);
        if (!s || s->len == 0)
            continue;
        int wrote =
            snprintf(out + off, (int)sizeof(out) - off, "\n%c %.*s", c,
                     (int)(s->len > 40 ? 40 : s->len), s->data ? s->data : "");
        if (wrote < 0)
            break;
        off += wrote;
        if (off >= (int)sizeof(out) - 1)
            break;
    }
    out[off] = '\0';
    ed_set_status_message("%s", out);
}

void cmd_put(const char *args) {
    char reg = '"';
    if (args && *args) {
        while (*args == ' ')
            args++;
        if (*args == '\"' || *args == '@')
            args++;
        if (*args)
            reg = *args;
    }
    const SizedStr *s = regs_get(reg);
    if (!s || s->len == 0) {
        ed_set_status_message("Register %c empty", reg);
        return;
    }
    Buffer *buf = buf_cur();
    if (!buf)
        return;
    /* Use the same paste machinery as normal-mode 'p' so undo works */
    sstr_free(&E.clipboard);
    E.clipboard = sstr_from(s->data, s->len);
    buf_paste_in(buf);
}

void cmd_undo(const char *args) {
    (void)args;
    if (undo_perform()) {
        ed_set_status_message("Undid");
    } else {
        ed_set_status_message("Nothing to undo");
    }
}

void cmd_redo(const char *args) {
    (void)args;
    if (redo_perform()) {
        ed_set_status_message("Redid");
    } else {
        ed_set_status_message("Nothing to redo");
    }
}

void cmd_repeat(const char *args) {
    (void)args;
    /* Get the last executed keybind sequence from '.' register */
    const SizedStr *dot_reg = regs_get('.');
    if (!dot_reg || !dot_reg->data || dot_reg->len == 0) {
        ed_set_status_message("No previous command to repeat");
        return;
    }

    /* Replay the sequence through the macro queue */
    macro_replay_string(dot_reg->data, dot_reg->len);
}

void cmd_macro_record(const char *args) {
    (void)args;
    if (macro_is_recording()) {
        char reg = macro_get_recording_register();
        macro_stop_recording();
        ed_set_status_message("Stopped recording macro to register '%c'", reg);
        return;
    }

    /* Read next key to determine which register */
    int key = ed_read_key();

    /* Convert to lowercase if needed */
    if (key >= 'A' && key <= 'Z')
        key = key - 'A' + 'a';

    /* Check if valid register (a-z) */
    if (key < 'a' || key > 'z') {
        ed_set_status_message("Invalid register for macro recording");
        return;
    }

    /* Start recording */
    macro_start_recording((char)key);
    ed_set_status_message("Recording macro to register '%c'...", (char)key);
}

void cmd_macro_play(const char *args) {
    (void)args;
    /* Read next key to determine which register */
    int key = ed_read_key();

    /* Handle @@ - replay last macro */
    if (key == '@') {
        macro_play_last();
        return;
    }

    /* Convert to lowercase if needed */
    if (key >= 'A' && key <= 'Z')
        key = key - 'A' + 'a';

    /* Check if valid register (a-z) */
    if (key < 'a' || key > 'z') {
        ed_set_status_message("Invalid register for macro playback");
        return;
    }

    /* Play the macro */
    macro_play((char)key);
}

void cmd_ln(const char *args) {
    (void)args;
    if (!E.show_line_numbers) {
        E.show_line_numbers = 1;
    } else {
        E.show_line_numbers = 0;
        E.relative_line_numbers = 0;
    }
    ed_set_status_message(
        "Line numbers: %s",
        !E.show_line_numbers
            ? "off"
            : (E.relative_line_numbers ? "relative" : "absolute"));
}

void cmd_rln(const char *args) {
    (void)args;
    if (!E.relative_line_numbers) {
        E.relative_line_numbers = 1;
        if (!E.show_line_numbers)
            E.show_line_numbers = 1;
    } else {
        E.relative_line_numbers = 0;
    }
    ed_set_status_message("Relative line numbers: %s",
                          E.relative_line_numbers ? "on" : "off");
}

void cmd_wrap(const char *args) {
    (void)args;
    Window *win = window_cur();
    if (!win)
        return;
    win->wrap = !win->wrap;
    ed_set_status_message("wrap: %s", win->wrap ? "on" : "off");
}

void cmd_wrapdefault(const char *args) {
    (void)args;
    E.default_wrap = !E.default_wrap;
    ed_set_status_message("wrap default: %s", E.default_wrap ? "on" : "off");
}

void cmd_logclear(const char *args) {
    (void)args;
    log_clear();
    ed_set_status_message("log cleared");
}

/* Format current buffer using an external formatter based on filetype.
 * NOTE: This currently does NOT integrate with undo history; formatting
 * is treated like a save+reload operation. */
void cmd_fmt(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf)
        return;
    if (!buf->filename || !*buf->filename) {
        ed_set_status_message("fmt: buffer has no filename");
        return;
    }

    const char *ft = buf->filetype ? buf->filetype : "txt";
    const char *tmpl = NULL;

    if (strcmp(ft, "c") == 0 || strcmp(ft, "cpp") == 0) {
        tmpl = "clang-format -i %s";
    } else if (strcmp(ft, "rust") == 0) {
        tmpl = "rustfmt %s";
    } else if (strcmp(ft, "go") == 0) {
        tmpl = "gofmt -w %s";
    } else if (strcmp(ft, "python") == 0) {
        tmpl = "black %s";
    } else if (strcmp(ft, "javascript") == 0 || strcmp(ft, "typescript") == 0) {
        tmpl = "prettier --write %s";
    } else if (strcmp(ft, "json") == 0) {
        tmpl = "prettier --parser json --write %s";
    } else if (strcmp(ft, "html") == 0 || strcmp(ft, "css") == 0 ||
               strcmp(ft, "markdown") == 0) {
        tmpl = "prettier --write %s";
    } else {
        ed_set_status_message("fmt: no formatter for filetype '%s'", ft);
        return;
    }

    /* Save buffer so formatter sees latest contents. */
    EdError serr = buf_save_in(buf);
    if (serr != ED_OK) {
        ed_set_status_message("fmt: save failed: %s", ed_error_string(serr));
        return;
    }

    char esc_path[1024];
    shell_escape_single(buf->filename, esc_path, sizeof(esc_path));

    char cmd[1536];
    snprintf(cmd, sizeof(cmd), tmpl, esc_path);

    /* Run formatter non-interactively, temporarily leaving raw mode. */
    disable_raw_mode();
    int status = system(cmd);
    enable_raw_mode();

    if (status != 0) {
        ed_set_status_message("fmt: formatter exited with status %d", status);
        return;
    }

    /* Reload buffer from disk to pick up formatted content. */
    buf_reload(buf);
    ed_set_status_message("fmt: formatted (%s)", buf->filename);
}

void cmd_ts(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("ts: %s", ts_is_enabled() ? "on" : "off");
        return;
    }
    if (strcmp(args, "on") == 0) {
        ts_set_enabled(1);
        for (int i = 0; i < (int)E.buffers.len; i++) {
            ts_buffer_autoload(&E.buffers.data[i]);
            ts_buffer_reparse(&E.buffers.data[i]);
        }
        ed_set_status_message("ts: on");
    } else if (strcmp(args, "off") == 0) {
        ts_set_enabled(0);
        ed_set_status_message("ts: off");
    } else if (strcmp(args, "auto") == 0) {
        ts_set_enabled(1);
        Buffer *b = buf_cur();
        if (b) {
            if (!ts_buffer_autoload(b))
                ed_set_status_message("ts: no lang for current file");
            ts_buffer_reparse(b);
        }
        ed_set_status_message("ts: auto");
    } else {
        ed_set_status_message("ts: on|off|auto");
    }
}

void cmd_tslang(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("tslang: <name>");
        return;
    }
    Buffer *b = buf_cur();
    if (!b)
        return;
    ts_set_enabled(1);
    if (!ts_buffer_load_language(b, args)) {
        ed_set_status_message("tslang: failed for %s", args);
        return;
    }
    ts_buffer_reparse(b);
    ed_set_status_message("tslang: %s", args);
}

void cmd_tsi(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("tsi: <lang>");
        return;
    }
    char cmd[256];
    /* Run tsi from build/; assumes hed is run from repo root */
    snprintf(cmd, sizeof(cmd), "tsi %s", args);
    cmd_shell(cmd);
}

void cmd_new_line(const char *args) {
    (void)args;
    Window *win = window_cur();
    if (!win || win->is_quickfix)
        return;
    Buffer *buf = buf_cur();
    if (!buf)
        return;

    if (buf->num_rows == 0) {
        win->cursor.y = 0;
        win->cursor.x = 0;
    } else {
        Row *row =
            (win->cursor.y < buf->num_rows) ? &buf->rows[win->cursor.y] : NULL;
        win->cursor.x = row ? (int)row->chars.len : 0;
    }
    buf_insert_newline_in(buf);
    ed_set_mode(MODE_INSERT);
}

void cmd_new_line_above(const char *args) {
    (void)args;
    Window *win = window_cur();
    if (!win || win->is_quickfix)
        return;
    Buffer *buf = buf_cur();
    if (!buf)
        return;

    win->cursor.x = 0;
    buf_insert_newline_in(buf);
    /* Cursor should now be on the new blank line above */
    ed_set_mode(MODE_INSERT);
}

void cmd_tmux_toggle(const char *args) {
    (void)args;
    tmux_toggle_pane();
}

void cmd_tmux_send(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :tmux_send <command>");
        return;
    }
    tmux_send_command(args);
}

void cmd_tmux_kill(const char *args) {
    (void)args;
    tmux_kill_pane();
}

void cmd_shell(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :shell <command>");
        return;
    }

    char cmd_buf[4096];
    snprintf(cmd_buf, sizeof(cmd_buf), "%s", args);

    bool acknowledge = true;
    const char *flag = "--skipwait";
    size_t flen = strlen(flag);
    char *p = cmd_buf;
    while ((p = strstr(p, flag))) {
        char before = (p == cmd_buf) ? ' ' : p[-1];
        char after = p[flen];
        if ((p == cmd_buf || isspace((unsigned char)before)) &&
            (after == '\0' || isspace((unsigned char)after))) {
            acknowledge = false;
            if (p > cmd_buf && isspace((unsigned char)p[-1]))
                p--;
            char *src = p + flen;
            while (isspace((unsigned char)*src))
                src++;
            memmove(p, src, strlen(src) + 1);
            continue;
        }
        p += flen;
    }

    while (isspace((unsigned char)cmd_buf[0])) {
        memmove(cmd_buf, cmd_buf + 1, strlen(cmd_buf));
    }
    if (cmd_buf[0] == '\0') {
        ed_set_status_message("Usage: :shell <command>");
        return;
    }

    /* Run command interactively, handing over the TTY */
    int status = term_cmd_run_interactive(cmd_buf, acknowledge);

    if (status == 0) {
        ed_set_status_message("Command completed successfully");
    } else if (status == -1) {
        ed_set_status_message("Failed to run command");
    } else {
        ed_set_status_message("Command exited with status %d", status);
    }

    ed_render_frame();
}

void cmd_git(const char *args) {
    (void)args;
    /* Run lazygit as a full-screen TUI, like fzf: temporarily leave raw mode.
     */
    int status = term_cmd_run_interactive("lazygit", false);
    if (status == 0) {
        ed_set_status_message("lazygit exited");
    } else if (status == -1) {
        ed_set_status_message("failed to run lazygit");
    } else {
        ed_set_status_message("lazygit exited with status %d", status);
    }
    ed_render_frame();
}

void cmd_reload(const char *args) {
    (void)args;
    /* Rebuild hed via make, then exec the new binary. */
    int status = term_cmd_run_interactive("make clean && make -j16", true);
    if (status != 0) {
        ed_set_status_message("reload: build failed (status %d)", status);
        return;
    }

    /* Leave raw mode before replacing the process image. */
    disable_raw_mode();

    /* Restart the editor; assume we are run from repo root. */
    const char *exe = "./build/hed";
    execl(exe, exe, (char *)NULL);

    /* If we reach here, exec failed. */
    perror("execl");
    enable_raw_mode();
    ed_set_status_message("reload: failed to exec %s", exe);
}

void cmd_tag(const char *args) {
    (void)args;
    goto_tag(args && *args ? args : NULL);
	buf_center_screen();
}

void cmd_modal_from_current(const char *args) {
    (void)args;

    /* Convert current window to modal */
    Window *modal = winmodal_from_current();
    if (!modal) {
        ed_set_status_message("Failed to create modal from current window");
        return;
    }

    /* Show the modal */
    winmodal_show(modal);
    ed_set_status_message("Window converted to modal");
}

void cmd_modal_to_layout(const char *args) {
    (void)args;

    /* Check if a modal is shown */
    Window *modal = winmodal_current();
    if (!modal) {
        ed_set_status_message("No modal window is currently shown");
        return;
    }

    /* Convert modal back to layout */
    winmodal_to_layout(modal);
    ed_set_status_message("Modal converted back to normal window");
}

/* Create a new fold region: :foldnew <start> <end> */
void cmd_fold_new(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("foldnew: no buffer");
        return;
    }

    if (!args || !*args) {
        ed_set_status_message("foldnew: usage: foldnew <start> <end>");
        return;
    }

    int start_line = 0, end_line = 0;
    if (sscanf(args, "%d %d", &start_line, &end_line) != 2) {
        ed_set_status_message("foldnew: usage: foldnew <start> <end>");
        return;
    }

    /* Convert from 1-indexed to 0-indexed */
    start_line--;
    end_line--;

    if (start_line < 0 || start_line >= buf->num_rows) {
        ed_set_status_message("foldnew: start line out of range");
        return;
    }

    if (end_line < start_line || end_line >= buf->num_rows) {
        ed_set_status_message("foldnew: end line out of range");
        return;
    }

    /* Mark the rows */
    buf->rows[start_line].fold_start = true;
    buf->rows[end_line].fold_end = true;

    /* Add fold region */
    fold_add_region(&buf->folds, start_line, end_line);

    ed_set_status_message("Fold created: lines %d-%d", start_line + 1,
                          end_line + 1);
}

/* Remove fold at line: :foldrm <line> */
void cmd_fold_rm(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("foldrm: no buffer");
        return;
    }

    if (!args || !*args) {
        ed_set_status_message("foldrm: usage: foldrm <line>");
        return;
    }

    int line = atoi(args);
    if (line <= 0) {
        ed_set_status_message("foldrm: invalid line number");
        return;
    }

    /* Convert from 1-indexed to 0-indexed */
    line--;

    if (line < 0 || line >= buf->num_rows) {
        ed_set_status_message("foldrm: line out of range");
        return;
    }

    /* Find fold at this line */
    int idx = fold_find_at_line(&buf->folds, line);
    if (idx == -1) {
        ed_set_status_message("foldrm: no fold at line %d", line + 1);
        return;
    }

    /* Clear fold markers on the rows */
    FoldRegion *region = &buf->folds.regions[idx];
    if (region->start_line >= 0 && region->start_line < buf->num_rows) {
        buf->rows[region->start_line].fold_start = false;
    }
    if (region->end_line >= 0 && region->end_line < buf->num_rows) {
        buf->rows[region->end_line].fold_end = false;
    }

    /* Remove the fold region */
    fold_remove_region(&buf->folds, idx);

    ed_set_status_message("Fold removed at line %d", line + 1);
}

/* Toggle fold at line: :foldtoggle <line> */
void cmd_fold_toggle(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("foldtoggle: no buffer");
        return;
    }

    int line = -1;
    if (args && *args) {
        line = atoi(args);
        if (line <= 0) {
            ed_set_status_message("foldtoggle: invalid line number");
            return;
        }
        line--; /* Convert to 0-indexed */
    } else {
        /* Use cursor line if no argument */
        Window *win = window_cur();
        if (!win) {
            ed_set_status_message("foldtoggle: no window");
            return;
        }
        line = win->cursor.y;
    }

    if (line < 0 || line >= buf->num_rows) {
        ed_set_status_message("foldtoggle: line out of range");
        return;
    }

    /* Toggle the fold */
    if (fold_toggle_at_line(&buf->folds, line)) {
        int idx = fold_find_at_line(&buf->folds, line);
        if (idx >= 0) {
            bool collapsed = buf->folds.regions[idx].is_collapsed;
            ed_set_status_message("Fold %s at line %d",
                                  collapsed ? "collapsed" : "expanded",
                                  line + 1);
        }
    } else {
        ed_set_status_message("foldtoggle: no fold at line %d", line + 1);
    }
}

void cmd_foldmethod(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("foldmethod: no buffer");
        return;
    }

    if (!args || !*args) {
        /* Show current fold method */
        const char *method_name = "unknown";
        switch (buf->fold_method) {
        case FOLD_METHOD_MANUAL:
            method_name = "manual";
            break;
        case FOLD_METHOD_BRACKET:
            method_name = "bracket";
            break;
        case FOLD_METHOD_INDENT:
            method_name = "indent";
            break;
        }
        ed_set_status_message("foldmethod=%s", method_name);
        return;
    }

    /* Parse the method name */
    FoldMethod new_method = FOLD_METHOD_MANUAL;
    if (strcmp(args, "manual") == 0) {
        new_method = FOLD_METHOD_MANUAL;
    } else if (strcmp(args, "bracket") == 0) {
        new_method = FOLD_METHOD_BRACKET;
    } else if (strcmp(args, "indent") == 0) {
        new_method = FOLD_METHOD_INDENT;
    } else {
        ed_set_status_message("foldmethod: unknown method '%s' (manual, bracket, indent)", args);
        return;
    }

    /* Set the method and apply it */
    buf->fold_method = new_method;
    fold_apply_method(buf, new_method);
    ed_set_status_message("foldmethod=%s", args);
}

void cmd_foldupdate(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("foldupdate: no buffer");
        return;
    }

    /* Reapply the current fold method */
    fold_apply_method(buf, buf->fold_method);

    const char *method_name = "manual";
    switch (buf->fold_method) {
    case FOLD_METHOD_MANUAL:
        method_name = "manual";
        break;
    case FOLD_METHOD_BRACKET:
        method_name = "bracket";
        break;
    case FOLD_METHOD_INDENT:
        method_name = "indent";
        break;
    }
    ed_set_status_message("Folds updated using %s method", method_name);
}
