#include "hed.h"
#include <stdlib.h>
#include <limits.h>
#include "fzf.h"

#define MAX_COMMANDS 256

/* Command entry */
typedef struct {
    char *name;
    CommandCallback callback;
    char *desc; /* optional description */
} Command;

/* Global command storage */
static Command commands[MAX_COMMANDS];
static int command_count = 0;

/*** Default command callbacks ***/

void cmd_quit(const char *args) {
    (void)args;
    Buffer *buf = buf_cur();
    if (buf && buf->dirty) {
        ed_set_status_message("File has unsaved changes! Use :q! to force quit");
    } else {
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
    }
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
        if (off >= (int)sizeof(msg) - 1) { off = (int)sizeof(msg) - 1; break; }
    }
    if (off == 0) snprintf(msg, sizeof(msg), "No commands");
    ed_set_status_message("%s", msg);
}
void cmd_quit_force(const char *args) {
    (void)args;
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
}

void cmd_write(const char *args) {
    (void)args;
    buf_save();
}

void cmd_write_quit(const char *args) {
    (void)args;
    buf_save();
    Buffer *buf = buf_cur();
    if (buf && !buf->dirty) {
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
    }
}

void cmd_buffer_next(const char *args) {
    (void)args;
    buf_next();
}

void cmd_buffer_prev(const char *args) {
    (void)args;
    buf_prev();
}

void cmd_buffer_list(const char *args) {
    (void)args;
    buf_list();
}

void cmd_buffer_switch(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :b <buffer_number>");
        return;
    }
    int buf_idx = atoi(args) - 1;
    buf_switch(buf_idx);
}

void cmd_buffer_delete(const char *args) {
    if (!args || !*args) {
        buf_close(E.current_buffer);
    } else {
        int buf_idx = atoi(args) - 1;
        buf_close(buf_idx);
    }
}

void cmd_edit(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :e <filename>");
        return;
    }
    buf_open((char *)args);
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static char *unescape_string(const char *in) {
    if (!in) return strdup("");
    size_t n = strlen(in);
    char *out = malloc(n + 1);
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
                    /* If invalid, keep literal 'x' */
                    out[oi++] = 'x';
                    break;
                }
                default:
                    /* Unknown escape: keep the char as-is */
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

void cmd_echo(const char *args) {
    /* Print the provided text to the status bar, supporting escapes like \n */
    char *msg = unescape_string(args);
    ed_set_status_message("%s", msg);
    free(msg);
}

void cmd_history(const char *args) {
    int n = 20; /* default */
    if (args && *args) {
        char *end = NULL;
        long v = strtol(args, &end, 10);
        if (end != args && v > 0 && v < 100000) n = (int)v;
    }
    int hlen = hist_len(&E.history);
    if (n > hlen) n = hlen;
    if (n <= 0) { ed_set_status_message("(no history)"); return; }

    /* Build a multi-line string up to the status buffer size. */
    char buf[256];
    int off = 0;
    for (int i = 0; i < n; i++) {
        const char *line = hist_get(&E.history, i);
        if (!line) continue;
        int wrote = snprintf(buf + off, (int)sizeof(buf) - off, i == 0 ? "%s" : "\n%s", line);
        if (wrote < 0) break;
        off += wrote;
        if (off >= (int)sizeof(buf) - 1) { off = (int)sizeof(buf) - 1; break; }
    }
    buf[off] = '\0';
    ed_set_status_message("%s", buf);
}

void cmd_registers(const char *args) {
    (void)args;
    /* Show a subset: ", 0, 1..9, a..z (non-empty), : */
    char out[256];
    int off = 0;
    const char regs_list[] = {'"','0','1','2','3','4','5','6','7','8','9',':'};
    for (size_t i = 0; i < sizeof(regs_list); i++) {
        const SizedStr *s = regs_get(regs_list[i]);
        if (!s) continue;
        int wrote = snprintf(out + off, (int)sizeof(out) - off, i == 0 ? "%c %.*s" : "\n%c %.*s",
                             regs_list[i], (int)(s->len > 40 ? 40 : s->len), s->data ? s->data : "");
        if (wrote < 0) break; off += wrote; if (off >= (int)sizeof(out) - 1) break;
    }
    /* named registers */
    for (char c = 'a'; c <= 'z'; c++) {
        const SizedStr *s = regs_get(c);
        if (!s || s->len == 0) continue;
        int wrote = snprintf(out + off, (int)sizeof(out) - off, "\n%c %.*s",
                             c, (int)(s->len > 40 ? 40 : s->len), s->data ? s->data : "");
        if (wrote < 0) break; off += wrote; if (off >= (int)sizeof(out) - 1) break;
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
    if (!s || s->len == 0) { ed_set_status_message("Register %c empty", reg); return; }
    Buffer *buf = buf_cur(); if (!buf) return;
    int at = buf->cursor_y < buf->num_rows ? buf->cursor_y + 1 : buf->num_rows;
    buf_row_insert(at, s->data, s->len);
    buf->cursor_y = at;
    buf->cursor_x = 0;
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
        !E.show_line_numbers ? "off" : (E.relative_line_numbers ? "relative" : "absolute"));
}

