#ifndef HED_PLUGIN_TS_HIGHLIGHT_H
#define HED_PLUGIN_TS_HIGHLIGHT_H

#include <stdint.h>

/* Highlight role registry.
 *
 * Maps tree-sitter capture names (e.g., "string", "string.special",
 * "markup.heading.1") to one of:
 *   - A raw ANSI SGR escape sequence (must start with ESC, 0x1b).
 *   - A palette entry name (anything else); resolved through
 *     theme_palette_get at lookup time.
 *
 * Palette refs are the right choice when you want the colour to follow
 * the active theme:
 *
 *     highlight_set("markup.heading.1", "title");   // theme-driven
 *     highlight_set("text.emphasis",    "\x1b[3m"); // raw, theme-independent
 *
 * Lookup walks the dotted name from most-specific to least, so a query
 * emitting @markup.heading.1 falls back to "markup.heading", then
 * "markup", until a registered entry is found.
 *
 * Last-write-wins. The role-value pointer is borrowed; the caller's string
 * must outlive the registry (typically a static const char*, a COLOR_*
 * macro, or a palette entry name string literal).
 *
 * Functions are declared weak so plugins can guard their calls with
 * `if (&highlight_set) ...` and gracefully no-op when hed is built with
 * WITH_TREESITTER=0.
 */

int highlight_set(const char *role, const char *value) __attribute__((weak));
const char *highlight_lookup(const char *capture, uint32_t len)
    __attribute__((weak));

#endif
