/* markdown_highlight: per-heading-level palette ramp + inline emphasis.
 *
 * Two pieces:
 *
 *   1. A custom highlights.scm registered via ts_register_query that emits
 *      @markup.heading.1 … @markup.heading.6 instead of the bundled
 *      grammar's flat @text.title — so each heading level can be coloured
 *      independently.
 *
 *   2. Highlight role registrations that map the new captures (and the
 *      grammar's existing emphasis/strong captures) to either palette
 *      tokens or raw SGRs.
 *
 * Demonstrates both highlight_set modes:
 *   - palette refs ("heading.h1", …) — follow the active theme
 *   - raw SGRs (italic, bold, strikethrough) — typographic effects that
 *     should be theme-independent
 */

#include "hed.h"
#include "markdown_internal.h"
#include "treesitter/highlight.h"
#include "treesitter/theme.h"
#include "treesitter/ts.h"

/* Per-heading-level colour ramp. Bold + a colour graded from accent to
 * muted, mirroring the visual weight a reader expects from rendered
 * markdown. Themes can replace these palette entries to re-tone the ramp. */
#define MD_H1 "\x1b[1;38;2;247;118;142m"  /* bold magenta-red */
#define MD_H2 "\x1b[1;38;2;255;158;100m"  /* bold orange */
#define MD_H3 "\x1b[1;38;2;224;175;104m"  /* bold amber */
#define MD_H4 "\x1b[1;38;2;158;206;106m"  /* bold green */
#define MD_H5 "\x1b[1;38;2;122;162;247m"  /* bold blue */
#define MD_H6 "\x1b[1;38;2;187;154;247m"  /* bold purple */

/* Inline emphasis: italic + accent over the keyword colour. */
#define MD_EMPHASIS "\x1b[3;38;2;187;154;247m"  /* italic + purple */
#define MD_STRONG   "\x1b[1;38;2;255;158;100m"  /* bold + orange */
#define MD_STRIKE   "\x1b[9;38;2;86;95;137m"    /* strikethrough + comment */

/* Custom highlights.scm. Distinguishes heading levels by inspecting which
 * h<n>_marker child the heading contains. The bundled query (used by
 * neovim) emits a flat @text.title — we override here so per-level palette
 * entries actually fire. Other capture rules mirror the bundled queries
 * so we don't regress code blocks, links, list markers, etc. */
static const char MARKDOWN_HIGHLIGHTS_SCM[] =
    "(atx_heading (atx_h1_marker) (inline) @markup.heading.1)\n"
    "(atx_heading (atx_h2_marker) (inline) @markup.heading.2)\n"
    "(atx_heading (atx_h3_marker) (inline) @markup.heading.3)\n"
    "(atx_heading (atx_h4_marker) (inline) @markup.heading.4)\n"
    "(atx_heading (atx_h5_marker) (inline) @markup.heading.5)\n"
    "(atx_heading (atx_h6_marker) (inline) @markup.heading.6)\n"
    "\n"
    "(setext_heading (paragraph) @markup.heading.1 (setext_h1_underline))\n"
    "(setext_heading (paragraph) @markup.heading.2 (setext_h2_underline))\n"
    "\n"
    "[\n"
    "  (atx_h1_marker)\n"
    "  (atx_h2_marker)\n"
    "  (atx_h3_marker)\n"
    "  (atx_h4_marker)\n"
    "  (atx_h5_marker)\n"
    "  (atx_h6_marker)\n"
    "  (setext_h1_underline)\n"
    "  (setext_h2_underline)\n"
    "] @punctuation.special\n"
    "\n"
    "[\n"
    "  (link_title)\n"
    "  (indented_code_block)\n"
    "  (fenced_code_block)\n"
    "] @text.literal\n"
    "\n"
    "(fenced_code_block_delimiter) @punctuation.delimiter\n"
    "(code_fence_content) @none\n"
    "(link_destination) @text.uri\n"
    "(link_label) @text.reference\n"
    "\n"
    "[\n"
    "  (list_marker_plus)\n"
    "  (list_marker_minus)\n"
    "  (list_marker_star)\n"
    "  (list_marker_dot)\n"
    "  (list_marker_parenthesis)\n"
    "  (thematic_break)\n"
    "] @punctuation.special\n"
    "\n"
    "[\n"
    "  (block_continuation)\n"
    "  (block_quote_marker)\n"
    "] @punctuation.special\n"
    "\n"
    "(backslash_escape) @string.escape\n";

void md_init_highlights(void) {
    if (!&theme_palette_set || !&highlight_set || !&ts_register_query) {
        log_msg("markdown: highlight API unavailable (treesitter not built?)");
        return;
    }

    /* Per-level heading palette. Themes can override individual levels
     * (e.g. a gruvbox plugin could call theme_palette_set("heading.h1", …))
     * to re-tone the ramp without touching this plugin. */
    theme_palette_set("heading.h1", MD_H1);
    theme_palette_set("heading.h2", MD_H2);
    theme_palette_set("heading.h3", MD_H3);
    theme_palette_set("heading.h4", MD_H4);
    theme_palette_set("heading.h5", MD_H5);
    theme_palette_set("heading.h6", MD_H6);

    /* Route per-level captures through the palette so theme swaps re-tone
     * the heading ramp automatically. */
    highlight_set("markup.heading.1", "heading.h1");
    highlight_set("markup.heading.2", "heading.h2");
    highlight_set("markup.heading.3", "heading.h3");
    highlight_set("markup.heading.4", "heading.h4");
    highlight_set("markup.heading.5", "heading.h5");
    highlight_set("markup.heading.6", "heading.h6");

    /* Inline typography from the markdown-inline grammar. Raw SGRs because
     * italic/bold/strike are decorations a reader recognises regardless of
     * theme. */
    highlight_set("text.emphasis", MD_EMPHASIS);
    highlight_set("text.strong",   MD_STRONG);
    highlight_set("text.strike",   MD_STRIKE);

    /* Plug the per-level queries into the treesitter loader as a fallback,
     * so they apply even when the user hasn't installed a queries override
     * under ~/.config/hed/ts/queries/markdown/. */
    ts_register_query("markdown", "highlights.scm", MARKDOWN_HIGHLIGHTS_SCM);
}