void cmd_rln(const char *args) {
    (void)args;
    if (!E.relative_line_numbers) {
        /* Turn on relative; ensure numbers are visible */
        E.relative_line_numbers = 1;
        if (!E.show_line_numbers) E.show_line_numbers = 1;
    } else {
        /* Turn off relative; keep absolute if enabled */
        E.relative_line_numbers = 0;
    }
    ed_set_status_message("Relative line numbers: %s",
        E.relative_line_numbers ? "on" : "off");
}

/* --- Quickfix commands --- */
static int parse_int_default(const char *s, int def) {
    if (!s || !*s) return def;
    char *end = NULL; long v = strtol(s, &end, 10);
    if (end == s) return def; if (v < 0) v = 0; if (v > 100000) v = 100000;
    return (int)v;
}

void cmd_copen(const char *args) {
    int h = parse_int_default(args, 8);
    if (h < 2) h = 2;
    qf_open(&E.qf, h);
}

void cmd_cclose(const char *args) {
    (void)args; qf_close(&E.qf);
}

void cmd_ctoggle(const char *args) {
    int h = parse_int_default(args, E.qf.height > 0 ? E.qf.height : 8);
    qf_toggle(&E.qf, h);
}

void cmd_cclear(const char *args) {
    (void)args; qf_clear(&E.qf);
}

static void cadd_current(const char *msg) {
    Buffer *b = buf_cur();
    const char *fn = b && b->filename ? b->filename : NULL;
    int line = b ? b->cursor_y + 1 : 1;
    int col  = b ? b->cursor_x + 1 : 1;
    qf_add(&E.qf, fn, line, col, msg ? msg : "");
}

void cmd_cadd(const char *args) {
    if (!args || !*args) { cadd_current(""); return; }
    /* Try to parse form: file:line[:col]: message */
    const char *p1 = strchr(args, ':');
    if (!p1) { cadd_current(args); return; }
    const char *p2 = strchr(p1 + 1, ':');
    const char *p3 = p2 ? strchr(p2 + 1, ':') : NULL;
    char file[256] = {0}; int line = 0, col = 0; const char *msg = NULL;
    if (p3) {
        /* file:line:col: msg */
        size_t flen = (size_t)(p1 - args); if (flen >= sizeof(file)) flen = sizeof(file) - 1;
        memcpy(file, args, flen); file[flen] = '\0';
        line = atoi(p1 + 1);
        col = atoi(p2 + 1);
        msg = p3 + 1;
        qf_add(&E.qf, file, line, col, msg);
    } else if (p2) {
        /* file:line: msg */
        size_t flen = (size_t)(p1 - args); if (flen >= sizeof(file)) flen = sizeof(file) - 1;
        memcpy(file, args, flen); file[flen] = '\0';
        line = atoi(p1 + 1);
        msg = p2 + 1;
        qf_add(&E.qf, file, line, 1, msg);
    } else {
        cadd_current(args);
    }
}

void cmd_cnext(const char *args) {
    (void)args;
    if (E.qf.len == 0) { ed_set_status_message("Quickfix empty"); return; }
    qf_move(&E.qf, 1);
    qf_open_selected(&E.qf);
}

void cmd_cprev(const char *args) {
    (void)args;
    if (E.qf.len == 0) { ed_set_status_message("Quickfix empty"); return; }
    qf_move(&E.qf, -1);
    qf_open_selected(&E.qf);
}

