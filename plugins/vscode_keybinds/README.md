# vscode_keybinds

VSCode-flavored keymap. Modeless — the editor stays in INSERT
permanently.

Not loaded by default. Switch at runtime with `:keymap vscode`, or
pre-load it in `src/config.c` by setting
`plugin_load(&plugin_vscode_keybinds, 1);`.

## File / buffer

| Key | Action |
|---|---|
| `Ctrl+S` | Save (`:w`) |
| `Ctrl+N` | New empty buffer (`:new`) |
| `Ctrl+O` `Ctrl+P` | File picker (`:fzf`) |
| `Ctrl+W` | Close window |

## Edit

| Key | Action |
|---|---|
| `Ctrl+Z` `Ctrl+Y` | Undo / redo |
| `Ctrl+X` `Ctrl+C` `Ctrl+V` | Cut / copy / paste (line-wise without selection, region-wise with) |

## Find & navigate

| Key | Action |
|---|---|
| `Ctrl+F` | Search (`:ssearch`) |
| `Ctrl+G` | Go to line / motion (`:goto`) |
| `Ctrl+D` | Find next occurrence of word under cursor |

## Selection

| Key | Action |
|---|---|
| `Shift+Up/Down/Left/Right` | Extend selection |
| `Ctrl+Shift+Left` `Ctrl+Shift+Right` | Extend by word |
| `Shift+Home` `Shift+End` | Extend to line start / end |

## Word motion

| Key | Action |
|---|---|
| `Ctrl+Left` `Ctrl+Right` | Move by word |

## Command palette

`F1` or `Alt+P` — opens hed's command picker (same as `:c`).

> Why not `Ctrl+Shift+P`? Most terminals can't deliver the
> Ctrl+Shift modifier combination — that's an xterm-protocol
> limitation. `F1` and `Alt+P` are the closest universally-routable
> alternatives.

## Notes

The plugin sets `ed_set_modeless(1)` on init so every `:` command
that would normally drop you into NORMAL keeps you in INSERT.

If you're missing something — multi-cursor, jump-to-symbol — that's
because hed's core doesn't support it yet, not because the keybind
is missing. Treat this keymap as a familiar surface, not as a full
VSCode replica.
