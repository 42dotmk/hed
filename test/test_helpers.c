#include "test_helpers.h"
#include "unity/unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TestData *parse_test_string(const char *marked_text) {
    if (!marked_text)
        return NULL;

    TestData *data = calloc(1, sizeof(TestData));
    if (!data)
        return NULL;

    size_t len = strlen(marked_text);
    data->text = malloc(len + 1);
    if (!data->text) {
        free(data);
        return NULL;
    }

    int line = 0, col = 0;
    size_t out_idx = 0;
    int found_initial = 0, found_expected = 0, found_start = 0, found_end = 0;

    for (size_t i = 0; i < len; i++) {
        char c = marked_text[i];
        switch (c) {
        case '^':
            if (found_initial) {
                fprintf(stderr, "Error: duplicate '^' marker\n");
                goto fail;
            }
            data->initial.line = line;
            data->initial.col = col;
            found_initial = 1;
            break;
        case '$':
            if (found_expected) {
                fprintf(stderr, "Error: duplicate '$' marker\n");
                goto fail;
            }
            data->expected_cursor.line = line;
            data->expected_cursor.col = col;
            found_expected = 1;
            break;
        case '[':
            if (found_start) {
                fprintf(stderr, "Error: duplicate '[' marker\n");
                goto fail;
            }
            data->selection_start.line = line;
            data->selection_start.col = col;
            found_start = 1;
            break;
        case ']':
            if (found_end) {
                fprintf(stderr, "Error: duplicate ']' marker\n");
                goto fail;
            }
            data->selection_end.line = line;
            data->selection_end.col = col;
            found_end = 1;
            break;
        case '\n':
            data->text[out_idx++] = c;
            line++;
            col = 0;
            break;
        default:
            data->text[out_idx++] = c;
            col++;
            break;
        }
    }

    data->text[out_idx] = '\0';

    if (!found_initial || !found_expected || !found_start || !found_end) {
        fprintf(stderr, "Error: Missing markers in test string\n");
        fprintf(stderr,
                "  Found: ^ = %d, $ = %d, [ = %d, ] = %d\n",
                found_initial, found_expected, found_start, found_end);
        goto fail;
    }

    return data;

fail:
    free(data->text);
    free(data);
    return NULL;
}

void free_test_data(TestData *data) {
    if (data) {
        free(data->text);
        free(data);
    }
}

static int textpos_to_offset(const char *text, TextPos pos, size_t *out_idx) {
    if (!text || !out_idx || pos.line < 0 || pos.col < 0)
        return 0;
    size_t len = strlen(text);
    int line = 0, col = 0;
    for (size_t idx = 0; idx <= len; idx++) {
        if (line == pos.line && col == pos.col) {
            *out_idx = idx;
            return 1;
        }
        if (idx == len)
            break;
        char c = text[idx];
        if (c == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    return 0;
}

char *format_marked_string(const char *text, TextPos initial,
                           TextPos sel_start, TextPos sel_end,
                           TextPos cursor) {
    if (!text)
        return NULL;
    size_t start_idx = 0, end_idx = 0, cursor_idx = 0, initial_idx = 0;
    if (!textpos_to_offset(text, sel_start, &start_idx))
        return NULL;
    if (!textpos_to_offset(text, sel_end, &end_idx))
        return NULL;
    if (!textpos_to_offset(text, cursor, &cursor_idx))
        return NULL;
    if (!textpos_to_offset(text, initial, &initial_idx))
        return NULL;

    size_t len = strlen(text);
    size_t cap = len + 16;
    char *out = malloc(cap);
    if (!out)
        return NULL;

    size_t out_len = 0;
    for (size_t idx = 0; idx <= len; idx++) {
        if (idx == start_idx)
            out[out_len++] = '[';
        if (idx == initial_idx)
            out[out_len++] = '^';
        if (idx == cursor_idx)
            out[out_len++] = '$';
        if (idx == end_idx)
            out[out_len++] = ']';

        if (idx == len)
            break;

        out[out_len++] = text[idx];
    }
    out[out_len] = '\0';
    return out;
}

Buffer *create_test_buffer(const char *text) {
    Buffer *buf = calloc(1, sizeof(Buffer));
    if (!buf)
        return NULL;

    int num_lines = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n')
            num_lines++;
    }

    buf->rows = calloc(num_lines, sizeof(Row));
    if (!buf->rows) {
        free(buf);
        return NULL;
    }
    buf->num_rows = 0;

    const char *line_start = text;
    const char *p = text;
    while (1) {
        if (*p == '\n' || *p == '\0') {
            size_t line_len = p - line_start;
            Row *row = &buf->rows[buf->num_rows];
            row->chars.data = malloc(line_len + 1);
            if (!row->chars.data) {
                free_test_buffer(buf);
                return NULL;
            }
            memcpy(row->chars.data, line_start, line_len);
            row->chars.data[line_len] = '\0';
            row->chars.len = line_len;
            row->chars.cap = line_len + 1;
            buf->num_rows++;

            if (*p == '\0')
                break;
            line_start = p + 1;
        }
        p++;
    }

    return buf;
}

void free_test_buffer(Buffer *buf) {
    if (!buf)
        return;
    for (int i = 0; i < buf->num_rows; i++) {
        free(buf->rows[i].chars.data);
    }
    free(buf->rows);
    free(buf);
}
/* Convenience wrappers for bracket tests */
int textobj_curly_inner(Buffer *buf, int line, int col,
                               TextSelection *sel) {
    return textobj_brackets_with(buf, line, col, '{', '}', false, sel);
}

int textobj_curly_outer(Buffer *buf, int line, int col,
                               TextSelection *sel) {
    return textobj_brackets_with(buf, line, col, '{', '}', true, sel);
}

/*
 * Test helper - run a textobject function and validate results
 */
void run_textobj_case(const char *marked_text,
                             int (*textobj_fn)(Buffer *, int, int,
                                               TextSelection *)) {
    char message[256];

    TestData *data = parse_test_string(marked_text);
    snprintf(message, sizeof(message), "could not parse test string");
    TEST_ASSERT_NOT_NULL_MESSAGE(data, message);

    Buffer *buf = create_test_buffer(data->text);
    snprintf(message, sizeof(message), "could not create buffer");
    TEST_ASSERT_NOT_NULL_MESSAGE(buf, message);

    TextSelection sel = (TextSelection){0};
    int result = textobj_fn(buf, data->initial.line, data->initial.col, &sel);
    snprintf(message, sizeof(message), "textobject function returned 0 (no match)");
    TEST_ASSERT_TRUE_MESSAGE(result, message);

    char *expected_str =
        format_marked_string(data->text, data->initial, data->selection_start,
                             data->selection_end, data->expected_cursor);
    snprintf(message, sizeof(message), "could not format expected string");
    TEST_ASSERT_NOT_NULL_MESSAGE(expected_str, message);

    char *actual_str = format_marked_string(data->text, data->initial,
                                            sel.start, sel.end, sel.cursor);
    snprintf(message, sizeof(message), "could not format actual string");
    TEST_ASSERT_NOT_NULL_MESSAGE(actual_str, message);

    snprintf(message, sizeof(message), "mismatch");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected_str, actual_str, message);

    free(expected_str);
    free(actual_str);
    free_test_buffer(buf);
    free_test_data(data);
}
