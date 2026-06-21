#ifndef HED_PLUGIN_TRANSLATE_H
#define HED_PLUGIN_TRANSLATE_H

#include "plugin.h"

extern const Plugin plugin_translate;

/* Override the default target language used when :translate is called
 * without arguments. Pass an ISO code such as "en", "fr", "mk", or any
 * value translate-shell accepts. */
void translate_set_default_target(const char *lang);

#endif
