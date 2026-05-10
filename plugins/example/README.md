← [hed](../../readme.md)

# example

The starter template. Copy this directory, rename, and you have a
plugin.

## Recipe

```bash
cp -r plugins/example plugins/myplugin
cd plugins/myplugin
mv example.c myplugin.c
mv example.h myplugin.h
# inside both files, replace `example` → `myplugin`,
# `plugin_example` → `plugin_myplugin`
```

Then in `src/config.c`:

```c
#include "myplugin/myplugin.h"
...
plugin_load(&plugin_myplugin, 1);
```

`make` picks up the new `.c` automatically — the makefile globs
`*.c` recursively under `plugins/`.

## What the template demonstrates

- The minimum surface: an `init` function and a `Plugin` descriptor.
- Registering a `:` command (`:hello`).
- Registering a NORMAL-mode keybind (`<space>eh`).
- Registering a hook (`HOOK_MODE_CHANGE`).

That's it. Three patterns, ~50 lines.

## Plugin contract

Every plugin exposes:

```c
typedef struct Plugin {
    const char *name, *desc;
    int  (*init)(void);
    void (*deinit)(void);
} Plugin;

extern const Plugin plugin_myplugin;
```

`plugin_load(&plugin_myplugin, 1)` registers the plugin and runs its
`init()`. Pass `0` to register without enabling — useful for keymaps
that get swapped to at runtime via `:keymap`.

`init()` returns `0` on success. Inside it, register your commands
with `cmd(...)`, your keybinds with `mapn(...)`/`mapi(...)`/etc., and
your hooks with `hook_register_*(...)`. All of those macros are in
`hed.h`, which is the only include a plugin should normally need.

## Out-of-tree plugins

Keep a plugin set outside the hed repo and point the build at it:

```bash
make PLUGINS_DIR=$HOME/my-hed-plugins
```

Headers from out-of-tree plugins are reachable as
`#include "myplugin/myplugin.h"` because the makefile passes
`-I$(PLUGINS_DIR)`.

## What `init` should *not* do

- Don't read configuration files at init time. Plugins are loaded
  before `main()` finishes setup; the file system is fine but the
  rendering subsystem is not.
- Don't print to stdout — it goes to the terminal mid-render. Use
  `log_msg(...)` (writes to the log file) or
  `ed_set_status_message(...)` (writes to the status line).
- Don't keep the editor busy. `init()` is synchronous; if you need to
  do something heavy, spawn a thread or defer to a hook (e.g.,
  `HOOK_STARTUP_DONE`).
