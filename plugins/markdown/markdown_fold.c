/* markdown_fold: heading-based fold detector.
 *
 * An ATX heading line (`#`, `##`, …, up to six) starts a fold that runs
 * until the next heading of the same or shallower level, or end-of-file.
 * Result: each section collapses into a one-line summary, and nested
 * sub-sections collapse into their parent.
 *
 * Fenced code blocks (``` … ```) are tracked so a `#` inside a code
 * fence doesn't accidentally start a section. Setext headings
 * (text underlined with === or ---) are intentionally not handled —
 * they're rare in the wild, and adding them would require lookahead
 * that complicates the scan with little payoff.
 *
 * Registers itself as the "markdown" fold method via
 * fold_method_register, and binds filetype "markdown" → "markdown" so
 * a freshly opened .md buffer folds automatically. Both registrations
 * are last-write-wins, so users can override either from config.c.
 */

#include "markdown_internal.h"
#include "buf/buffer.h"
#include "buf/row.h"
#include "fold_methods/fold_methods.h"
#include "utils/fold.h"

#include <stdbool.h>
#include <stdlib.h>

/* Strip leading spaces and return the offset of the first non-space byte
 * within the row. Tabs count as one byte (good enough for heading
 * detection — markdown's CommonMark rule allows up to 3 leading spaces
 * before an ATX marker, beyond which it's an indented code block). */
static size_t leading_ws(const Row *row) {
    size_t i = 0;
    while (i < row->chars.len && row->chars.data[i] == ' ')
        i++;
    return i;
}

/* If the row begins with an ATX heading marker (1–6 `#` followed by a
 * space, tab, or end-of-line), return the heading level. Otherwise 0. */
static int atx_heading_level(const Row *row) {
    size_t off = leading_ws(row);
    if (off > 3) /* >3 leading spaces is an indented code block */
        return 0;
    int level = 0;
    while (off + (size_t)level < row->chars.len &&
           row->chars.data[off + level] == '#' && level < 7) {
        level++;
    }
    if (level == 0 || level > 6)
        return 0;
    size_t after = off + (size_t)level;
    if (after == row->chars.len)
        return level; /* "###" with nothing after is still a heading */
    char c = row->chars.data[after];
    return (c == ' ' || c == '\t') ? level : 0;
}

/* True if the row is a fenced-code-block delimiter (``` or ~~~ with no
 * preceding indent worth caring about). Markdown allows either fence
 * character to appear three or more times. */
static bool is_code_fence(const Row *row) {
    size_t off = leading_ws(row);
    if (off > 3)
        return false;
    if (off >= row->chars.len)
        return false;
    char fence = row->chars.data[off];
    if (fence != '`' && fence != '~')
        return false;
    int run = 0;
    while (off + (size_t)run < row->chars.len &&
           row->chars.data[off + run] == fence)
        run++;
    return run >= 3;
}

static void md_detect_folds(Buffer *buf) {
    if (!buf)
        return;

    fold_clear_all(&buf->folds);
    for (int i = 0; i < buf->num_rows; i++) {
        buf->rows[i].fold_start = false;
        buf->rows[i].fold_end = false;
    }
    if (buf->num_rows == 0)
        return;

    /* Stack of open headings: parallel arrays of start-line and level. */
    int *start_lines = malloc(sizeof(int) * 8);
    int *levels      = malloc(sizeof(int) * 8);
    int  cap = 8, depth = 0;
    if (!start_lines || !levels) {
        free(start_lines);
        free(levels);
        return;
    }

    bool in_fence = false;

    for (int line = 0; line < buf->num_rows; line++) {
        Row *row = &buf->rows[line];

        if (is_code_fence(row)) {
            in_fence = !in_fence;
            continue;
        }
        if (in_fence)
            continue;

        int level = atx_heading_level(row);
        if (level == 0)
            continue;

        /* Close any open headings of equal or deeper level. They end on
         * the line before this one. */
        while (depth > 0 && levels[depth - 1] >= level) {
            int start = start_lines[depth - 1];
            int end   = line - 1;
            if (end > start) {
                buf->rows[start].fold_start = true;
                buf->rows[end].fold_end     = true;
                fold_add_region(&buf->folds, start, end);
            }
            depth--;
        }

        if (depth >= cap) {
            cap *= 2;
            int *ns = realloc(start_lines, sizeof(int) * cap);
            int *nl = realloc(levels, sizeof(int) * cap);
            if (!ns || !nl) {
                free(ns ? ns : start_lines);
                free(nl ? nl : levels);
                return;
            }
            start_lines = ns;
            levels      = nl;
        }
        start_lines[depth] = line;
        levels[depth]      = level;
        depth++;
    }

    /* Flush whatever's still open — those sections run to EOF. */
    while (depth > 0) {
        int start = start_lines[depth - 1];
        int end   = buf->num_rows - 1;
        if (end > start) {
            buf->rows[start].fold_start = true;
            buf->rows[end].fold_end     = true;
            fold_add_region(&buf->folds, start, end);
        }
        depth--;
    }

    free(start_lines);
    free(levels);
}

void md_init_fold(void) {
    fold_method_register("markdown", md_detect_folds);
    fold_method_set_default("markdown", "markdown");
}
