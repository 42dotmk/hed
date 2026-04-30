# keymap

Runtime swap between `vim_keybinds` and `emacs_keybinds`.

## Commands

- `:keymap` — print the active keymap
- `:keymap vim` / `:keymap emacs` — switch
- `:keymap-toggle` — flip between the two

## How it works

- Both keymap plugins must be loaded by `config.c` (typically vim active
  at boot, emacs loaded but disabled).
- `:keymap <name>` re-runs the target plugin's `init()` directly, which
  re-registers all its keybinds. Last-write-wins resolves any overlap.
- For emacs, also flips `emacs_keybinds_set_modeless(1)` so the
  always-insert hook starts firing; flips it off when switching back to vim.

## Enable

In `src/config.c`'s `config_init()`:

```c
plugin_load(&plugin_vim_keybinds,   1);
plugin_load(&plugin_emacs_keybinds, 0);
plugin_load(&plugin_keymap,         1);
```
