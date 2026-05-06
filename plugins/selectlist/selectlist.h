#ifndef HED_PLUGIN_SELECTLIST_H
#define HED_PLUGIN_SELECTLIST_H

#include "plugin.h"
#include "ui/winmodal.h"

extern const Plugin plugin_selectlist;

typedef void (*SelectListCallback)(int index, const char *item, void *user);

/* Open a SelectList at a fixed position. Pass x=-1, y=-1 to center.
 * Returns 0 on success, -1 on failure. Closes any existing list first. */
int selectlist_open(int x, int y, int width, int height,
                    const char *const *items, int count,
                    SelectListCallback cb, void *user);

/* Open a SelectList anchored at a screen cell (1-based). */
int selectlist_open_anchored(int anchor_x, int anchor_y, int width,
                             const char *const *items, int count,
                             WModalAnchor prefer,
                             SelectListCallback cb, void *user);

#endif /* HED_PLUGIN_SELECTLIST_H */
