#include "cmd_misc.h"
#include "../hed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper functions for escape sequence parsing */
static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static char *unescape_string(const char *in) {
    if (!in) {
        char *empty = strdup("");
        return empty ? empty : NULL;
    }
    size_t n = strlen(in);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    size_t oi = 0;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        if (c == '\\' && i + 1 < n) {
            char nxt = in[++i];
            switch (nxt) {
                case 'n': out[oi++] = '\n'; break;
                case 'r': out[oi++] = '\r'; break;
                case 't': out[oi++] = '\t'; break;
                case '\\': out[oi++] = '\\'; break;
                case '"': out[oi++] = '"'; break;
                case '\'': out[oi++] = '\''; break;
                case 'x': {
                    if (i + 2 < n) {
                        int h1 = hexval(in[i+1]);
                        int h2 = hexval(in[i+2]);
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
        if (wrote < 0) break;
        off += wrote;
        if (off >= (int)sizeof(msg) - 1) {
            off = (int)sizeof(msg) - 1;
            break;
        }
    }
    if (off == 0) snprintf(msg, sizeof(msg), "No commands");
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
        if (end != args && v > 0 && v < 100000) n = (int)v;
    }
    int hlen = hist_len(&E.history);
    if (n > hlen) n = hlen;
    if (n <= 0) {
        ed_set_status_message("(no history)");
        return;
    }

    char buf[256];
    int off = 0;
    for (int i = 0; i < n; i++) {
        const char *line = hist_get(&E.history, i);
        if (!line) continue;
        int wrote = snprintf(buf + off, (int)sizeof(buf) - off,
                             i == 0 ? "%s" : "\n%s", line);
        if (wrote < 0) break;
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
    const char regs_list[] = {'"','0','1','2','3','4','5','6','7','8','9',':'};

    for (size_t i = 0; i < sizeof(regs_list); i++) {
        const SizedStr *s = regs_get(regs_list[i]);
        if (!s) continue;
        int wrote = snprintf(out + off, (int)sizeof(out) - off,
                             i == 0 ? "%c %.*s" : "\n%c %.*s",
                             regs_list[i],
                             (int)(s->len > 40 ? 40 : s->len),
                             s->data ? s->data : "");
        if (wrote < 0) break;
        off += wrote;
        if (off >= (int)sizeof(out) - 1) break;
    }

    /* named registers */
    for (char c = 'a'; c <= 'z'; c++) {
        const SizedStr *s = regs_get(c);
        if (!s || s->len == 0) continue;
        int wrote = snprintf(out + off, (int)sizeof(out) - off, "\n%c %.*s",
                             c, (int)(s->len > 40 ? 40 : s->len),
                             s->data ? s->data : "");
        if (wrote < 0) break;
        off += wrote;
        if (off >= (int)sizeof(out) - 1) break;
    }
    out[off] = '\0';
    ed_set_status_message("%s", out);
}

void cmd_put(const char *args) {
    char reg = '"';
    if (args && *args) {
        while (*args == ' ') args++;
        if (*args == '\"' || *args == '@') args++;
        if (*args) reg = *args;
    }
    const SizedStr *s = regs_get(reg);
    if (!s || s->len == 0) {
        ed_set_status_message("Register %c empty", reg);
        return;
    }
    Buffer *buf = buf_cur();
    if (!buf) return;
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

void cmd_ln(const char *args) {
    (void)args;
    if (!E.show_line_numbers) {
        E.show_line_numbers = 1;
    } else {
        E.show_line_numbers = 0;
        E.relative_line_numbers = 0;
    }
    ed_set_status_message("Line numbers: %s",
        !E.show_line_numbers ? "off" :
        (E.relative_line_numbers ? "relative" : "absolute"));
}

void cmd_rln(const char *args) {
    (void)args;
    if (!E.relative_line_numbers) {
        E.relative_line_numbers = 1;
        if (!E.show_line_numbers) E.show_line_numbers = 1;
    } else {
        E.relative_line_numbers = 0;
    }
    ed_set_status_message("Relative line numbers: %s",
        E.relative_line_numbers ? "on" : "off");
}

void cmd_wrap(const char *args) {
    (void)args;
    Window *win = window_cur();
    if (!win) return;
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
    if (!b) return;
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
    if (!win || win->is_quickfix) return;
    Buffer *buf = buf_cur();
    if (!buf) return;

    if (buf->num_rows == 0) {
        win->cursor.y = 0;
        win->cursor.x = 0;
    } else {
        Row *row = (win->cursor.y < buf->num_rows) ? &buf->rows[win->cursor.y] : NULL;
        win->cursor.x = row ? (int)row->chars.len : 0;
    }
    buf_insert_newline_in(buf);
    ed_set_mode(MODE_INSERT);
}

void cmd_new_line_above(const char *args) {
    (void)args;
    Window *win = window_cur();
    if (!win || win->is_quickfix) return;
    Buffer *buf = buf_cur();
    if (!buf) return;

    win->cursor.x = 0;
    buf_insert_newline_in(buf);
    /* Cursor should now be on the new blank line above */
    ed_set_mode(MODE_INSERT);
}

void cmd_shell(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :shell <command>");
        return;
    }

    /* Run command interactively, handing over the TTY */
    int status = term_cmd_run_interactive(args);

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
    /* Run lazygit as a full-screen TUI, like fzf: temporarily leave raw mode. */
    int status = term_cmd_run_interactive("lazygit");
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
    int status = term_cmd_run_interactive("make -j4");
    if (status != 0) {
        ed_set_status_message("reload: build failed (status %d)", status);
        return;
    }

    /* Leave raw mode before replacing the process image. */
    disable_raw_mode();

    /* Restart the editor; assume we are run from repo root. */
    const char *exe = "hed";
    execl(exe, exe, (char *)NULL);

    /* If we reach here, exec failed. */
    perror("execl");
    enable_raw_mode();
    ed_set_status_message("reload: failed to exec %s", exe);
}
