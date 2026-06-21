/* Parser tests for ed_parse_key_from_fd: feed byte sequences through a
 * pipe and assert on the decoded key, with focus on SGR mouse events. */
#include "../src/input/input.h"
#include "../src/editor.h"
#include "unity/unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void setUp(void) { }
void tearDown(void) { }

/* input.c calls die() on hard read errors; tests never hit that path. */
void die(const char *s) {
    perror(s);
    exit(1);
}

/* The vendored Unity is minimal (only *_MESSAGE asserts). */
#define ASSERT_EQ_INT(expected, actual)                                        \
    do {                                                                       \
        int _e = (int)(expected), _a = (int)(actual);                          \
        char _msg[160];                                                        \
        snprintf(_msg, sizeof(_msg), "%s: expected %d, got %d", #actual, _e,   \
                 _a);                                                          \
        TEST_ASSERT_TRUE_MESSAGE(_e == _a, _msg);                              \
    } while (0)

static int parse_bytes(const char *bytes, size_t len) {
    int fds[2];
    ASSERT_EQ_INT(0, pipe(fds));
    ASSERT_EQ_INT((int)len, (int)write(fds[1], bytes, len));
    close(fds[1]);
    int key = ed_parse_key_from_fd(fds[0]);
    close(fds[0]);
    return key;
}

#define PARSE(lit) parse_bytes(lit, sizeof(lit) - 1)

void test_mouse_press(void) {
    /* Left button press at column 42, row 7. */
    int key = PARSE("\x1b[<0;42;7M");
    ASSERT_EQ_INT(KEY_MOUSE, key);
    const MouseEvent *ev = ed_last_mouse();
    ASSERT_EQ_INT(MOUSE_PRESS, ev->type);
    ASSERT_EQ_INT(0, ev->button);
    ASSERT_EQ_INT(42, ev->x);
    ASSERT_EQ_INT(7, ev->y);
    ASSERT_EQ_INT(0, ev->mods);
}

void test_mouse_release(void) {
    int key = PARSE("\x1b[<0;42;7m");
    ASSERT_EQ_INT(KEY_MOUSE, key);
    ASSERT_EQ_INT(MOUSE_RELEASE, ed_last_mouse()->type);
}

void test_mouse_drag(void) {
    /* Bit 32 = motion with button held. */
    int key = PARSE("\x1b[<32;10;3M");
    ASSERT_EQ_INT(KEY_MOUSE, key);
    const MouseEvent *ev = ed_last_mouse();
    ASSERT_EQ_INT(MOUSE_DRAG, ev->type);
    ASSERT_EQ_INT(0, ev->button);
    ASSERT_EQ_INT(10, ev->x);
    ASSERT_EQ_INT(3, ev->y);
}

void test_mouse_wheel(void) {
    int key = PARSE("\x1b[<64;5;5M");
    ASSERT_EQ_INT(KEY_MOUSE, key);
    ASSERT_EQ_INT(MOUSE_WHEEL_UP, ed_last_mouse()->type);
    ASSERT_EQ_INT(-1, ed_last_mouse()->button);

    key = PARSE("\x1b[<65;5;5M");
    ASSERT_EQ_INT(KEY_MOUSE, key);
    ASSERT_EQ_INT(MOUSE_WHEEL_DOWN, ed_last_mouse()->type);
}

void test_mouse_modifiers(void) {
    /* 0 + 4 (shift) + 16 (ctrl) = 20. */
    int key = PARSE("\x1b[<20;1;1M");
    ASSERT_EQ_INT(KEY_MOUSE, key);
    const MouseEvent *ev = ed_last_mouse();
    ASSERT_EQ_INT(KEY_SHIFT | KEY_CTRL, ev->mods);
    ASSERT_EQ_INT(0, ev->button);
}

void test_mouse_right_button(void) {
    int key = PARSE("\x1b[<2;8;2M");
    ASSERT_EQ_INT(KEY_MOUSE, key);
    ASSERT_EQ_INT(2, ed_last_mouse()->button);
}

void test_mouse_large_coordinates(void) {
    /* SGR's reason to exist: coordinates past the old 223 limit. */
    int key = PARSE("\x1b[<0;500;300M");
    ASSERT_EQ_INT(KEY_MOUSE, key);
    ASSERT_EQ_INT(500, ed_last_mouse()->x);
    ASSERT_EQ_INT(300, ed_last_mouse()->y);
}

void test_mouse_truncated_degrades_to_esc(void) {
    ASSERT_EQ_INT('\x1b', PARSE("\x1b[<0;42"));
    ASSERT_EQ_INT('\x1b', PARSE("\x1b[<0;42;7;9M"));
    ASSERT_EQ_INT('\x1b', PARSE("\x1b[<0;42;7X"));
}

/* Regressions: mouse branch must not disturb existing CSI parsing. */
void test_csi_still_works(void) {
    ASSERT_EQ_INT(KEY_ARROW_UP, PARSE("\x1b[A"));
    ASSERT_EQ_INT(KEY_DELETE, PARSE("\x1b[3~"));
    ASSERT_EQ_INT(KEY_CTRL | KEY_ARROW_LEFT, PARSE("\x1b[1;5D"));
    ASSERT_EQ_INT(KEY_META | 'x', PARSE("\x1bx"));
    ASSERT_EQ_INT(KEY_F1, PARSE("\x1bOP"));
    ASSERT_EQ_INT('a', PARSE("a"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mouse_press);
    RUN_TEST(test_mouse_release);
    RUN_TEST(test_mouse_drag);
    RUN_TEST(test_mouse_wheel);
    RUN_TEST(test_mouse_modifiers);
    RUN_TEST(test_mouse_right_button);
    RUN_TEST(test_mouse_large_coordinates);
    RUN_TEST(test_mouse_truncated_degrades_to_esc);
    RUN_TEST(test_csi_still_works);
    return UNITY_END();
}
