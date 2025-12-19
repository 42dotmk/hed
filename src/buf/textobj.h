#ifndef TEXTOBJ_H
#define TEXTOBJ_H

#include "buffer.h"

/* Simple position (line, column), both 0-indexed and byte-based. */
typedef struct TextPos {
    int line;
    int col;
} TextPos;

/* A generic selection with inclusive start, exclusive end, plus cursor. */
typedef struct TextSelection {
    TextPos start;  /* inclusive */
    TextPos end;    /* exclusive */
    TextPos cursor; /* cursor location (clamped) */
} TextSelection;

/* Text-object helpers. All return 1 on success, 0 on failure. */
int textobj_word(Buffer *buf, int line, int col, TextSelection *sel);
int textobj_line(Buffer *buf, int line, int col, TextSelection *sel);
/* Brackets/quotes: inner selection inferred from nearest enclosing pair. */
int textobj_brackets(Buffer *buf, int line, int col, TextSelection *sel);
/* Brackets/quotes with explicit delimiters; include_delims=1 returns outer
 * range including the pair. */
int textobj_brackets_with(Buffer *buf, int line, int col, char open, char close,
                          int include_delims, TextSelection *sel);

int textobj_to_word_end(Buffer *buf, int line, int col, TextSelection *sel);
int textobj_to_word_start(Buffer *buf, int line, int col, TextSelection *sel);
int textobj_to_line_end(Buffer *buf, int line, int col, TextSelection *sel);
int textobj_to_line_start(Buffer *buf, int line, int col, TextSelection *sel);
int textobj_to_file_end(Buffer *buf, int line, int col, TextSelection *sel);
int textobj_to_file_start(Buffer *buf, int line, int col, TextSelection *sel);
int textobj_to_paragraph_end(Buffer *buf, int line, int col,
                             TextSelection *sel);
int textobj_to_paragraph_start(Buffer *buf, int line, int col,
                               TextSelection *sel);
int textobj_paragraph(Buffer *buf, int line, int col, TextSelection *sel);

/* New textobjects for character and line deletion */
int textobj_char_at_cursor(Buffer *buf, int line, int col, TextSelection *sel);
int textobj_line_with_newline(Buffer *buf, int line, int col,
                               TextSelection *sel);

#endif /* TEXTOBJ_H */
