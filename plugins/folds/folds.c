/*
 * folds plugin — bracket and indent fold methods.
 *
 * Two detectors, both registered into the core fold-method registry
 * (src/utils/fold_methods.{c,h}):
 *
 *   bracket — pairs `{` with `}` ignoring chars inside string literals.
 *             Emits one fold per pair spanning at least one row.
 *   indent  — runs of lines whose indentation is strictly greater than
 *             a containing "parent" line. Blank lines inherit the
 *             surrounding indent so they don't split a fold.
 *
 * Plus filetype default bindings so e.g. opening a .c file picks
 * "bracket" automatically. Users can still override per-buffer with
 * :foldmethod.
 *
 * Lifted from the old src/fold_methods/ subdirectory — see the plan
 * file at the project root for why.
 */

#include "folds/folds.h"
#include "buf/buffer.h"
#include "buf/row.h"
#include "utils/fold.h"
#include "utils/fold_methods.h"
#include <stdlib.h>

/* ====================================================================
 * bracket
 * ==================================================================== */

typedef struct {
    int *lines;
    int count;
    int capacity;
} BracketStack;

static void bstack_init(BracketStack *s) {
    s->lines = NULL;
    s->count = 0;
    s->capacity = 0;
}

static void bstack_free(BracketStack *s) {
    free(s->lines);
    s->lines = NULL;
    s->count = 0;
    s->capacity = 0;
}

static void bstack_push(BracketStack *s, int line) {
    if (s->count >= s->capacity) {
        int new_cap = s->capacity == 0 ? 8 : s->capacity * 2;
        int *new_lines = realloc(s->lines, new_cap * sizeof(int));
        if (!new_lines)
            return;
        s->lines = new_lines;
        s->capacity = new_cap;
    }
    s->lines[s->count++] = line;
}

static int bstack_pop(BracketStack *s) {
    if (s->count == 0)
        return -1;
    return s->lines[--s->count];
}

/* Cheap string-literal detector: scan the prefix tracking quote
 * state with \ escaping. Misses raw strings, heredocs, comments —
 * good enough for braces in mainstream syntaxes. */
static bool char_is_inside_string(const char *line, size_t pos) {
    bool in_sq = false, in_dq = false, escaped = false;
    for (size_t i = 0; i < pos; i++) {
        if (escaped) { escaped = false; continue; }
        if (line[i] == '\\') { escaped = true; continue; }
        if (line[i] == '\'' && !in_dq) in_sq = !in_sq;
        else if (line[i] == '"' && !in_sq) in_dq = !in_dq;
    }
    return in_sq || in_dq;
}

static void detect_brackets(Buffer *buf) {
    if (!buf)
        return;

    fold_clear_all(&buf->folds);
    for (int i = 0; i < buf->num_rows; i++) {
        buf->rows[i].fold_start = false;
        buf->rows[i].fold_end = false;
    }

    BracketStack stack;
    bstack_init(&stack);

    for (int line = 0; line < buf->num_rows; line++) {
        Row *row = &buf->rows[line];
        for (size_t i = 0; i < row->chars.len; i++) {
            char c = row->chars.data[i];
            if (char_is_inside_string(row->chars.data, i))
                continue;
            if (c == '{') {
                bstack_push(&stack, line);
            } else if (c == '}') {
                int start_line = bstack_pop(&stack);
                if (start_line >= 0 && start_line < line) {
                    if (line - start_line > 0) {
                        buf->rows[start_line].fold_start = true;
                        buf->rows[line].fold_end = true;
                        fold_add_region(&buf->folds, start_line, line);
                    }
                }
            }
        }
    }

    bstack_free(&stack);
}

/* ====================================================================
 * indent
 * ==================================================================== */

static int indent_width(Row *row) {
    int indent = 0;
    for (size_t i = 0; i < row->chars.len; i++) {
        char c = row->chars.data[i];
        if (c == ' ') indent++;
        else if (c == '\t') indent += 4;
        else break;
    }
    return indent;
}

