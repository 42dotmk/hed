← [hed](../../readme.md)

# vscode_keybinds

VSCode-flavored keymap. Modeless — the editor stays in INSERT
permanently.

Not loaded by default. Switch at runtime with `:keymap vscode`, or
pre-load it in `src/config.c` by setting
`plugin_load(&plugin_vscode_keybinds, 1);`.

## File / window / buffer

| Key | Action |
|---|---|
| `Ctrl+S` | Save (`:w`) |
| `Ctrl+N` | New empty buffer (`:new`) |
| `Ctrl+O` `Ctrl+P` | File picker (`:fzf`) |
| `Ctrl+E` | Recent files (`:recent`) |
| `Ctrl+W` | Close window |
| `Ctrl+\` `Alt+\` | Split editor (vertical) |
| `Alt+-` | Split horizontal |
| `Ctrl+PageDown` / `Ctrl+PageUp` | Next / previous buffer |
| `Alt+N` / `Alt+Shift+N` | Next / previous buffer |

## Edit

| Key | Action |
|---|---|
| `Ctrl+Z` `Ctrl+Y` | Undo / redo |
| `Ctrl+X` `Ctrl+C` `Ctrl+V` | Cut / copy / paste (line-wise without selection, region-wise with) |
| `Del` | Delete forward (joins lines at eol) |
| `Ctrl+Backspace` | Delete word left (arrives as `Ctrl+H` in terminals) |
| `Ctrl+Del` | Delete word right |
| `Alt+Up` / `Alt+Down` | Move line up / down |
| `Shift+Alt+Down` | Duplicate line |
| `Ctrl+]` / `Shift+Tab` | Indent / unindent line (`Ctrl+[` is Esc in a terminal, hence Shift+Tab) |
| `Ctrl+/` | Toggle comment (arrives as `Ctrl+_`; `Alt+/` also works) |
| `Shift+Alt+F` | Format document (`:fmt`) |

## Find & navigate

| Key | Action |
|---|---|
| `Ctrl+F` | Search in file |
| `F3` | Find next |
| `Ctrl+Shift+F` | Search in workspace (`:rg`) |
| `Ctrl+G` | Go to line (prompt prefilled with `:goto `) |
| `Alt+Left` / `Alt+Right` | Navigate back / forward (jump list) |
| `F12` `Ctrl+T` | Go to definition / symbol (`:tag`, needs ctags) |

## Multi-cursor (multicursor plugin)

| Key | Action |
|---|---|
| `Ctrl+D` | Add cursor at next occurrence of word under cursor (or selection) |
| `Ctrl+K Ctrl+D` | Skip this occurrence, take the next |
| `Ctrl+Alt+Up` / `Ctrl+Alt+Down` | Add cursor above / below |
| `Ctrl+K Ctrl+S` | Toggle synced edits at all cursors (`:mc_sync`) |
| `Ctrl+K Esc` | Clear extra cursors |

Unlike VSCode, added cursors don't edit synchronously until you turn
sync on (`Ctrl+K Ctrl+S`) — that's the multicursor plugin's model.

## Selection

| Key | Action |
|---|---|
| `Shift+Up/Down/Left/Right` | Extend selection |
| `Ctrl+Shift+Left` `Ctrl+Shift+Right` | Extend by word |
| `Shift+Home` `Shift+End` | Extend to line start / end |
| `Ctrl+Shift+Home` `Ctrl+Shift+End` | Extend to file start / end |
| `Shift+PageUp` `Shift+PageDown` | Extend a page at a time |
| `Ctrl+A` | Select all |
| `Ctrl+L` | Select line (repeat to extend line-wise) |

## Motion

| Key | Action |
|---|---|
| `Ctrl+Left` `Ctrl+Right` | Move by word |
| `Home` `End` | Line start / end |
| `Ctrl+Home` `Ctrl+End` | File start / end |
| `PageUp` `PageDown` | Page up / down |

## Folding

| Key | Action |
|---|---|
| `Ctrl+K Ctrl+L` | Toggle fold at cursor |
| `Ctrl+K Ctrl+J` | Unfold all |
| `Ctrl+K 0` | Fold all (`Ctrl+0` isn't deliverable, so plain `0`) |

## Command palette

`F1` or `Alt+P` — opens hed's command picker (same as `:c`).

> Why not `Ctrl+Shift+P`? Most terminals can't deliver the
> Ctrl+Shift+letter combination — that's an xterm-protocol
> limitation. The same applies to `Ctrl+Shift+K`, `Ctrl+Enter`,
> `Ctrl+Tab`: where VSCode uses those, this keymap picks the closest
> deliverable alternative.

## Notes

The plugin sets `ed_set_modeless(1)` on init so every `:` command
that would normally drop you into NORMAL keeps you in INSERT.

`Ctrl+Backspace` is bound to `Ctrl+H` because that's the byte
terminals send for it. If your terminal sends `Ctrl+H` for plain
Backspace (rare, "backarrow mode"), Backspace will delete a word at
a time — rebind `<C-h>` in `src/config.c` if so.
