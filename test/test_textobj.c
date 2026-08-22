#include "../src/buf/textobj.h"
#include "test_helpers.h"
#include "unity/unity.h"
void setUp(void) {}
void tearDown(void) {}
#define totc(fn, ...)                                                          \
    do {                                                                       \
        const char *cases[] = {__VA_ARGS__};                                   \
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {        \
            run_textobj_case(fn, cases[i]);                                    \
        }                                                                      \
    } while (0)

void test_textobj_word(void) {
    totc(textobj_word, "hello [wo^$rld] there", "hello [worl^$d] there",
         "hello [^$a] there",
         "hello[^$ ]there",     // vim iw: a single blank is a word
         "hello[ ^$  ]there",   // vim iw: a blank run is a word
         "hello world[^$   ]"); // trailing blanks at end of line
}

void test_textobj_word_around(void) {
    totc(textobj_word_around,
         "hello [wo^$rld ]there", // word + trailing blanks
         "hello[ wor^$ld]",       // no trailing blanks -> leading blanks
         "hello[^$ there] end",   // on blank: blanks + following word
         "hello[ ^$  there] end", // on blank run: blanks + following word
         "hello [world^$  ]"); // trailing blanks, no word after -> word before
}

void test_textobj_WORD(void) {
    totc(textobj_WORD,
         "hello [foo.b^$ar] there", // WORD spans punctuation
         "x [^$--flag] y",
         "hello[ ^$  ]x.y", // a blank run is a WORD too
         "a.b c.d[^$   ]"); // trailing blanks at end of line
}

void test_textobj_WORD_around(void) {
    totc(textobj_WORD_around,
         "hello [foo.^$bar ]there", // WORD + trailing blanks
         "hello[ foo.b^$ar]",       // no trailing blanks -> leading blanks
         "hello[^$ a.b] end",       // on blank: blanks + following WORD
         "hello [a.b^$  ]"); // trailing blanks, no WORD after -> WORD before
}

void test_textobj_to_word_end(void) {
    totc(textobj_to_word_end,
         "hello^ [worl$d] there", // when at a non word char it should find the
                                  // next word and jump to its end
         "hello wo[^rl$d] there", // inside a word should only select from the
                                  // start cursor till the end of the cursor
         "hello [^worl$d] there", // at start of the word it should select the
         "hell^o [worl$d] there", // when on an end of the previous word, it
                                  // should find the next word and jump there.
         "hello worl^d\n[secon$d] line", // when cursor is at the end of the
                                         // line and word end
         "piaț^ă [lum$e] mare", // multibyte last char: w must leave the word
         "^ă [lum$e]",          // single multibyte-char word
         "f[^ăr$ă] x");         // mid-word on a multibyte char
}

void test_textobj_to_word_start(void) {
    totc(textobj_to_word_start, "hello [$world]^ there",
         "hello [$world ^]there", "hello [$wor^l]d there",

         // when cursor is on the beggining of next line it should pass
         // to the end of previous like to the beggining of the last word
         "hello world [$there]\n^second line",

         "salut [$lum^ă] x", // multibyte last char: b goes to word start
         "[$abc ^]ăst x",    // on a word-starting multibyte char: b must
                             // leave the word
         "[$fără ^]ș x");    // prev word is all multibyte
}

void test_textobj_to_word_utf8_WORD(void) {
    totc(textobj_to_WORD_end, "piaț^ă [lum$e] mare");
    totc(textobj_to_WORD_start, "salut [$lum^ă] x");
}

/* j/k carry the render column, not the byte offset: from 'a' after a
 * tab (byte 1, render col TAB_STOP=4) moving down must land on byte 4
 * of the plain row below, and moving back up must return to byte 1. */
void test_textobj_line_updown_tabs(void) {
    Buffer *buf = create_test_buffer("\tab\n0123456789");
    TEST_ASSERT_NOT_NULL_MESSAGE(buf, "could not create buffer");
    TextSelection sel;
    TEST_ASSERT_TRUE_MESSAGE(textobj_line_down(buf, 0, 1, &sel), "j failed");
    TEST_ASSERT_TRUE_MESSAGE(sel.cursor.line == 1 && sel.cursor.col == 4,
                             "j from tab row lost the render column");
    TEST_ASSERT_TRUE_MESSAGE(textobj_line_up(buf, 1, 4, &sel), "k failed");
    TEST_ASSERT_TRUE_MESSAGE(sel.cursor.line == 0 && sel.cursor.col == 1,
                             "k onto tab row lost the render column");
    free_test_buffer(buf);
}

void test_textobj_char_at_cursor(void) {
    totc(textobj_char_at_cursor, "hello [^$w]orld", "[^$h]ello world");
}

void test_textobj_line(void) {
    totc(textobj_line, "[^$hello world]", "[hello worl$^d]",
         "[^$hello world]\nsecond line");
}

void test_textobj_line_with_newline(void) {
    totc(textobj_line_with_newline, "[he^$llo world\n]second line",
         "[$hello worl^d]");
}

void test_textobj_line_boundaries(void) {
    totc(textobj_to_line_end, "alpha [^bet$a]\n", "alpha bet[^$a]");

    totc(textobj_to_line_start, "hello line 1\n[$alpha ^b]eta\nline 3",
         "line 1\n[^$a]lpha beta");
}

void test_textobj_file_boundaries(void) {
    totc(textobj_to_file_end, "fir[^st line\nsecond line\nthird lin$e]");
    totc(textobj_to_file_start, "[$first line\nsec^o]nd line");
}

// void test_textobj_cursor_to_char_occurence(void) {
//     totc(textobj_to_char_occurence,
//             "hello [^world\n here we stop $?], yes"
//             "hello [^world hello stop$?]\n here we stop , yes"
//             );
//
// }

void test_textobj_brackets_cases(void) {
    totc(textobj_brackets, "call([^$foo bar])", "array([foo bar^$])");
    totc(textobj_curly_inner, "{[bar ^$baz]}");
    totc(textobj_curly_outer, "[{foo ^$bar}]");
}

void test_textobj_paragraphs(void) {
    totc(textobj_to_paragraph_end,
         "para1 [^line1\npara1 line2$\n]\npara2 line1\npara2 line2");
    totc(textobj_to_paragraph_start,
         "[$para1 line1\npara1 ^l]ine2\n\npara2 line1\npara2 line2");
    totc(textobj_paragraph,
         "[para1 line1\npara1 ^line2$\n]\npara2 line1\npara2 line2",
         "something else\n\n[para1 line1\npara1 ^line2$\n]\npara2 line1\npara2 "
         "line2");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_textobj_word);
    RUN_TEST(test_textobj_word_around);
    RUN_TEST(test_textobj_WORD);
    RUN_TEST(test_textobj_WORD_around);
    RUN_TEST(test_textobj_to_word_end);
    RUN_TEST(test_textobj_to_word_start);
    RUN_TEST(test_textobj_to_word_utf8_WORD);
    RUN_TEST(test_textobj_line_updown_tabs);
    RUN_TEST(test_textobj_char_at_cursor);
    RUN_TEST(test_textobj_line);
    RUN_TEST(test_textobj_line_with_newline);
    RUN_TEST(test_textobj_line_boundaries);
    RUN_TEST(test_textobj_file_boundaries);
    RUN_TEST(test_textobj_brackets_cases);
    RUN_TEST(test_textobj_paragraphs);
    return UNITY_END();
}
