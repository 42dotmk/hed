/* tokyo_night: the bundled "Tokyo Night" palette as a swappable theme.
 *
 * Mirrors src/lib/theme.h, but lives in the runtime palette so other
 * plugins (markdown, lsp diagnostics, …) can pick up colours by name and
 * follow theme switches automatically.
 *
 * Activates itself on init. To make it the default, load it before any
 * plugin that depends on palette entries:
 *
 *     plugin_load(&plugin_tokyo_night, 1);
 *     plugin_load(&plugin_treesitter,  1);
 *     plugin_load(&plugin_markdown,    1);
 */

#include "hed.h"
#include "treesitter/theme.h"

#define TN_RGBC(r, g, b) "\x1b[38;2;" #r ";" #g ";" #b "m"

/* Core palette (Tokyo Night Storm). */
#define TN_FG          TN_RGBC(192, 202, 245)
#define TN_STRING      TN_RGBC(158, 206, 106)
#define TN_COMMENT     TN_RGBC(86,  95,  137)
#define TN_CONSTANT    TN_RGBC(224, 175, 104)
#define TN_NUMBER      TN_RGBC(255, 158, 100)
#define TN_KEYWORD     TN_RGBC(187, 154, 247)
#define TN_TYPE        TN_RGBC(42,  195, 222)
#define TN_FUNCTION    TN_RGBC(122, 162, 247)
#define TN_LABEL       TN_RGBC(224, 108, 117)
#define TN_OPERATOR    TN_RGBC(144, 153, 174)
#define TN_DELIMITER   TN_RGBC(65,  72,  104)
#define TN_DIAG_ERROR  TN_RGBC(247, 118, 142)
#define TN_DIAG_WARN   TN_RGBC(224, 175, 104)
#define TN_DIAG_NOTE   TN_RGBC(122, 162, 247)
#define TN_TITLE       "\x1b[1;38;2;192;202;245m"  /* bold + fg */
#define TN_URI         "\x1b[4;38;2;122;162;247m"  /* underline + blue */

static void apply_tokyo_night(void) {
    if (!&theme_palette_set)
        return;
    theme_palette_set("string",      TN_STRING);
    theme_palette_set("comment",     TN_COMMENT);
    theme_palette_set("variable",    TN_FG);
    theme_palette_set("constant",    TN_CONSTANT);
    theme_palette_set("number",      TN_NUMBER);
    theme_palette_set("keyword",     TN_KEYWORD);
    theme_palette_set("type",        TN_TYPE);
    theme_palette_set("function",    TN_FUNCTION);
    theme_palette_set("attribute",   TN_KEYWORD);
    theme_palette_set("label",       TN_LABEL);
    theme_palette_set("operator",    TN_OPERATOR);
    theme_palette_set("punctuation", TN_DELIMITER);
    theme_palette_set("title",       TN_TITLE);
    theme_palette_set("uri",         TN_URI);
    theme_palette_set("diag.error",  TN_DIAG_ERROR);
    theme_palette_set("diag.warn",   TN_DIAG_WARN);
    theme_palette_set("diag.note",   TN_DIAG_NOTE);
}

static int tokyo_night_init(void) {
    if (!&theme_register || !&theme_activate) {
        log_msg("tokyo_night: theme API unavailable (treesitter not built?)");
        return 0;
    }
    theme_register("tokyo-night", apply_tokyo_night);
    theme_activate("tokyo-night");
    return 0;
}

const Plugin plugin_tokyo_night = {
    .name   = "tokyo_night",
    .desc   = "Tokyo Night palette as a runtime-swappable theme",
    .init   = tokyo_night_init,
    .deinit = NULL,
};
