#include "sed.h"
#include "../hed.h"
#include "../buf/row.h"
#include "../commands/cmd_util.h"
#include "term_cmd.h"
#include <stdlib.h>
#include <string.h>

/* Forward declarations of internal buffer functions */
void buf_row_insert_in(Buffer *buf, int at, const char *s, size_t len);

/* Serialize buffer rows to a single string with newlines */
static char *buf_rows_to_string(Buffer *buf, size_t *out_len) {
    if (!buf || !out_len) {
        return NULL;
    }

    *out_len = 0;

    /* Calculate total size needed */
    size_t totlen = 0;
    for (int j = 0; j < buf->num_rows; j++) {
        totlen += buf->rows[j].chars.len + 1; /* +1 for newline */
    }

    /* Allocate buffer */
    char *buffer = malloc(totlen + 1); /* +1 for null terminator */
    if (!buffer) {
        return NULL;
    }

    /* Copy each row with newline */
    char *p = buffer;
    for (int j = 0; j < buf->num_rows; j++) {
        memcpy(p, buf->rows[j].chars.data, buf->rows[j].chars.len);
        p += buf->rows[j].chars.len;
        *p++ = '\n';
    }
    *p = '\0';

    *out_len = totlen;
    return buffer;
}

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
    int saved_cx = win ? buf->cursor.x : 0;
    int saved_cy = win ? buf->cursor.y : 0;

    /* 2. Serialize buffer to string */
    size_t input_len = 0;
    char *input = buf_rows_to_string(buf, &input_len);
    if (!input) {
        ed_set_status_message("sed: memory allocation failed");
        return ED_ERR_NOMEM;
    }

    /* 3. Escape buffer content and sed expression for safe shell execution */
    char escaped_input[input_len * 4 + 128]; /* Worst case: every char needs escaping */
    char escaped_expr[4096];

    shell_escape_single(input, escaped_input, sizeof(escaped_input));
    shell_escape_single(sed_expr, escaped_expr, sizeof(escaped_expr));

    /* 4. Build sed command: printf <content> | sed <expression> */
    size_t cmd_size = strlen(escaped_input) + strlen(escaped_expr) + 128;
    char *cmd = malloc(cmd_size);
    if (!cmd) {
        free(input);
        ed_set_status_message("sed: memory allocation failed");
        return ED_ERR_NOMEM;
    }

    snprintf(cmd, cmd_size, "printf '%%s' %s | sed %s 2>&1",
             escaped_input, escaped_expr);

    free(input); /* Don't need original input anymore */

    /* 5. Execute sed and capture output */
    char **output_lines = NULL;
    int output_count = 0;
    int success = term_cmd_run(cmd, &output_lines, &output_count);

    free(cmd);

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
        buf->cursor.y = saved_cy;

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
        buf->cursor.x = saved_cx;
    }

    /* 9. Show success message */
    ed_set_status_message("sed: %d line(s)", buf->num_rows);

    return ED_OK;
}