void cmd_copenidx(const char *args) {
    int idx = parse_int_default(args, 1);
    if (idx <= 0) idx = 1;
    if (idx > E.qf.len) idx = E.qf.len;
    qf_open_idx(&E.qf, idx - 1);
}

/* Escape single quotes for shell-safe printf (prototype) */
static void shell_escape_single(const char *in, char *out, size_t outsz);

void cmd_buffers(const char *args) {
    (void)args;
    if (E.num_buffers <= 0) { ed_set_status_message("no buffers"); return; }
    /* Build list: index<TAB>name<TAB>modified<TAB>lines */
    char pipebuf[8192]; size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "printf '%%s\t%%s\t%%s\t%%s\\n' ");
    for (int i = 0; i < E.num_buffers; i++) {
        char idxs[16]; snprintf(idxs, sizeof(idxs), "%d", i + 1);
        const char *nm = E.buffers[i].filename ? E.buffers[i].filename : "[No Name]";
        const char *mod = (E.buffers[i].dirty ? "*" : "-");
        char lines[32]; snprintf(lines, sizeof(lines), "%d", E.buffers[i].num_rows);
        char eidx[32], enam[512], emod[8], elines[32];
        shell_escape_single(idxs, eidx, sizeof(eidx));
        shell_escape_single(nm, enam, sizeof(enam));
        shell_escape_single(mod, emod, sizeof(emod));
        shell_escape_single(lines, elines, sizeof(elines));
        size_t need = strlen(eidx) + 1 + strlen(enam) + 1 + strlen(emod) + 1 + strlen(elines) + 1;
        if (off + need + 4 >= sizeof(pipebuf)) break;
        memcpy(pipebuf + off, eidx, strlen(eidx)); off += strlen(eidx);
        pipebuf[off++] = ' ';
        memcpy(pipebuf + off, enam, strlen(enam)); off += strlen(enam);
        pipebuf[off++] = ' ';
        memcpy(pipebuf + off, emod, strlen(emod)); off += strlen(emod);
        pipebuf[off++] = ' ';
        memcpy(pipebuf + off, elines, strlen(elines)); off += strlen(elines);
        pipebuf[off++] = ' ';
    }
    pipebuf[off] = '\0';

    const char *fzf_opts = "--delimiter '\\t' --with-nth 2 "
                           "--preview 'printf \"buf:%s modified:%s lines:%s\\n\\n\" {1} {3} {4}; "
                           "command -v bat >/dev/null 2>&1 && bat --style=plain --color=always --line-range :200 {2} || sed -n \"1,200p\" {2} 2>/dev/null' "
                           "--preview-window right,60%,wrap";
    char **sel = NULL; int cnt = 0;
    if (!fzf_run_opts(pipebuf, fzf_opts, 0, &sel, &cnt) || cnt == 0) {
        fzf_free(sel, cnt); ed_set_status_message("buffers: canceled"); return; }
    /* Parse selection: idx<TAB>name */
    char *picked = sel[0];
    char *tab = strchr(picked, '\t');
    if (tab) *tab = '\0';
    int idx = atoi(picked);
    if (idx < 1 || idx > E.num_buffers) { fzf_free(sel, cnt); ed_set_status_message("buffers: invalid"); return; }
    buf_switch(idx - 1);
    ed_set_status_message("buffer %d", idx);
    fzf_free(sel, cnt);
}

/* Command picker using fzf: :c → choose a command name, prefill and wait */
/* Escape single quotes for shell-safe printf */
static void shell_escape_single(const char *in, char *out, size_t outsz) {
    size_t o = 0;
    if (o < outsz) out[o++] = '\'';
    for (const char *p = in; *p && o + 4 < outsz; p++) {
        if (*p == '\'') { out[o++] = '\''; out[o++] = '\\'; out[o++] = '\''; out[o++] = '\''; }
        else out[o++] = *p;
    }
    if (o < outsz) out[o++] = '\'';
    if (o < outsz) out[o] = '\0'; else out[outsz - 1] = '\0';
}

