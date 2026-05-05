#include "plugin.h"
#include "lib/log.h"
#include "editor.h"
#include "utils/fzf.h"
#include "commands/cmd_util.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

int plugin_get_count(void) {
    return loaded_count;
}

int plugin_get_at(int index, const char **name, const char **desc,
                  int *enabled) {
    if (index < 0 || index >= loaded_count) return 0;
    const Plugin *p = loaded[index].p;
    if (name)    *name    = p->name ? p->name : "?";
    if (desc)    *desc    = p->desc ? p->desc : "";
    if (enabled) *enabled = loaded[index].enabled;
    return 1;
}

/* :plugins — fzf picker over loaded plugins. Each row shows status +
 * name; the description appears in the preview pane. The selection
 * is informational only (no action wired to it yet). */
void cmd_plugins(const char *args) {
    (void)args;

    char pipebuf[16384];
    size_t off = 0;
    off += (size_t)snprintf(pipebuf + off, sizeof(pipebuf) - off,
                            "printf '%%s\\t%%s\\n' ");

    int count = plugin_get_count();
    int active = 0;

    for (int i = 0; i < count; i++) {
        const char *name = NULL, *desc = NULL;
        int enabled = 0;
        if (!plugin_get_at(i, &name, &desc, &enabled)) continue;
        if (enabled) active++;

        char display[256];
        snprintf(display, sizeof(display), "[%c] %s",
                 enabled ? 'x' : ' ', name);

        char es[512], ed[512];
        shell_escape_single(display, es, sizeof(es));
        shell_escape_single(desc[0] ? desc : "(no description)", ed,
                            sizeof(ed));

        size_t need = strlen(es) + 1 + strlen(ed) + 2;
        if (off + need >= sizeof(pipebuf)) break;

        memcpy(pipebuf + off, es, strlen(es));
        off += strlen(es);
        pipebuf[off++] = ' ';
        memcpy(pipebuf + off, ed, strlen(ed));
        off += strlen(ed);
        pipebuf[off++] = ' ';
    }
    pipebuf[off] = '\0';

    /* Show only the status+name in the row; pop the description into
     * the preview pane for the highlighted entry. */
    const char *fzf_opts =
        "--delimiter '\t' "
        "--with-nth 1 "
        "--preview \"printf '%s' {2}\" "
        "--preview-window 'right:50%:wrap'";

    char **sel = NULL;
    int cnt = 0;
    if (!fzf_run_opts(pipebuf, fzf_opts, 0, &sel, &cnt) || cnt == 0) {
        ed_set_status_message("plugins: %d loaded, %d enabled",
                              count, active);
        fzf_free(sel, cnt);
        return;
    }
    fzf_free(sel, cnt);
    ed_set_status_message("plugins: %d loaded, %d enabled", count, active);
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
