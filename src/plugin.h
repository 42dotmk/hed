#ifndef HED_PLUGIN_H
#define HED_PLUGIN_H

/* Static plugin interface.
 *
 * A plugin is a self-contained module that registers hooks, commands,
 * and/or keybinds in its init() function. Plugins are linked into the
 * hed binary; "loading" is just calling init().
 *
 * Each plugin lives in src/plugins/<name>/ and exposes a `const Plugin
 * plugin_<name>` symbol via a small header. config.c includes those
 * headers and calls plugin_load(&plugin_<name>, 1) to load + activate.
 *
 * Lifecycle:
 *   plugin_load(p, 1)  — register the plugin and run its init() now.
 *   plugin_load(p, 0)  — register but don't init (loaded-disabled state).
 *   plugin_enable(p)   — run init() if not already; idempotent.
 *   plugin_disable(p)  — run deinit() if non-NULL; idempotent.
 *
 * deinit() is currently a stub for most plugins — hooks/cmds/keybinds
 * have no unregister API yet, so disabling a loaded plugin won't fully
 * tear it down. When that lands, deinit() will be honored.
 */
typedef struct Plugin {
    const char *name;
    const char *desc;
    int (*init)(void);
    void (*deinit)(void);
} Plugin;

/* Load (register) a plugin. If enabled is non-zero, immediately runs
 * init(). Returns 0 on success or whatever init() returned. Idempotent
 * on the (Plugin*) — a second load is a no-op. */
int plugin_load(const Plugin *p, int enabled);

/* Run init() on a loaded plugin if it's not already enabled. If the
 * plugin hasn't been loaded yet, this loads + enables it. Idempotent. */
int plugin_enable(const Plugin *p);

/* Run deinit() if non-NULL and mark inactive. Plugin remains loaded
 * (visible in plugin_list). Idempotent. */
int plugin_disable(const Plugin *p);

/* Print all loaded plugins and their enabled state to status / log. */
void plugin_list(void);

/* Command callback: ":plugins" — calls plugin_list(). */
void cmd_plugins(const char *args);

#endif
