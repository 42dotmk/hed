← [hed](../../readme.md)

# keymap

Runtime keymap swap. Without this plugin, you're locked to whichever
keymap plugin was loaded with `enabled=1` at startup. With it, you
can flip between vim, emacs, and vscode bindings on the fly.

## Commands

```
:keymap                 # print the current keymap name
:keymap vim             # switch to vim bindings
:keymap emacs           # switch to emacs bindings
:keymap vscode          # switch to vscode bindings
:keymap-toggle          # cycle: vim → emacs → vscode → vim
```

## How it works

The three keymap plugins (`vim_keybinds`, `emacs_keybinds`,
`vscode_keybinds`) are typically loaded with `plugin_load(..., 0)` —
registered but not enabled. Switching keymaps clears the existing
keybind table and runs the target keymap's `init()`, which
re-registers its bindings.

Keybind dispatch is last-write-wins on `(mode, sequence, filetype)`,
so personal overrides in `src/config.c` (registered after the
keymap's `init()`) survive a swap because they get re-applied each
time. Cross-keymap state like macros and registers persists.

## Notes

Modeless behavior switches with the keymap: emacs and vscode set
modeless (always-INSERT), vim clears it (mode-aware). If you don't
want that, edit `src/config.c` to call `ed_set_modeless(...)` after
your `:keymap` calls.

`<space>tk` is the default leader binding for `:keymap-toggle`.
