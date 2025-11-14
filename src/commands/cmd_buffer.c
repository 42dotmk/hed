#include "cmd_buffer.h"
#include "cmd_util.h"
#include "../hed.h"
#include "fzf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    if (E.buffers.len == 0) {
        ed_set_status_message("No buffers");
        return;
    }

    /* Build a shell command that outputs all buffer entries */
    char cmd[8192];
    int off = 0;

    off += snprintf(cmd + off, sizeof(cmd) - off, "printf '");

    for (int i = 0; i < (int)E.buffers.len && off < (int)sizeof(cmd) - 200; i++) {
        Buffer *buf = &E.buffers.data[i];
        const char *name = buf->title;
        char marker = (i == E.current_buffer) ? '*' : ' ';
        char dirty_marker = buf->dirty ? '+' : ' ';

        /* Format: [N]* filename + */
        /* We'll prefix with the index for easy parsing */
        char entry[512];
        snprintf(entry, sizeof(entry), "[%d]%c %s %c",
                 i + 1, marker, name, dirty_marker);

        /* Escape single quotes in the entry */
        const char *p = entry;
        while (*p && off < (int)sizeof(cmd) - 100) {
            if (*p == '\'') {
                off += snprintf(cmd + off, sizeof(cmd) - off, "'\\''");
            } else {
                cmd[off++] = *p;
            }
            p++;
        }

        /* Add newline separator */
        if (i < (int)E.buffers.len - 1) {
            cmd[off++] = '\\';
            cmd[off++] = 'n';
        }
    }

    off += snprintf(cmd + off, sizeof(cmd) - off, "'");
    cmd[off] = '\0';

    char **sel = NULL;
    int cnt = 0;

    /* For buffers with filenames, show preview; for unnamed buffers, no preview */
    const char *fzf_opts = "--preview 'f=$(echo {} | sed \"s/^\\[[0-9]\\+\\][* ] \\(.*\\) [+ ]$/\\1/\"); [ -f \"$f\" ] && (command -v bat >/dev/null 2>&1 && bat --style=plain --color=always --line-range :200 \"$f\" || sed -n \"1,200p\" \"$f\" 2>/dev/null) || echo \"No preview available\"' --preview-window right,60%,wrap";

    if (fzf_run_opts(cmd, fzf_opts, 0, &sel, &cnt) && cnt > 0 && sel[0] && sel[0][0]) {
        const char *picked = sel[0];

        /* Parse the buffer index from "[N]" at the start */
        int buffer_idx = -1;
        if (picked[0] == '[') {
            buffer_idx = atoi(picked + 1) - 1;  /* Convert 1-indexed to 0-indexed */
        }

        if (buffer_idx >= 0 && buffer_idx < (int)E.buffers.len) {
            buf_switch(buffer_idx);
        } else {
            ed_set_status_message("Invalid buffer selection");
        }
    } else {
        ed_set_status_message("no selection");
    }

    fzf_free(sel, cnt);
}

void cmd_buffer_switch(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("Usage: :b <buffer_number>");
        return;
    }
    int buf_idx = atoi(args) - 1;
    EdError err = buf_switch(buf_idx);
    if (err != ED_OK) {
        ed_set_status_message("Failed to switch: %s", ed_error_string(err));
    } else {
        Buffer *buf = buf_cur();
        ed_set_status_message("Switched to buffer %d: %s", buf_idx + 1, buf->title);
    }
}

void cmd_buffer_delete(const char *args) {
    int buf_idx;
    if (!args || !*args) {
        buf_idx = E.current_buffer;
    } else {
        buf_idx = atoi(args) - 1;
    }

    EdError err = buf_close(buf_idx);
    if (err != ED_OK) {
        switch (err) {
            case ED_ERR_INVALID_INDEX:
                ed_set_status_message("Invalid buffer index");
                break;
            case ED_ERR_BUFFER_READONLY:
                ed_set_status_message("Cannot close *messages buffer");
                break;
            case ED_ERR_BUFFER_DIRTY:
                ed_set_status_message("Buffer has unsaved changes! Save first or use :bd!");
                break;
            default:
                ed_set_status_message("Error closing buffer: %s", ed_error_string(err));
                break;
        }
    } else {
        ed_set_status_message("Buffer closed");
    }
}

void cmd_buffers(const char *args) {
    (void)args;
    if (E.buffers.len <= 0) { ed_set_status_message("no buffers"); return; }
    /* Build list: index<TAB>name<TAB>modified<TAB>lines */
    char pipebuf[8192]; size_t off = 0;
    off += snprintf(pipebuf + off, sizeof(pipebuf) - off, "printf '%%s\t%%s\t%%s\t%%s\\n' ");
    for (int i = 0; i < (int)E.buffers.len; i++) {
        char idxs[16]; snprintf(idxs, sizeof(idxs), "%d", i + 1);
        const char *nm = E.buffers.data[i].title;
        const char *mod = (E.buffers.data[i].dirty ? "*" : "-");
        char lines[32]; snprintf(lines, sizeof(lines), "%d", E.buffers.data[i].num_rows);
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
    if (idx < 1 || idx > (int)E.buffers.len) { fzf_free(sel, cnt); ed_set_status_message("buffers: invalid"); return; }
    buf_switch(idx - 1);
    ed_set_status_message("buffer %d", idx);
    fzf_free(sel, cnt);
}
