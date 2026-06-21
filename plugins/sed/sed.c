#include "hed.h"
#include "sed.h"

EdError sed_apply_to_buffer(Buffer *buf, const char *sed_expr) {
    /* 1. Validate inputs */
    if (!PTR_VALID(buf)) {
        return ED_ERR_INVALID_ARG;
    }

    if (!sed_expr || !*sed_expr) {
        ed_set_status_message("sed: empty expression");
        return ED_ERR_INVALID_ARG;
    }

    if (buf->readonly) {
        ed_set_status_message("Buffer is read-only");
        return ED_ERR_BUFFER_READONLY;
    }

    /* Save cursor position for restoration */
    Window *win = window_cur();
    int saved_cx = win ? win->cursor.x : 0;
    int saved_cy = win ? win->cursor.y : 0;

    /* 2. Serialize buffer to string */
    size_t input_len = 0;
    char *input = buf_to_text(buf, &input_len);
    if (!input) {
        ed_set_status_message("sed: memory allocation failed");
        return ED_ERR_NOMEM;
    }

    /* 3. Build `printf '%s' <content> | sed <expr> 2>&1`, shell-quoting
     *    both arguments into a growable StrBuf (no fixed bound). */
    StrBuf cmd = strbuf_new();
    strbuf_append(&cmd, "printf '%s' ", 12);
    strbuf_append_shell_quoted(&cmd, input);
    strbuf_append(&cmd, " | sed ", 7);
    strbuf_append_shell_quoted(&cmd, sed_expr);
    strbuf_append(&cmd, " 2>&1", 5);
    free(input); /* Don't need original input anymore */

    char *cmd_str = strbuf_to_cstr(&cmd);
    strbuf_free(&cmd);
    if (!cmd_str) {
        ed_set_status_message("sed: memory allocation failed");
        return ED_ERR_NOMEM;
    }

    /* 4. Execute sed and capture output */
    char **output_lines = NULL;
    int output_count = 0;
    int success = term_cmd_run(cmd_str, &output_lines, &output_count);

    free(cmd_str);

    if (!success) {
        ed_set_status_message("sed: execution failed (check sed syntax)");
        return ED_ERR_SYSTEM;
    }

    /* 6. Clear old buffer content */
    for (int i = 0; i < buf->num_rows; i++) {
        row_free(&buf->rows[i]);
    }
    free(buf->rows);
    buf->rows = NULL;
    buf->num_rows = 0;

    /* 7. Insert new content from sed output */
    if (output_count == 0) {
        /* Sed deleted everything - create one empty row */
        buf_row_insert_in(buf, 0, "", 0);
    } else {
        for (int i = 0; i < output_count; i++) {
            buf_row_insert_in(buf, i, output_lines[i],
                            strlen(output_lines[i]));
        }
    }

    term_cmd_free(output_lines, output_count);

    /* 8. Update buffer state */
    buf->dirty++;

    /* Restore cursor position (clamp to valid range) */
    if (win) {
        /* Clamp Y to valid range */
        if (saved_cy >= buf->num_rows) {
            saved_cy = buf->num_rows - 1;
        }
        if (saved_cy < 0) {
            saved_cy = 0;
        }
        win->cursor.y = saved_cy;

        /* Clamp X to valid range for current line */
        if (saved_cy < buf->num_rows) {
            int max_x = (int)buf->rows[saved_cy].chars.len;
            if (saved_cx > max_x) {
                saved_cx = max_x;
            }
        }
        if (saved_cx < 0) {
            saved_cx = 0;
        }
        win->cursor.x = saved_cx;
    }

    /* 9. Show success message */
    ed_set_status_message("sed: %d line(s)", buf->num_rows);

    return ED_OK;
}
