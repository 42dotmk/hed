#ifndef HED_PLUGIN_HED_THEMES_H
#define HED_PLUGIN_HED_THEMES_H
#include "plugin.h"

/* Re-export the runtime theme API so a single include is enough to
 * activate or query themes from elsewhere (e.g. config_init):
 *
 *     #include "hed_themes/hed_themes.h"
 *     ...
 *     plugin_load(&plugin_hed_themes, 1);
 *     theme_activate("gruvbox");
 */
#include "treesitter/theme.h"

extern const Plugin plugin_hed_themes;

#endif
