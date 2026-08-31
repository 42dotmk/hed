#ifndef HED_PLUGIN_SMART_INDENT_H
#define HED_PLUGIN_SMART_INDENT_H
#include "plugin.h"
extern const Plugin plugin_smart_indent;

/* Per-filetype indent rules (last-write-wins; "*" is the fallback).
 * indent_after: chars that, ending a line, add one indent level on the
 * next line. dedent_of: closers that, typed as the first char on a
 * line, remove one level. Call from config_user_init() to customize:
 *   smart_indent_register("lua", "{(", ")}");  */
void smart_indent_register(const char *filetype, const char *indent_after,
                           const char *dedent_of);
#endif
