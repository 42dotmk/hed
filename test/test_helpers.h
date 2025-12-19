#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stddef.h>

#include "../src/buf/buffer.h"
#include "../src/buf/textobj.h"

typedef struct {
    char *text;
    TextPos initial;
    TextPos expected_cursor;
    TextPos selection_start;
    TextPos selection_end;
} TestData;

TestData *parse_test_string(const char *marked_text);
void free_test_data(TestData *data);
char *format_marked_string(const char *text, TextPos initial,
                           TextPos sel_start, TextPos sel_end,
                           TextPos cursor);
Buffer *create_test_buffer(const char *text);
void free_test_buffer(Buffer *buf);
void run_textobj_case(const char *marked_text,
                             int (*textobj_fn)(Buffer *, int, int,
                                               TextSelection *));


int textobj_curly_outer(Buffer *buf, int line, int col,
                               TextSelection *sel);
int textobj_curly_inner(Buffer *buf, int line, int col,
                               TextSelection *sel);

#endif /* TEST_HELPERS_H */
