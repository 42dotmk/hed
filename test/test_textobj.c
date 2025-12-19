#include "test_helpers.h"
#include "unity/unity.h"
#include "../src/buf/textobj.h"

void setUp(void) {}
void tearDown(void) {}

void test_textobj_word(void) {
    run_textobj_case("hello [wo^$rld] there",
                     textobj_word);

    run_textobj_case("hello [worl^$d] there",
                     textobj_word);

    run_textobj_case("hello [^$a] there",
                     textobj_word);
}

void test_textobj_to_word_end(void) {
    run_textobj_case("hello^ [worl$d] there",
                     textobj_to_word_end);

    run_textobj_case("hello wo[^rl$d] there",
                     textobj_to_word_end);
}

void test_textobj_to_word_start(void) {
    run_textobj_case("hello [$world]^ there",
                     textobj_to_word_start);

    run_textobj_case("hello [$wor]^ld there",
                     textobj_to_word_start);
}

void test_textobj_char_at_cursor(void) {
    run_textobj_case("hello [^$w]orld",
                     textobj_char_at_cursor);

    run_textobj_case("[^$h]ello world",
                     textobj_char_at_cursor);
}

void test_textobj_line(void) {
    run_textobj_case("[^$hello world]", textobj_line);

    run_textobj_case("[hello worl$^d]", textobj_line);

    run_textobj_case(
                     "[^$hello world]\nsecond line", textobj_line);
}

void test_textobj_line_with_newline(void) {
    run_textobj_case(
                     "[he^$llo world\n]second line", textobj_line_with_newline);

    run_textobj_case("[$hello worl^d]",
                     textobj_line_with_newline);
}

void test_textobj_line_boundaries(void) {
    run_textobj_case("alpha [^beta$]",
                     textobj_to_line_end);

    run_textobj_case("alpha beta[^$]",
                     textobj_to_line_end);

    run_textobj_case("[$alpha ^b]eta",
                     textobj_to_line_start);

    run_textobj_case("[^$a]lpha beta",
                     textobj_to_line_start);
}

void test_textobj_file_boundaries(void) {
    run_textobj_case(
                     "fir[^$st line\nsecond line\nthird line]",
                     textobj_to_file_end);

    run_textobj_case(
                     "[$first line\nsec^o]nd line", textobj_to_file_start);
}

void test_textobj_brackets_cases(void) {
    run_textobj_case("call([^$foo bar])",
                     textobj_brackets);

    run_textobj_case("array([foo ba^$r])",
                     textobj_brackets);

    run_textobj_case("{[bar ^$baz]}",
                     textobj_curly_inner);

    run_textobj_case("[{foo ^$bar}]",
                     textobj_curly_outer);
}

void test_textobj_paragraphs(void) {
    run_textobj_case(
                     "para1 [^$line1\npara1 line2]\n\npara2 line1\npara2 line2",
                     textobj_to_paragraph_end);

    run_textobj_case(
                     "[para1 line1\npara1 ]^$line2\n\npara2 line1\npara2 line2",
                     textobj_to_paragraph_start);

    run_textobj_case(
                     "[para1 line1\npara1 ^$line2]\n\npara2 line1\npara2 line2",
                     textobj_paragraph);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_textobj_word);
    RUN_TEST(test_textobj_to_word_end);
    RUN_TEST(test_textobj_to_word_start);
    RUN_TEST(test_textobj_char_at_cursor);
    RUN_TEST(test_textobj_line);
    RUN_TEST(test_textobj_line_with_newline);
    RUN_TEST(test_textobj_line_boundaries);
    RUN_TEST(test_textobj_file_boundaries);
    RUN_TEST(test_textobj_brackets_cases);
    RUN_TEST(test_textobj_paragraphs);
    return UNITY_END();
}
