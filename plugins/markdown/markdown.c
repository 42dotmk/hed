/* markdown: editor support for Markdown buffers.
 *
 * Composed of two pieces, each in its own translation unit:
 *
 *   - markdown_highlight.c — per-heading-level palette ramp + inline
 *     emphasis (italic/bold/strike) via custom highlights.scm.
 *
 *   - markdown_fold.c — heading-based fold detector registered as the
 *     "markdown" fold method, plus the filetype default mapping that
 *     auto-applies it to .md buffers.
 *
 * Splitting keeps each file focused and lets either feature be lifted
 * into a standalone plugin if a user wants only one of them.
 */

#include "markdown.h"
#include "markdown_internal.h"
#include <stddef.h>

static int markdown_init(void) {
    md_init_highlights();
    md_init_fold();
    return 0;
}

const Plugin plugin_markdown = {
    .name   = "markdown",
    .desc   = "highlights + heading-based folding for markdown",
    .init   = markdown_init,
    .deinit = NULL,
};
