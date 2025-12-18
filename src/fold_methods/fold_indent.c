#include "fold_methods.h"
#include "fold.h"
#include "buf/buffer.h"
#include "buf/row.h"
#include <stdlib.h>

/*
 * Calculate the indentation level of a line
 * Returns the number of leading spaces (tabs count as 4 spaces)
 */
static int get_indent_level(Row *row) {
    int indent = 0;
    for (size_t i = 0; i < row->chars.len; i++) {
        char c = row->chars.data[i];
        if (c == ' ') {
            indent++;
        } else if (c == '\t') {
            indent += 4;
        } else {
            /* Non-whitespace character, stop counting */
            break;
        }
    }
    return indent;
}

/*
 * Check if a line is blank (only whitespace)
 */
static bool is_blank_line(Row *row) {
    for (size_t i = 0; i < row->chars.len; i++) {
        char c = row->chars.data[i];
        if (c != ' ' && c != '\t') {
            return false;
        }
    }
    return true;
}

/*
 * Fold region being built
 */
typedef struct {
    int start_line;
    int base_indent;
    bool active;
} IndentFold;

void fold_detect_indent(Buffer *buf) {
    if (!buf)
        return;

    /* Clear existing folds and markers */
    fold_clear_all(&buf->folds);
    for (int i = 0; i < buf->num_rows; i++) {
        buf->rows[i].fold_start = false;
        buf->rows[i].fold_end = false;
    }

    if (buf->num_rows == 0)
        return;

    /* Stack of active indent-based folds */
    IndentFold *fold_stack = malloc(sizeof(IndentFold) * 32);
    if (!fold_stack)
        return;
    int stack_size = 0;
    int stack_capacity = 32;

    for (int line = 0; line < buf->num_rows; line++) {
        Row *row = &buf->rows[line];

        /* Skip blank lines - they inherit the context */
        if (is_blank_line(row))
            continue;

        int indent = get_indent_level(row);

        /* Close any folds that have ended (indent returned to or below base) */
        while (stack_size > 0) {
            IndentFold *top = &fold_stack[stack_size - 1];
            if (indent <= top->base_indent) {
                /* Fold has ended at previous line */
                int end_line = line - 1;

                /* Find last non-blank line */
                while (end_line > top->start_line && is_blank_line(&buf->rows[end_line])) {
                    end_line--;
                }

                /* Only create fold if it spans at least 2 lines */
                if (end_line > top->start_line) {
                    buf->rows[top->start_line].fold_start = true;
                    buf->rows[end_line].fold_end = true;
                    fold_add_region(&buf->folds, top->start_line, end_line);
                }

                stack_size--;
            } else {
                break;
            }
        }

        /* Check if we should start a new fold */
        /* A fold starts when the next non-blank line has greater indent */
        int next_line = line + 1;
        while (next_line < buf->num_rows && is_blank_line(&buf->rows[next_line])) {
            next_line++;
        }

        if (next_line < buf->num_rows) {
            int next_indent = get_indent_level(&buf->rows[next_line]);
            if (next_indent > indent) {
                /* Start a new fold */
                if (stack_size >= stack_capacity) {
                    stack_capacity *= 2;
                    IndentFold *new_stack = realloc(fold_stack, sizeof(IndentFold) * stack_capacity);
                    if (!new_stack) {
                        free(fold_stack);
                        return;
                    }
                    fold_stack = new_stack;
                }

                fold_stack[stack_size].start_line = line;
                fold_stack[stack_size].base_indent = indent;
                fold_stack[stack_size].active = true;
                stack_size++;
            }
        }
    }

    /* Close any remaining open folds at end of buffer */
    while (stack_size > 0) {
        IndentFold *top = &fold_stack[stack_size - 1];
        int end_line = buf->num_rows - 1;

        /* Find last non-blank line */
        while (end_line > top->start_line && is_blank_line(&buf->rows[end_line])) {
            end_line--;
        }

        /* Only create fold if it spans at least 2 lines */
        if (end_line > top->start_line) {
            buf->rows[top->start_line].fold_start = true;
            buf->rows[end_line].fold_end = true;
            fold_add_region(&buf->folds, top->start_line, end_line);
        }

        stack_size--;
    }

    free(fold_stack);
}