void cmd_cpick(const char *args) {
    (void)args;
    /* Build printf list with name\tdesc lines for fzf preview */
    char pipebuf[8192]; size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "printf '%%s\t%%s\\n' ");
    for (int i = 0; i < command_count; i++) {
        const char *nm = commands[i].name ? commands[i].name : "";
        const char *ds = commands[i].desc ? commands[i].desc : "";
        char en[256], ed[512];
        shell_escape_single(nm, en, sizeof(en));
        shell_escape_single(ds, ed, sizeof(ed));
        size_t need = strlen(en) + 1 + strlen(ed) + 1;
        if (off + need + 4 >= sizeof(pipebuf)) break;
        memcpy(pipebuf + off, en, strlen(en)); off += strlen(en);
        pipebuf[off++] = ' ';
        memcpy(pipebuf + off, ed, strlen(ed)); off += strlen(ed);
        pipebuf[off++] = ' ';
    }
    pipebuf[off] = '\0';

    const char *fzf_opts = "--delimiter '\t' --with-nth 1 --preview 'echo {2}' --preview-window right,60%,wrap";
    char **sel = NULL; int cnt = 0;
    if (!fzf_run_opts(pipebuf, fzf_opts, 0, &sel, &cnt) || cnt == 0) {
        ed_set_status_message("c: canceled");
        fzf_free(sel, cnt); return;
    }
    /* Parse the picked line: name<TAB>desc */
    char *picked = sel[0];
    char *tab = strchr(picked, '\t');
    if (tab) *tab = '\0';

    /* Pre-fill command line and stay in command mode */
    ed_set_mode(MODE_COMMAND);
    E.command_len = 0;
    size_t ll = strlen(picked);
    size_t maxcopy = sizeof(E.command_buf) - 2;
    if (ll > maxcopy) ll = maxcopy;
    memcpy(E.command_buf, picked, ll); E.command_len = (int)ll;
    E.command_buf[E.command_len++] = ' ';
    E.command_buf[E.command_len] = '\0';
    ed_set_status_message(":%s", E.command_buf);
    E.stay_in_command = 1;
    fzf_free(sel, cnt);
}

/* ---- ripgrep integration: :rg {pattern} ---- */
void cmd_rg(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :rg <pattern>");
        return;
    }
    /* Build command: rg in current dir with vimgrep-like output */
    char cmd[1024];
    /* You can add flags like --hidden or respect .gitignore as needed */
    snprintf(cmd, sizeof(cmd), "rg --vimgrep --no-heading --color=never -n --column -- %s 2>/dev/null", args);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        ed_set_status_message("Failed to run rg");
        return;
    }

    qf_clear(&E.qf);
    char line[1024];
    int added = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* Expect: file:line:col:match */
        char *p1 = strchr(line, ':'); if (!p1) continue; *p1 = '\0';
        char *file = line;
        char *p2 = strchr(p1 + 1, ':'); if (!p2) continue; *p2 = '\0';
        int lno = atoi(p1 + 1);
        char *p3 = strchr(p2 + 1, ':'); if (!p3) continue; *p3 = '\0';
        int col = atoi(p2 + 1);
        char *text = p3 + 1;
        /* strip trailing newline */
        size_t tl = strlen(text); if (tl && (text[tl - 1] == '\n' || text[tl - 1] == '\r')) text[--tl] = '\0';
        qf_add(&E.qf, file, lno, col, text);
        added++;
        if (E.qf.height <= 0) E.qf.height = 8;
    }
    pclose(fp);

    if (added > 0) {
        qf_open(&E.qf, E.qf.height > 0 ? E.qf.height : 8);
        ed_set_status_message("rg: %d matches", added);
    } else {
        ed_set_status_message("rg: no matches");
    }
}

/* ---- fzf integration: :fzf (interactive, files only) ----
 * Presents a file list via fzf for interactive multi-select, then populates quickfix
 * with the chosen files (no in-file text search here).
 */
void cmd_fzf(const char *args) {
    (void)args;
    qf_clear(&E.qf);
    char **sel = NULL; int cnt = 0;
    const char *find_files_cmd = "(command -v rg >/dev/null 2>&1 && rg --files || find . -type f -print)";
    const char *fzf_opts = "--preview 'command -v bat >/dev/null 2>&1 && bat --style=plain --color=always --line-range :200 {} || sed -n \"1,200p\" {} 2>/dev/null' --preview-window right,60%,wrap";
    if (fzf_run_opts(find_files_cmd, fzf_opts, 0, &sel, &cnt) && cnt > 0 && sel[0] && sel[0][0]) {
        const char *picked = sel[0];
        /* If buffer for this file exists, switch and reload; else open */
        int found = -1;
        for (int i = 0; i < E.num_buffers; i++) {
            if (E.buffers[i].filename && strcmp(E.buffers[i].filename, picked) == 0) { found = i; break; }
        }
        if (found >= 0) {
            buf_switch(found);
            buf_reload_current();
        } else {
            buf_open((char *)picked);
        }
        ed_set_status_message("opened: %s", picked);
    } else {
        ed_set_status_message("fzf: no selection");
    }
    fzf_free(sel, cnt);
}

