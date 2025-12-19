/*
 * Textobject Tests
 *
 * Test utility for validating textobject behavior with marked test strings.
 *
 * Marker format:
 *   ^ = initial cursor position
 *   $ = expected cursor position after operation
 *   [ ] = expected selection boundaries (start and end)
 *
 * Example:
 *   "hello [^wor$ld] there"
 *   - Initial cursor at 'w' (position 6)
 *   - Expected selection from '[' to ']' (positions 6-11)
 *   - Expected cursor at 'd' (position 9)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define COLOR_PASS "\x1b[32m"
#define COLOR_FAIL "\x1b[31m"
#define COLOR_RESET "\x1b[0m"

/* Include hed's textobject implementation */
#include "../src/buf/buffer.h"
#include "../src/buf/textobj.h"
#include "../src/lib/sizedstr.h"

typedef struct {
    char *text;              /* Cleaned text (no markers) */
    TextPos initial;         /* Initial cursor location */
    TextPos expected_cursor; /* Expected cursor location */
    TextPos selection_start; /* Expected selection start */
    TextPos selection_end;   /* Expected selection end */
} TestData;

static int total_tests = 0;
static int passed_tests = 0;

/*
 * Parse a test string with markers and extract test data.
 *
 * Returns NULL on error, otherwise a TestData* that must be freed with free().
 */
