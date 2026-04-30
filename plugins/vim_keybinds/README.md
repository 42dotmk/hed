# vim_keybinds

Ships the default Vim-style modal keymap for hed: hjkl motion, operator
keybinds (`d`, `c`, `y`), text-object dispatch, the leader (`<space>`)
cluster, fold bindings (`za`, `zo`, `zM`, ...), and insert-mode helpers.

This plugin is what makes hed feel like Vim out of the box. Disabling it
gives you a blank-canvas editor — useful as a starting point if you want a
different keymap profile (emacs-style, modeless, etc).

## Override semantics

Bindings registered here are overridable. The keybind subsystem uses
**last-write-wins** for any matching `(mode, sequence, filetype)` tuple
(see `remove_duplicate` in `src/keybinds.c`).

Inside `config_init()` (in `src/config.c`), the order is:

1. `vim_keybinds` runs first (ships defaults).
2. Other plugins (`clipboard`, `dired`) register after, overriding select
   bindings (`y`/`yy` for clipboard, `<CR>`/`-`/`~`/`cd` for dired).
3. The user's personal `load_keybinds()` step runs last — anything declared
   there wins.

So to override a default, just declare it later in `config_init`:

```c
mapn("j", my_custom_down, "scroll-then-down");
```

## What this plugin does NOT own

- Yank/clipboard sync — owned by the `clipboard` plugin.
- Dired navigation (`<CR>`, `-`, `~`, `cd`) — owned by the `dired` plugin.
- Filetype-scoped bindings — those go in the relevant feature plugin.

## Text objects

Text objects (`iw`, `ap`, `i(`, hjkl motions, etc.) are registered here too —
they're the operator-composition counterpart of the keybinds and conceptually
part of the same "Vim defaults" surface.

## Enable

In `src/config.c`'s `user_hooks_init()`:

```c
plugin_enable("vim_keybinds");
```

Should be enabled first among the keybind-owning plugins so others can
override its defaults.
