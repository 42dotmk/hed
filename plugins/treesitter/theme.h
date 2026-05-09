#ifndef HED_PLUGIN_TS_THEME_H
#define HED_PLUGIN_TS_THEME_H

/* Runtime palette + theme registry for the highlight system.
 *
 * A *palette* is a string-keyed table of named ANSI SGR escape sequences
 * ("string" → "\x1b[38;2;158;206;106m", "title" → "\x1b[1;38;2;192;202;245m",
 * etc.). Highlight roles (see highlight.h) reference palette entries by
 * name; switching themes re-populates the palette and every role's colour
 * follows.
 *
 * A *theme* is a named function that re-populates the palette. Plugins
 * register themes via theme_register and switch with theme_activate, or
 * the user can switch from `:theme <name>`.
 *
 * Palette entries are not cleared between activations: a theme that doesn't
 * set a given key leaves the previous value in place. Themes are expected
 * to set every key they care about.
 *
 * All functions are weak so callers outside the treesitter plugin can guard
 * with `if (&theme_palette_set) ...` and no-op when WITH_TREESITTER=0.
 */

typedef void (*theme_apply_fn)(void);

/* Palette: name → SGR. Borrowed pointer; caller's string must outlive the
 * registry (typically a static const char* or a COLOR_* macro). */
int theme_palette_set(const char *name, const char *sgr) __attribute__((weak));
const char *theme_palette_get(const char *name) __attribute__((weak));

/* Theme registry: name → apply_fn. apply_fn is called from theme_activate
 * and is expected to issue theme_palette_set calls for every entry it owns. */
int theme_register(const char *name, theme_apply_fn apply) __attribute__((weak));
int theme_activate(const char *name) __attribute__((weak));
const char *theme_active_name(void) __attribute__((weak));

/* List registered theme names, NULL-terminated. The returned array is
 * owned by the registry; do not free. */
const char *const *theme_list(void) __attribute__((weak));

#endif
