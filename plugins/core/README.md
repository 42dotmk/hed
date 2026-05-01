# core

The default `:` command set. Every keymap-agnostic command that
"comes with hed" lives here — file I/O, buffer navigation, quickfix,
search, registers, undo, macros, windows, folds, shell, fuzzy
pickers. Without it, the editor starts but most commands are
unavailable.

## File / buffer

| Command | Action |
|---|---|
| `:e <path>` | Edit file or directory |
| `:w` | Write current buffer |
| `:q` `:q!` `:quit` | Quit / force quit |
| `:wq` | Write + quit |
| `:bn` `:bp` `:b <n>` | Next / previous / nth buffer |
| `:ls` | List open buffers |
| `:bd` | Delete buffer |
| `:refresh` | Reload buffer from disk |

## Search

| Command | Action |
|---|---|
| `:rg <pattern>` | Ripgrep into quickfix |
| `:rgword` | Ripgrep word under cursor |
| `:ssearch` | Search current file (fzf interactive) |

## Quickfix

`:copen` `:cclose` `:ctoggle` `:cnext` `:cprev` `:cadd` `:cclear`
`:copenidx <n>`

## Windows

`:split` `:vsplit` `:wfocus` `:wclose` `:new`
`:wh` `:wj` `:wk` `:wl` (focus left/down/up/right)
`:modal` `:unmodal` (convert current window to/from a floating modal)

## Folds

`:foldnew <s> <e>` `:foldrm` `:foldtoggle` `:foldmethod <name>`
`:foldupdate`

## Pickers (fuzzy)

`:fzf` (file picker) — `:recent` (recent files) — `:c [query]`
(command picker) — `:hfzf` (command history) — `:jfzf` (jump list)

## Misc

| Command | Action |
|---|---|
| `:goto <n>` | Jump to line |
| `:goto <motion> [count]` | Apply a registered motion N times |
| `:tag <name>` | Jump to ctags definition |
| `:keybinds` | List all currently registered keybinds |
| `:plugins` | List loaded plugins |
| `:cd [dir]` `:pwd` | Change / print working directory |
| `:shell <cmd>` `:shq <cmd>` | Run a shell command |
| `:git` | Open lazygit |
| `:undo` `:redo` `:repeat` | Undo / redo / repeat last action |
| `:record <reg>` `:play <reg>` | Macro record / play |
| `:reg` `:put <reg>` | Inspect / paste register |
| `:ln` `:rln` | Toggle line numbers / relative numbers |
| `:wrap` `:wrapdefault` | Toggle soft-wrap |
| `:logclear` | Clear `.hedlog` |
| `:echo <text>` | Print to status line |
| `:modeless on\|off\|toggle` | Toggle the always-insert redirect |

## Hooks

`core` also installs the editor-wide hooks: cursor-shape change on
mode change, undo grouping. These are why your block cursor turns
into a beam in INSERT mode.

## Notes

`core` ships no keybinds — those belong to the keymap plugins
(`vim_keybinds`, `emacs_keybinds`, `vscode_keybinds`). It only owns
the command surface.
