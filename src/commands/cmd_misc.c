#include "commands/cmd_misc.h"
#include "editor.h"
#include "commands.h"
#include "buf/buf_helpers.h"
#include "utils/yank.h"
#include "terminal.h"
#include "lib/log.h"
#include "ui/winmodal.h"
#include <unistd.h>
#include "macros.h"
#include "registers.h"
#include "utils/fold.h"
#include "utils/fzf.h"
#include "fold_methods/fold_methods.h"
#include "keybinds.h"
#include "commands/cmd_util.h"
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
    /* Build printf list with command\tdesc lines for fzf */
    char pipebuf[8192];
    size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "printf '%%s\t%%s\\n' ");

    for (ptrdiff_t i = 0; i < arrlen(commands); i++) {
        const char *nm = commands[i].name ? commands[i].name : "";
        const char *ds = commands[i].desc ? commands[i].desc : "";
        char en[256], ed[512];
        shell_escape_single(nm, en, sizeof(en));
        shell_escape_single(ds, ed, sizeof(ed));

        size_t need = strlen(en) + 1 + strlen(ed) + 1;
        if (off + need + 4 >= sizeof(pipebuf))
            break;

        memcpy(pipebuf + off, en, strlen(en));
        off += strlen(en);
        pipebuf[off++] = ' ';
        memcpy(pipebuf + off, ed, strlen(ed));
        off += strlen(ed);
        pipebuf[off++] = ' ';
    }
    pipebuf[off] = '\0';

    const char *fzf_opts = "--delimiter '\t'";
    char **sel = NULL;
    int cnt = 0;
    if (!fzf_run_opts(pipebuf, fzf_opts, 0, &sel, &cnt) || cnt == 0) {
        ed_set_status_message("commands: canceled");
        fzf_free(sel, cnt);
        return;
    }

    fzf_free(sel, cnt);
    ed_set_status_message("commands: %td total", arrlen(commands));
}

void cmd_list_keybinds(const char *args) {
    (void)args;
    /* Build printf list with sequence\tdesc lines for fzf */
    char pipebuf[16384];
    size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "printf '%%s\t%%s\\n' ");

    int count = keybind_get_count();
    for (int i = 0; i < count; i++) {
        const char *sequence = NULL;
        const char *desc = NULL;
        int mode = 0;

        if (!keybind_get_at(i, &sequence, &desc, &mode))
            continue;

        /* Add mode prefix */
        char display[256];
        const char *mode_prefix = "";
        if (mode == MODE_NORMAL) mode_prefix = "[N] ";
        else if (mode == MODE_INSERT) mode_prefix = "[I] ";
        else if (mode == MODE_VISUAL) mode_prefix = "[V] ";
        else if (mode == MODE_VISUAL_BLOCK) mode_prefix = "[VB] ";
        else if (mode == MODE_COMMAND) mode_prefix = "[C] ";

        snprintf(display, sizeof(display), "%s%s", mode_prefix, sequence ? sequence : "");

        char es[512], ed[512];
        shell_escape_single(display, es, sizeof(es));
        shell_escape_single(desc ? desc : "", ed, sizeof(ed));

        size_t need = strlen(es) + 1 + strlen(ed) + 1;
        if (off + need + 4 >= sizeof(pipebuf))
            break;

        memcpy(pipebuf + off, es, strlen(es));
        off += strlen(es);
        pipebuf[off++] = ' ';
        memcpy(pipebuf + off, ed, strlen(ed));
        off += strlen(ed);
        pipebuf[off++] = ' ';
    }
    pipebuf[off] = '\0';

    const char *fzf_opts = "--delimiter '\t'";
    char **sel = NULL;
    int cnt = 0;
    if (!fzf_run_opts(pipebuf, fzf_opts, 0, &sel, &cnt) || cnt == 0) {
        ed_set_status_message("keybinds: canceled");
        fzf_free(sel, cnt);
        return;
    }

    fzf_free(sel, cnt);
    ed_set_status_message("keybinds: %d total", count);
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
    /* Paste from the specified register */
    paste_from_register(buf, reg, true);
}

void cmd_undo(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (!undo_apply(buf))
        ed_set_status_message("Already at oldest change");
}

void cmd_redo(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) return;
    if (!redo_apply(buf))
        ed_set_status_message("Already at newest change");
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
    /* Get numeric count before reading the register key */
    int count = keybind_get_and_clear_pending_count();

    /* Read next key to determine which register */
    int key = ed_read_key();

    /* Handle @@ - replay last macro */
    if (key == '@') {
        for (int i = 0; i < count; i++) {
            macro_play_last();
        }
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

    /* Play the macro count times */
    for (int i = 0; i < count; i++) {
        macro_play((char)key);
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

void cmd_buf_refresh(const char* args){
	(void)args;
	Buffer *buf=buf_cur();
	buf_reload(buf);
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
    win->cursor.y--;
    /* Cursor should now be on the new blank line above */
    ed_set_mode(MODE_INSERT);
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

/* Build a comma-separated list of registered methods for error messages.
 * Returns a pointer into a static buffer; not thread-safe (the editor is
 * single-threaded). */
static const char *foldmethod_list_str(void) {
    static char buf[256];
    buf[0] = '\0';
    size_t off = 0;
    int n = fold_method_count();
    for (int i = 0; i < n; i++) {
        const char *name = fold_method_name_at(i);
        if (!name) continue;
        int written = snprintf(buf + off, sizeof(buf) - off,
                               "%s%s", off ? ", " : "", name);
        if (written < 0 || (size_t)written >= sizeof(buf) - off)
            break;
        off += (size_t)written;
    }
    return buf;
}

void cmd_foldmethod(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("foldmethod: no buffer");
        return;
    }

    if (!args || !*args) {
        ed_set_status_message("foldmethod=%s",
                              buf->fold_method ? buf->fold_method : "(none)");
        return;
    }

    if (!fold_method_lookup(args)) {
        ed_set_status_message("foldmethod: unknown method '%s' (%s)",
                              args, foldmethod_list_str());
        return;
    }

    char *copy = strdup(args);
    if (!copy) {
        ed_set_status_message("foldmethod: out of memory");
        return;
    }
    free(buf->fold_method);
    buf->fold_method = copy;
    fold_apply_method(buf, args);
    ed_set_status_message("foldmethod=%s", args);
}

void cmd_foldupdate(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (!buf) {
        ed_set_status_message("foldupdate: no buffer");
        return;
    }
    fold_apply_method(buf, buf->fold_method);
    ed_set_status_message("Folds updated using %s method",
                          buf->fold_method ? buf->fold_method : "(none)");
}
