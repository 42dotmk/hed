#ifndef HED_PLUGIN_FOLDS_H
#define HED_PLUGIN_FOLDS_H

#include "plugin.h"

/*
 * folds — register the "bracket" and "indent" fold methods and bind
 * them to sensible filetype defaults. Lifted from core (was
 * src/fold_methods/) so the registry stays minimal and the methods
 * can be swapped, extended, or replaced without touching core.
 *
 * On load: registers fold_method "bracket" for { } scanning and
 * "indent" for indentation-level scanning, then sets per-filetype
 * defaults (c, cpp, javascript, typescript, rust, java, go → bracket;
 * python, shell → indent). User config can override via
 * fold_method_set_default or :foldmethod.
 */

extern const Plugin plugin_folds;

#endif /* HED_PLUGIN_FOLDS_H */
