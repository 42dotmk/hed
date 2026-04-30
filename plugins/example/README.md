# example

A minimal plugin you can copy as a starting template. Demonstrates the
three things plugins typically do: register a command, register a
keybind, register a hook.

Not enabled by default. Add to `src/config.c`'s `config_init()` to try it:

```c
#include "plugins/example/example.h"
...
plugin_load(&plugin_example, 1);
```

Then `:hello world` or `<space>eh` in normal mode.

## Recipe — making your own plugin

1. **Copy the directory.**
   ```sh
   cp -r src/plugins/example src/plugins/myplugin
   ```

2. **Rename the symbol and files.** In `myplugin/myplugin.c` and
   `myplugin/myplugin.h`, replace every `example`/`plugin_example` with
   your plugin's name. The `Plugin.name` string is what shows up in
   `:plugins`.

3. **Implement.** Add commands, keybinds, hooks inside the `_init`
   function. See the existing plugins (`auto_pair`, `clipboard`, `dired`,
   `tmux`) for patterns.

4. **Register.** In `src/config.c`:
   ```c
   #include "plugins/myplugin/myplugin.h"
   ...
   plugin_load(&plugin_myplugin, 1);
   ```

5. **Build.** `make` picks it up automatically — the Makefile globs
   `*.c` under `$(PLUGINS_DIR)`.

## Out-of-tree plugins

You can keep your plugins outside the hed repo and point the build at
that directory:

```sh
make PLUGINS_DIR=$HOME/my-hed-plugins
```

Note: `config.c` includes plugin headers via path — out-of-tree plugins
need their includes to resolve. The Makefile passes `-I$(PLUGINS_DIR)`
so a plugin at `$HOME/my-hed-plugins/myplugin/myplugin.h` is reachable
as `#include "myplugin/myplugin.h"`.

## What every plugin needs

- A `const Plugin plugin_<name>` symbol in a `.c` file.
- A `<name>.h` header exposing that symbol via `extern`.
- An `init` function (returns 0 on success).
- A `deinit` function — optional today (no unregister API yet); set to
  `NULL` if you don't need teardown.

That's it. No hidden registry, no constructor magic, no manifest file.
