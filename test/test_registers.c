#include "../src/input/registers.h"
#include "unity/unity.h"
#include <string.h>

void setUp(void) { regs_init(); }
void tearDown(void) { regs_free(); }

#define ASSERT_TRUE(c) TEST_ASSERT_TRUE_MESSAGE((c), #c)
#define ASSERT_NULL(p) TEST_ASSERT_TRUE_MESSAGE((p) == NULL, #p " != NULL")
#define ASSERT_EQ_INT(e, a) TEST_ASSERT_TRUE_MESSAGE((e) == (a), #e " != " #a)

static void assert_sb(const StrBuf *r, const char *want, const char *what) {
    TEST_ASSERT_NOT_NULL_MESSAGE(r, what);
    char got[256];
    size_t n = r->len < sizeof(got) - 1 ? r->len : sizeof(got) - 1;
    memcpy(got, r->data ? r->data : "", n);
    got[n] = '\0';
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, what);
}

static void assert_reg(char name, const char *want) {
    assert_sb(regs_get(name), want, "register");
}

static void assert_part(int idx, int n, const char *want) {
    assert_sb(regs_get_part('"', idx, n), want, "part");
}

void test_plain_write_has_no_parts(void) {
    regs_set_yank("foo", 3);
    assert_reg('"', "foo");
    ASSERT_NULL(regs_get_part('"', 0, 1));
}

void test_batch_yank_joins_and_keeps_parts(void) {
    regs_batch_begin(3);
    /* Replay order is not rank order: active first, then descending. */
    regs_batch_slot(1);
    regs_set_yank("bb", 2);
    regs_batch_slot(2);
    regs_set_yank("ccc", 3);
    regs_batch_slot(0);
    regs_set_yank("a", 1);
    ASSERT_TRUE(regs_batch_active());
    /* Nothing landed yet. */
    ASSERT_EQ_INT(0, (int)regs_get('"')->len);

    ASSERT_EQ_INT(1, regs_batch_end());
    ASSERT_TRUE(!(regs_batch_active()));
    assert_reg('"', "a\nbb\nccc");
    assert_reg('0', "a\nbb\nccc");
    ASSERT_EQ_INT(REG_CHARWISE, regs_get_type('"'));
    assert_part(0, 3, "a");
    assert_part(1, 3, "bb");
    assert_part(2, 3, "ccc");
}

void test_part_count_must_match(void) {
    regs_batch_begin(2);
    regs_batch_slot(0);
    regs_set_yank("a", 1);
    regs_batch_slot(1);
    regs_set_yank("b", 1);
    regs_batch_end();
    ASSERT_NULL(regs_get_part('"', 0, 1));
    ASSERT_NULL(regs_get_part('"', 0, 3));
    ASSERT_NULL(regs_get_part('"', 2, 2));
    ASSERT_NULL(regs_get_part('0', 0, 2));
    assert_part(1, 2, "b");
}

void test_unwritten_slot_is_empty_part(void) {
    regs_batch_begin(2);
    regs_batch_slot(1);
    regs_set_yank("b", 1);
    regs_batch_end();
    assert_reg('"', "\nb");
    assert_part(0, 2, "");
    assert_part(1, 2, "b");
}

void test_batch_delete_rotates_once(void) {
    regs_set_yank("keep", 4);
    regs_push_delete("old", 3);
    regs_batch_begin(2);
    regs_batch_slot(0);
    regs_push_delete("x", 1);
    regs_batch_slot(1);
    regs_push_delete("y", 1);
    /* A delete batch does not owe a HOOK_YANK. */
    ASSERT_EQ_INT(0, regs_batch_end());
    assert_reg('"', "x\ny");
    assert_reg('1', "x\ny");
    assert_reg('2', "old");
    ASSERT_EQ_INT(0, (int)regs_get('3')->len);
    assert_reg('0', "keep");
    assert_part(0, 2, "x");
    assert_part(1, 2, "y");
}

void test_linewise_parts_join_as_lines(void) {
    regs_batch_begin(2);
    regs_batch_slot(0);
    regs_set_yank_typed("one\n", 4, REG_LINEWISE);
    regs_batch_slot(1);
    regs_set_yank_typed("two", 3, REG_LINEWISE); /* dd-style, no newline */
    regs_batch_end();
    assert_reg('"', "one\ntwo\n");
    ASSERT_EQ_INT(REG_LINEWISE, regs_get_type('"'));
}

void test_empty_batch_is_noop(void) {
    regs_set_yank("foo", 3);
    regs_batch_begin(3);
    regs_batch_slot(2);
    ASSERT_EQ_INT(0, regs_batch_end());
    assert_reg('"', "foo");
    ASSERT_NULL(regs_get_part('"', 0, 3));
}

void test_later_plain_write_drops_parts(void) {
    regs_batch_begin(2);
    regs_batch_slot(0);
    regs_set_yank("a", 1);
    regs_batch_slot(1);
    regs_set_yank("b", 1);
    regs_batch_end();
    assert_part(0, 2, "a");
    regs_set_yank("single", 6);
    ASSERT_NULL(regs_get_part('"', 0, 2));
    assert_reg('"', "single");
}

void test_non_batched_writes_apply_during_batch(void) {
    regs_batch_begin(2);
    regs_batch_slot(0);
    regs_set_yank("a", 1);
    regs_set_unnamed("direct", 6); /* not a yank/delete: goes straight in */
    assert_reg('"', "direct");
    regs_batch_slot(1);
    regs_set_yank("b", 1);
    regs_batch_end();
    assert_reg('"', "a\nb");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_plain_write_has_no_parts);
    RUN_TEST(test_batch_yank_joins_and_keeps_parts);
    RUN_TEST(test_part_count_must_match);
    RUN_TEST(test_unwritten_slot_is_empty_part);
    RUN_TEST(test_batch_delete_rotates_once);
    RUN_TEST(test_linewise_parts_join_as_lines);
    RUN_TEST(test_empty_batch_is_noop);
    RUN_TEST(test_later_plain_write_drops_parts);
    RUN_TEST(test_non_batched_writes_apply_during_batch);
    return UNITY_END();
}
