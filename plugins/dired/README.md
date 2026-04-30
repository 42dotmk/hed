# dired

Activates the directory browser (oil.nvim-like). The implementation lives in
`src/dired.{c,h}` because core code (`buf/buffer.c`, `commands/commands_buffer.c`)
calls into it when a directory path is opened. This plugin owns just the
activation layer: keybinds + lifecycle hooks.

## What gets activated

- `dired_hooks_init()` — buffer lifecycle + keypress hooks scoped to the
  `dired` filetype (close/cleanup, scroll behavior, etc).
- Normal-mode keybinds in dired buffers:
  - `<CR>` — open file or descend into directory
  - `-` — go to parent directory
  - `~` — return to original (origin) directory
  - `cd` — chdir to current dired path

## Notes

- Opening a directory (`:e .`, `:e /path/to/dir`, `./build/hed .`) is handled
  by the core editor, not this plugin. The plugin only wires up navigation
  once the dired buffer exists.
- The `<CR>`, `-`, `~`, `cd` keybinds are global — they only do anything
  meaningful when the current buffer's filetype is `dired`. The implementation
  early-returns otherwise.

## Enable

In `src/config.c`'s `user_hooks_init()`:

```c
plugin_enable("dired");
```

Disable by removing that line. Without the plugin, opening a directory still
loads a dired buffer (core behavior), but you'll have no keybinds to navigate
inside it.
