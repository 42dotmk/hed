#ifndef HED_PLUGIN_TREESITTER_H
#define HED_PLUGIN_TREESITTER_H
#include "plugin.h"

/* Declared weak: when the treesitter plugin isn't compiled in (e.g.
 * `make WITH_TREESITTER=0`), &plugin_treesitter resolves to NULL and
 * plugin_load() gracefully no-ops. */
extern const Plugin plugin_treesitter __attribute__((weak));

#endif
