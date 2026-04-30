# vscode_keybinds

VSCode-flavored keymap. Modeless (always-insert) with the standard
Ctrl-key bindings VSCode users expect.

## ⚠️ Caveats

VSCode is a GUI app — it gets a richer set of modifier combinations than
a terminal can deliver. Three things that don't translate cleanly:

1. **Ctrl+Shift+P** (command palette) — most terminals send the same
   bytes for `Ctrl+P` and `Ctrl+Shift+P`, so the palette uses `<M-p>`
   (Alt+P) instead.
2. **Ctrl+/** (toggle comment) — `Ctrl+/` is byte 0x1F, outside the
   Ctrl+letter range our keybind layer encodes. Mapped to `<M-/>` instead.
3. **Ctrl+\\** (split editor) — no Ctrl form available. Mapped to `<M-\\>`.

## Bindings provided

### File / window
| VSCode | hed binding | Action |
|---|---|---|
| `Ctrl+S` | `C-s` | save |
| `Ctrl+N` | `C-n` | new buffer |
| `Ctrl+O` | `C-o` | open file (fzf) |
| `Ctrl+P` | `C-p` | quick open (fzf) |
| `Ctrl+W` | `C-w` | close window |
| `Ctrl+Shift+P` | `M-p` | command palette |
| `Ctrl+\\` | `M-\\` | split editor right |
| (none) | `M--` | split editor down |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | `M-n` / `M-N` | next / prev buffer |

### Edit
| VSCode | hed | Action |
|---|---|---|
| `Ctrl+Z` | `C-z` | undo |
| `Ctrl+Y` | `C-y` | redo |
| `Ctrl+X` | `C-x` | cut line / cut selection |
| `Ctrl+C` | `C-c` | copy line / copy selection |
| `Ctrl+V` | `C-v` | paste |
| `Ctrl+/` | `M-/` | toggle comment |
| `Esc` | `Esc` | cancel selection |

### Find / navigate
| VSCode | hed | Action |
|---|---|---|
| `Ctrl+F` | `C-f` | find in file |
| `Ctrl+G` | `C-g` | search prompt (goto-line stand-in) |
| `Ctrl+D` | `C-d` | select next occurrence |
| `Home` / `End` | `Home` / `End` | beginning / end of line |

## Conflicts with vim_keybinds

Both want `<C-d>` (vim: scroll), `<C-n>` / `<C-p>` (vim: quickfix nav),
`<C-o>` (vim: jump back), `<C-v>` (vim: visual-block). Don't enable both;
swap via the `keymap` plugin (or just edit `config.c`).

## Enable

In `src/config.c`'s `config_init()`:

```c
plugin_load(&plugin_vscode_keybinds, 1);
// remove or comment out plugin_load(&plugin_vim_keybinds, 1);
```

If you want runtime swap, load it via the keymap plugin (see
`plugins/keymap/README.md`) — but that plugin currently knows only
about `vim` and `emacs`. Extend the `apply()` function there to add a
`vscode` case.