/* ---- shell → quickfix: :shq <command>
 * Runs a shell command, captures stdout lines, and populates quickfix.
 * If a line matches file:line:col:text, it parses and makes it jumpable; otherwise
 * stores the text only (no jump target).
 */
void cmd_shq(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :shq <command>");
        return;
    }
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s 2>/dev/null", args);

    /* No need to leave raw mode for non-interactive commands */
    FILE *fp = popen(cmd, "r");
    if (!fp) { ed_set_status_message("shq: failed to run"); return; }

    qf_clear(&E.qf);
    char line[2048];
    int added = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* Try parse as file:line:col:text */
        char tmp[2048]; snprintf(tmp, sizeof(tmp), "%s", line);
        char *p1 = strchr(tmp, ':');
        if (p1) {
            *p1 = '\0';
            char *file = tmp;
            char *p2 = strchr(p1 + 1, ':');
            if (p2) {
                *p2 = '\0';
                int lno = atoi(p1 + 1);
                char *p3 = strchr(p2 + 1, ':');
                int col = 1; char *text = NULL;
                if (p3) { *p3 = '\0'; col = atoi(p2 + 1); text = p3 + 1; }
                else { col = atoi(p2 + 1); text = ""; }
                size_t tl = strlen(text); if (tl && (text[tl - 1] == '\n' || text[tl - 1] == '\r')) text[--tl] = '\0';
                qf_add(&E.qf, file, lno, col, text);
                added++;
                continue;
            }
        }
        /* Plain line -> text-only quickfix item */
        size_t ll = strlen(line); if (ll && (line[ll - 1] == '\n' || line[ll - 1] == '\r')) line[--ll] = '\0';
        qf_add(&E.qf, NULL, 0, 0, line);
        added++;
    }
    pclose(fp);

    if (added > 0) {
        qf_open(&E.qf, E.qf.height > 0 ? E.qf.height : 8);
        ed_set_status_message("shq: %d line(s)", added);
    } else {
        ed_set_status_message("shq: no output");
    }
}

/* Change current working directory: :cd [path]
 * - With no args, prints current working directory.
 * - Supports ~ expansion.
 */
void cmd_cd(const char *args) {
    char cwd[PATH_MAX];
    if (!args || !*args) {
        if (getcwd(cwd, sizeof(cwd))) {
            ed_set_status_message("cwd: %s", cwd);
        } else {
            ed_set_status_message("cwd: (unknown)");
        }
        return;
    }

    while (*args == ' ' || *args == '\t') args++;
    char path[PATH_MAX];
    const char *home = getenv("HOME");
    if (*args == '~' && home && (args[1] == '/' || args[1] == '\0')) {
        snprintf(path, sizeof(path), "%s/%s", home, args[1] ? args + 2 - 1 : "");
    } else {
        snprintf(path, sizeof(path), "%s", args);
    }

    if (chdir(path) == 0) {
        if (getcwd(cwd, sizeof(cwd))) {
            ed_set_status_message("cd: %s", cwd);
        } else {
            ed_set_status_message("cd: ok");
        }
    } else {
        ed_set_status_message("cd: %s", strerror(errno));
    }
}

/* Initialize command system */
void command_init(void) {
    command_count = 0;
    user_commands_init();
}

/* Register a command */
void command_register(const char *name, CommandCallback callback, const char *desc) {
    if (command_count >= MAX_COMMANDS) return;
    commands[command_count].name = strdup(name);
    commands[command_count].callback = callback;
    commands[command_count].desc = desc ? strdup(desc) : NULL;
    command_count++;
}

/* Execute a command by name */
int command_execute(const char *name, const char *args) {
    for (int i = 0; i < command_count; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            commands[i].callback(args);
            return 1;
        }
    }
    return 0;
}

int command_invoke(const char *name, const char *args) {
    return command_execute(name, args);
}
