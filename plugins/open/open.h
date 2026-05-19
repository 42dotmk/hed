#ifndef HED_PLUGIN_OPEN_H
#define HED_PLUGIN_OPEN_H
#include "plugin.h"
extern const Plugin plugin_open;

/* Hand `target` (a path or URL) to the system's default application
 * (xdg-open on Linux, `open` on macOS). Fire-and-forget; safe to call
 * from any context. */
void open_path(const char *target);

#endif
