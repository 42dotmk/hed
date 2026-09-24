#include "../src/utils/fold.h"
#include "unity/unity.h"
#include <stdio.h>

/* The vendored Unity has only the _MESSAGE assert; these are the rest. */
#define ASSERT_TRUE(c) TEST_ASSERT_TRUE_MESSAGE((c), #c)
#define ASSERT_FALSE(c) TEST_ASSERT_TRUE_MESSAGE(!(c), "!(" #c ")")
#define ASSERT_SAME(e, a, msg) TEST_ASSERT_TRUE_MESSAGE((e) == (a), msg)

static FoldList L;

void setUp(void) { fold_list_init(&L); }
void tearDown(void) { fold_list_free(&L); }

/* The hidden-line answer the cache must reproduce: inside a collapsed
 * region and not on its start line. */
static bool hidden_brute(const FoldList *l, int line) {
    for (int i = 0; i < l->count; i++) {
        const FoldRegion *r = &l->regions[i];
        if (r->is_collapsed && line > r->start_line && line <= r->end_line)
            return true;
    }
    return false;
}

static void assert_hidden_matches(int lines) {
    for (int y = -1; y < lines; y++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "line %d", y);
        ASSERT_SAME(hidden_brute(&L, y), fold_is_line_hidden(&L, y), msg);
    }
}

void test_nothing_hidden_without_collapsed_folds(void) {
    fold_add_region(&L, 2, 5);
    fold_add_region(&L, 7, 9);
    assert_hidden_matches(12);
    ASSERT_FALSE(fold_is_line_hidden(&L, 3));
}

void test_collapsed_region_hides_all_but_start(void) {
    fold_add_region(&L, 2, 5);
    fold_collapse_at_line(&L, 2);
    ASSERT_FALSE(fold_is_line_hidden(&L, 2));
    ASSERT_TRUE(fold_is_line_hidden(&L, 3));
    ASSERT_TRUE(fold_is_line_hidden(&L, 5));
    ASSERT_FALSE(fold_is_line_hidden(&L, 6));
}

void test_overlapping_and_adjacent_spans_merge(void) {
    fold_add_region(&L, 1, 10);
    fold_add_region(&L, 3, 4);
    fold_add_region(&L, 12, 13);
    fold_add_region(&L, 13, 16);
    fold_add_region(&L, 20, 20); /* one line: hides nothing */
    for (int i = 0; i < L.count; i++)
        fold_set_collapsed(&L, i, true);
    assert_hidden_matches(24);
}

void test_changes_invalidate_the_cache(void) {
    fold_add_region(&L, 2, 5);
    fold_collapse_at_line(&L, 2);
    ASSERT_TRUE(fold_is_line_hidden(&L, 4));
    fold_expand_at_line(&L, 2);
    ASSERT_FALSE(fold_is_line_hidden(&L, 4));
    fold_toggle_at_line(&L, 2);
    ASSERT_TRUE(fold_is_line_hidden(&L, 4));
    fold_remove_region(&L, 0);
    ASSERT_FALSE(fold_is_line_hidden(&L, 4));
    fold_add_region(&L, 2, 5);
    fold_set_collapsed(&L, 0, true);
    fold_clear_all(&L);
    ASSERT_FALSE(fold_is_line_hidden(&L, 4));
}

/* Level by definition: one more than the regions strictly containing
 * it (an identical span doesn't count). */
static int level_brute(const FoldList *l, int idx) {
    const FoldRegion *b = &l->regions[idx];
    int level = 1;
    for (int j = 0; j < l->count; j++) {
        const FoldRegion *a = &l->regions[j];
        if (j != idx && a->start_line <= b->start_line &&
            a->end_line >= b->end_line &&
            (a->start_line != b->start_line || a->end_line != b->end_line))
            level++;
    }
    return level;
}

void test_apply_level_matches_nesting(void) {
    /* Added out of order, with a duplicate span and shared starts. */
    fold_add_region(&L, 12, 15);
    fold_add_region(&L, 3, 4);
    fold_add_region(&L, 0, 10);
    fold_add_region(&L, 2, 5);
    fold_add_region(&L, 2, 5);
    fold_add_region(&L, 2, 3);
    fold_add_region(&L, 13, 14);
    for (int level = 0; level <= 4; level++) {
        fold_apply_level(&L, level);
        for (int i = 0; i < L.count; i++) {
            char msg[48];
            snprintf(msg, sizeof(msg), "level %d region %d", level, i);
            ASSERT_SAME(level_brute(&L, i) > level, L.regions[i].is_collapsed,
                        msg);
        }
        assert_hidden_matches(18);
    }
}

/* Folds the way detect_brackets makes them: a random brace stream over
 * lines, each close pairing with the last open — so regions nest, and
 * `{{`/`}}` on the same lines produce duplicate spans. */
void test_apply_level_matches_nesting_random(void) {
    unsigned seed = 12345;
    for (int round = 0; round < 50; round++) {
        fold_clear_all(&L);
        int stack[64], sp = 0;
        for (int line = 0; line < 120; line++) {
            for (int k = 0; k < 3; k++) {
                seed = seed * 1103515245u + 12345u;
                int op = (int)(seed >> 16) % 4;
                if (op == 0 && sp < 64)
                    stack[sp++] = line;
                else if (op == 1 && sp > 0 && stack[sp - 1] < line)
                    fold_add_region(&L, stack[--sp], line);
            }
        }
        for (int level = 0; level <= 6; level++) {
            fold_apply_level(&L, level);
            for (int i = 0; i < L.count; i++)
                ASSERT_SAME(level_brute(&L, i) > level,
                            L.regions[i].is_collapsed, "random level");
            assert_hidden_matches(122);
        }
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_hidden_without_collapsed_folds);
    RUN_TEST(test_collapsed_region_hides_all_but_start);
    RUN_TEST(test_overlapping_and_adjacent_spans_merge);
    RUN_TEST(test_changes_invalidate_the_cache);
    RUN_TEST(test_apply_level_matches_nesting);
    RUN_TEST(test_apply_level_matches_nesting_random);
    return UNITY_END();
}
