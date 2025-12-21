#include "../src/buf/textobj.h"
#include "test_helpers.h"
#include "unity/unity.h"
void setUp(void) { }
void tearDown(void) { }
#define totc(fn, ...)                                                          \
    do {                                                                       \
        const char *cases[] = {__VA_ARGS__};                                   \
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {        \
            run_textobj_case(fn, cases[i]);                                    \
        }                                                                      \
    } while (0)

void test_textobj_word(void) {
    totc(textobj_word, 
		 "hello [wo^$rld] there", 
		 "hello [worl^$d] there",
         "hello [^$a] there");
}

void test_textobj_to_word_end(void) {
    totc(textobj_to_word_end, 
		"hello^ [worl$d] there", //when at a non word char it should find the next word and jump to its end
		"hello wo[^rl$d] there", // inside a word should only select from the start cursor till the end of the cursor
		"hello [^worl$d] there", // at start of the word it should select the 
		"hell^o [worl$d] there", //when on an end of the previous word, it should find the next word and jump there. 
		"hello worl^d\n[secon$d] line"); //when cursor is at the end of the line and word end
}

void test_textobj_to_word_start(void) {
    totc(textobj_to_word_start, 
			"hello [$world]^ there",
			"hello [$world ^]there",
            "hello [$wor^l]d there",

			// when cursor is on the beggining of next line it should pass 
			// to the end of previous like to the beggining of the last word
			"hello world [$there]\n^second line"); 
}

void test_textobj_char_at_cursor(void) {
    totc(textobj_char_at_cursor, 
            "hello [^$w]orld", 
            "[^$h]ello world");
}

void test_textobj_line(void) {
    totc(textobj_line, 
         "[^$hello world]", 
         "[hello worl$^d]",
         "[^$hello world]\nsecond line");
}

void test_textobj_line_with_newline(void) {
    totc(textobj_line_with_newline, 
         "[he^$llo world\n]second line",
         "[$hello worl^d]");
}

void test_textobj_line_boundaries(void) {
    totc(textobj_to_line_end, 
		"alpha [^bet$a]\n", 
        "alpha bet[^$a]");

	totc(textobj_to_line_start,
        "hello line 1\n[$alpha ^b]eta\nline 3",
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
         "something else\n\n[para1 line1\npara1 ^line2$\n]\npara2 line1\npara2 line2");
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