static bool row_is_blank(Row *row) {
    for (size_t i = 0; i < row->chars.len; i++) {
        char c = row->chars.data[i];
        if (c != ' ' && c != '\t')
            return false;
    }
    return true;
}

typedef struct {
    int start_line;
    int base_indent;
    bool active;
} IndentFrame;

static void detect_indent(Buffer *buf) {
    if (!buf || buf->num_rows == 0)
        return;

    fold_clear_all(&buf->folds);
    for (int i = 0; i < buf->num_rows; i++) {
        buf->rows[i].fold_start = false;
        buf->rows[i].fold_end = false;
    }

    IndentFrame *stack = malloc(sizeof(IndentFrame) * 32);
    if (!stack)
        return;
    int sz = 0, cap = 32;

    for (int line = 0; line < buf->num_rows; line++) {
        Row *row = &buf->rows[line];
        if (row_is_blank(row))
            continue;

        int indent = indent_width(row);

        /* Close every frame whose base_indent the current line has
         * met or undershot — those folds ended on the previous
         * non-blank row. Trim trailing blanks off the fold tail. */
        while (sz > 0) {
            IndentFrame *top = &stack[sz - 1];
            if (indent <= top->base_indent) {
                int end = line - 1;
                while (end > top->start_line && row_is_blank(&buf->rows[end]))
                    end--;
                if (end > top->start_line) {
                    buf->rows[top->start_line].fold_start = true;
                    buf->rows[end].fold_end = true;
                    fold_add_region(&buf->folds, top->start_line, end);
                }
                sz--;
            } else {
                break;
            }
        }

        /* Open a frame if the next non-blank row indents further. */
        int next = line + 1;
        while (next < buf->num_rows && row_is_blank(&buf->rows[next]))
            next++;

        if (next < buf->num_rows && indent_width(&buf->rows[next]) > indent) {
            if (sz >= cap) {
                cap *= 2;
                IndentFrame *new_stack = realloc(stack, sizeof(IndentFrame) * cap);
                if (!new_stack) {
                    free(stack);
                    return;
                }
                stack = new_stack;
            }
            stack[sz].start_line = line;
            stack[sz].base_indent = indent;
            stack[sz].active = true;
            sz++;
        }
    }

    /* Close anything still open at EOF. */
    while (sz > 0) {
        IndentFrame *top = &stack[sz - 1];
        int end = buf->num_rows - 1;
        while (end > top->start_line && row_is_blank(&buf->rows[end]))
            end--;
        if (end > top->start_line) {
            buf->rows[top->start_line].fold_start = true;
            buf->rows[end].fold_end = true;
            fold_add_region(&buf->folds, top->start_line, end);
        }
        sz--;
    }

    free(stack);
}

/* ====================================================================
 * plugin lifecycle
 * ==================================================================== */

static int folds_init(void) {
    fold_method_register("bracket", detect_brackets);
    fold_method_register("indent",  detect_indent);

    /* Filetype defaults. Match the filetype strings emitted by
     * fs_path_detect_filetype() — anything not listed falls through
     * to "manual" (no folds) until the user picks one. */
    fold_method_set_default("c",          "bracket");
    fold_method_set_default("cpp",        "bracket");
    fold_method_set_default("javascript", "bracket");
    fold_method_set_default("typescript", "bracket");
    fold_method_set_default("rust",       "bracket");
    fold_method_set_default("java",       "bracket");
    fold_method_set_default("go",         "bracket");

    fold_method_set_default("python",     "indent");
    fold_method_set_default("shell",      "indent");
    fold_method_set_default("yaml",       "indent");

    return 0;
}

const Plugin plugin_folds = {
    .name   = "folds",
    .desc   = "bracket + indent fold methods",
    .init   = folds_init,
    .deinit = NULL,
};
