#include "fold_methods.h"
#include "fold.h"
#include "buf/buffer.h"
#include "buf/row.h"
#include <stdlib.h>

/*
 * Stack for tracking opening bracket positions
 */
typedef struct {
    int *lines;     /* Line numbers of opening brackets */
    int count;
    int capacity;
} BracketStack;

static void stack_init(BracketStack *stack) {
    stack->lines = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

static void stack_free(BracketStack *stack) {
    free(stack->lines);
    stack->lines = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

static void stack_push(BracketStack *stack, int line) {
    if (stack->count >= stack->capacity) {
        int new_capacity = stack->capacity == 0 ? 8 : stack->capacity * 2;
        int *new_lines = realloc(stack->lines, new_capacity * sizeof(int));
        if (!new_lines)
            return;
        stack->lines = new_lines;
        stack->capacity = new_capacity;
    }
    stack->lines[stack->count++] = line;
}

static int stack_pop(BracketStack *stack) {
    if (stack->count == 0)
        return -1;
    return stack->lines[--stack->count];
}

/*
 * Check if a character is inside a string or comment
 * Simple heuristic: check if we're inside quotes
 */
static bool is_in_string(const char *line, size_t pos) {
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    for (size_t i = 0; i < pos; i++) {
        if (escaped) {
            escaped = false;
            continue;
        }

        if (line[i] == '\\') {
            escaped = true;
            continue;
        }

        if (line[i] == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (line[i] == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        }
    }

    return in_single_quote || in_double_quote;
}

void fold_detect_brackets(Buffer *buf) {
    if (!buf)
        return;

    /* Clear existing folds and markers */
    fold_clear_all(&buf->folds);
    for (int i = 0; i < buf->num_rows; i++) {
        buf->rows[i].fold_start = false;
        buf->rows[i].fold_end = false;
    }

    BracketStack stack;
    stack_init(&stack);

    /* Scan through all lines looking for brackets */
    for (int line = 0; line < buf->num_rows; line++) {
        Row *row = &buf->rows[line];

        for (size_t i = 0; i < row->chars.len; i++) {
            char c = row->chars.data[i];

            /* Skip brackets inside strings */
            if (is_in_string(row->chars.data, i))
                continue;

            if (c == '{') {
                /* Opening bracket - push to stack */
                stack_push(&stack, line);
            } else if (c == '}') {
                /* Closing bracket - pop and create fold */
                int start_line = stack_pop(&stack);
                if (start_line >= 0 && start_line < line) {
                    /* Only create fold if there's at least one line between */
                    if (line - start_line > 0) {
                        /* Mark the rows */
                        buf->rows[start_line].fold_start = true;
                        buf->rows[line].fold_end = true;

                        /* Add fold region */
                        fold_add_region(&buf->folds, start_line, line);
                    }
                }
            }
        }
    }

    stack_free(&stack);
}
