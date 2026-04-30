#include "plugin.h"
#include "hed.h"
#include <stddef.h>

#define MAX_PLUGINS 64

typedef struct {
    const Plugin *p;
    int enabled;
} Slot;

static Slot loaded[MAX_PLUGINS];
static int loaded_count = 0;

static Slot *find_slot(const Plugin *p) {
    for (int i = 0; i < loaded_count; i++)
        if (loaded[i].p == p) return &loaded[i];
    return NULL;
}

int plugin_load(const Plugin *p, int enabled) {
    if (!p) return -1;
    Slot *slot = find_slot(p);
    if (slot) {
        /* Already loaded. If asked to enable now, ensure init has run. */
        if (enabled) return plugin_enable(p);
        return 0;
    }
    if (loaded_count >= MAX_PLUGINS) {
        log_msg("plugin: load list full, cannot register '%s'",
                p->name ? p->name : "?");
        return -1;
    }
    loaded[loaded_count].p = p;
    loaded[loaded_count].enabled = 0;
    loaded_count++;
    log_msg("plugin: loaded '%s'%s",
            p->name ? p->name : "?",
            enabled ? " (enabled)" : " (disabled)");
    if (enabled) return plugin_enable(p);
    return 0;
}

int plugin_enable(const Plugin *p) {
    if (!p) return -1;
    Slot *slot = find_slot(p);
    if (!slot) {
        /* Never loaded — load + enable in one shot. */
        return plugin_load(p, 1);
    }
    if (slot->enabled) return 0;
    int rc = p->init ? p->init() : 0;
    if (rc != 0) {
        log_msg("plugin: '%s' init failed (%d)", p->name ? p->name : "?", rc);
        return rc;
    }
    slot->enabled = 1;
    log_msg("plugin: enabled '%s'", p->name ? p->name : "?");
    return 0;
}

int plugin_disable(const Plugin *p) {
    if (!p) return 0;
    Slot *slot = find_slot(p);
    if (!slot || !slot->enabled) return 0;
    if (p->deinit) p->deinit();
    slot->enabled = 0;
    log_msg("plugin: disabled '%s'", p->name ? p->name : "?");
    return 0;
}

void cmd_plugins(const char *args) {
    (void)args;
    plugin_list();
}

void plugin_list(void) {
    int active = 0;
    for (int i = 0; i < loaded_count; i++)
        if (loaded[i].enabled) active++;
    ed_set_status_message("plugins: %d loaded, %d enabled",
                          loaded_count, active);
    for (int i = 0; i < loaded_count; i++) {
        const Plugin *p = loaded[i].p;
        log_msg("  [%c] %s - %s",
                loaded[i].enabled ? 'x' : ' ',
                p->name ? p->name : "?",
                p->desc ? p->desc : "");
    }
}