TestData *parse_test_string(const char *marked_text) {
    if (!marked_text)
        return NULL;

    TestData *data = calloc(1, sizeof(TestData));
    if (!data)
        return NULL;

    size_t len = strlen(marked_text);
    data->text = malloc(len + 1); /* Upper bound (markers removed later) */
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
        fprintf(stderr, "  Found: ^ = %d, $ = %d, [ = %d, ] = %d\n",
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

static char *format_marked_string(const char *text, TextPos initial,
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

/*
 * Create a Buffer from test data text
 */
void free_test_buffer(Buffer *buf);
Buffer *create_test_buffer(const char *text) {
    Buffer *buf = calloc(1, sizeof(Buffer));
    if (!buf)
        return NULL;

    /* Count lines */
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

    /* Split text into lines */
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
static int textobj_curly_inner(Buffer *buf, int line, int col,
                               TextSelection *sel) {
    return textobj_brackets_with(buf, line, col, '{', '}', 0, sel);
}

static int textobj_curly_outer(Buffer *buf, int line, int col,
                               TextSelection *sel) {
    return textobj_brackets_with(buf, line, col, '{', '}', 1, sel);
}

/*
 * Test helper - run a textobject function and validate results
 */
int test_textobj(const char *test_name, const char *marked_text,
                 int (*textobj_fn)(Buffer *, int, int, TextSelection *)) {
    total_tests++;
    printf("Test: %s\n", test_name);

    TestData *data = parse_test_string(marked_text);
    if (!data) {
        printf("  %sFAIL%s: Could not parse test string\n\n", COLOR_FAIL,
               COLOR_RESET);
        return 0;
    }

    Buffer *buf = create_test_buffer(data->text);
    if (!buf) {
        printf("  %sFAIL%s: Could not create buffer\n\n", COLOR_FAIL,
               COLOR_RESET);
        free_test_data(data);
        return 0;
    }

    TextSelection sel = {0};
    int result =
        textobj_fn(buf, data->initial.line, data->initial.col, &sel);

    if (!result) {
        printf("  %sFAIL%s: Textobject function returned 0 (no match)\n\n",
               COLOR_FAIL, COLOR_RESET);
        free_test_buffer(buf);
        free_test_data(data);
        return 0;
    }

    char *expected_str = format_marked_string(
        data->text, data->initial, data->selection_start, data->selection_end,
        data->expected_cursor);
    char *actual_str = format_marked_string(
        data->text, data->initial, sel.start, sel.end, sel.cursor);

    int pass = 1;
    if (!expected_str || !actual_str) {
        printf("  %sFAIL%s: Could not format comparison strings\n", COLOR_FAIL,
               COLOR_RESET);
        pass = 0;
    } else if (strcmp(expected_str, actual_str) != 0) {
        printf("  %sFAIL%s\n", COLOR_FAIL, COLOR_RESET);
        printf("    Expected: \"%s\"\n", expected_str);
        printf("    Got:      \"%s\"\n", actual_str);
        pass = 0;
    }

    if (pass) {
        printf("  %sPASS%s\n", COLOR_PASS, COLOR_RESET);
        passed_tests++;
    }
    printf("\n");

    free(expected_str);
    free(actual_str);
    free_test_buffer(buf);
    free_test_data(data);
    return pass;
}

/*
 * Test cases
 */

void test_textobj_word(void) {
    printf("=== Testing textobj_word ===\n\n");

    test_textobj(
        "word - cursor at start",
        "hello [^$world] there",
        textobj_word
    );

    test_textobj(
        "word - cursor in middle",
        "hello [wo^$rld] there",
        textobj_word
    );

    test_textobj(
        "word - cursor at end",
        "hello [worl^$d] there",
        textobj_word
    );

    test_textobj(
        "word - single character",
        "hello [^$a] there",
        textobj_word
    );
}

void test_textobj_to_word_end(void) {
    printf("=== Testing textobj_to_word_end ===\n\n");

    test_textobj(
        "to_word_end - from start",
        "hello^$ [world] there",
        textobj_to_word_end
    );

    test_textobj(
        "to_word_end - from middle",
        "hello wo[^$rld] there",
        textobj_to_word_end
    );
}

void test_textobj_to_word_start(void) {
    printf("=== Testing textobj_to_word_start ===\n\n");

    test_textobj(
        "to_word_start - from end",
        "hello [world]^$ there",
        textobj_to_word_start
    );

    test_textobj(
        "to_word_start - from middle",
        "hello [wor]^$ld there",
        textobj_to_word_start
    );
}

void test_textobj_char_at_cursor(void) {
    printf("=== Testing textobj_char_at_cursor ===\n\n");

    test_textobj(
        "char - ASCII character",
        "hello [^$w]orld",
        textobj_char_at_cursor
    );

    test_textobj(
        "char - first character",
        "[^$h]ello world",
        textobj_char_at_cursor
    );
}

void test_textobj_line(void) {
    printf("=== Testing textobj_line ===\n\n");

    test_textobj(
        "line - single line",
        "[^$hello world]",
        textobj_line
    );

    test_textobj(
        "line - cursor near end",
        "[hello worl^$d]",
        textobj_line
    );

    test_textobj(
        "line - multiline, first line",
        "[^$hello world]\nsecond line",
        textobj_line
    );
}

void test_textobj_line_with_newline(void) {
    printf("=== Testing textobj_line_with_newline ===\n\n");

    test_textobj(
        "line_with_newline - first line",
        "[$he^llo world\n]second line",
        textobj_line_with_newline
    );

    test_textobj(
        "line_with_newline - last line only",
        "[$hello worl^d]",
        textobj_line_with_newline
    );
}

void test_textobj_line_boundaries(void) {
    printf("=== Testing line boundary motions ===\n\n");

    test_textobj(
        "to_line_end - middle",
        "alpha [^$beta]",
        textobj_to_line_end
    );

    test_textobj(
        "to_line_end - at end",
        "alpha beta[^$]",
        textobj_to_line_end
    );

    test_textobj(
        "to_line_start - middle",
        "[alpha ]^$beta",
        textobj_to_line_start
    );

    test_textobj(
        "to_line_start - at start",
        "[]^$alpha beta",
        textobj_to_line_start
    );
}

void test_textobj_file_boundaries(void) {
    printf("=== Testing file boundary motions ===\n\n");

    test_textobj(
        "to_file_end - first line mid",
        "fir[^$st line\nsecond line\nthird line]",
        textobj_to_file_end
    );

    test_textobj(
        "to_file_start - second line mid",
        "[first line\nsec]^$ond line",
        textobj_to_file_start
    );
}

void test_textobj_brackets_cases(void) {
    printf("=== Testing bracket textobjects ===\n\n");

    test_textobj(
        "brackets - cursor on '('",
        "call(^$[foo bar])",
        textobj_brackets
    );

    test_textobj(
        "brackets - cursor on ')'",
        "array([foo bar]^$)",
        textobj_brackets
    );

    test_textobj(
        "brackets_with - inner braces",
        "{[bar ^$baz]}",
        textobj_curly_inner
    );

    test_textobj(
        "brackets_with - include braces",
        "[{foo ^$bar}]",
        textobj_curly_outer
    );
}

void test_textobj_paragraphs(void) {
    printf("=== Testing paragraph textobjects ===\n\n");

    test_textobj(
        "paragraph_end - from first line",
        "para1 [^$line1\npara1 line2]\n\npara2 line1\npara2 line2",
        textobj_to_paragraph_end
    );

    test_textobj(
        "paragraph_start - from second line",
        "[para1 line1\npara1 ]^$line2\n\npara2 line1\npara2 line2",
        textobj_to_paragraph_start
    );

    test_textobj(
        "paragraph - whole block",
        "[para1 line1\npara1 ^$line2]\n\npara2 line1\npara2 line2",
        textobj_paragraph
    );
}

int main(void) {
    printf("Running Textobject Tests\n");
    printf("========================\n\n");

    test_textobj_word();
    test_textobj_to_word_end();
    test_textobj_to_word_start();
    test_textobj_char_at_cursor();
    test_textobj_line();
    test_textobj_line_with_newline();
    test_textobj_line_boundaries();
    test_textobj_file_boundaries();
    test_textobj_brackets_cases();
    test_textobj_paragraphs();

    printf("========================\n");
    printf("Passed %d/%d tests\n", passed_tests, total_tests);

    if (passed_tests != total_tests) {
        printf("Some tests failed!\n");
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
