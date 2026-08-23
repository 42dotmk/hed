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
| `:wa` | Write all dirty named buffers |
| `:q` `:q!` `:quit` | Quit / force quit |
| `:qa` `:qa!` | Quit if nothing unsaved / discard everything |
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

## Motion & selection

Every motion in the editor is a registered text object, and the two
commands below are how keymaps (and you, from the prompt) drive them.
A plain motion drops any active selection; `:extend` grows one.

| Command | Action |
|---|---|
| `:goto <n>` | Jump to line |
| `:goto <motion> [count]` | Plain motion: apply a text object N times (drops any selection). Word-name aliases exist for the arrow/page motions: `left` `right` `up` `down` `pageup` `pagedown` |
| `:extend <motion> [count]` | Enter visual mode (anchor at the cursor) if needed, then move — Shift+motion as a command (`:extend w 3`, `:extend gg`) |
| `:select <textobj>` | Visually select a text object (`:select ae` = select all) |
| `:select_line` | Select the current line; repeating extends line-wise |

## Editing

The keymap-facing edit commands (`cmd_edit.c`). Operators act on the
active visual selection when one exists.

| Command | Action |
|---|---|
| `:delete` `:change` `:yank` | Operator + `<textobj>` arg, or interactive (`:delete iw`) |
| `:delete_line` `:delete_char` `:delete_eol` | Line / char / to-eol deletes |
| `:delete_forward` | Del semantics: selection, else char, else join at eol |
| `:delete_word_left` `:delete_word_right` | Word-run deletes (join lines at the edges) |
| `:change_line` `:change_eol` `:yank_line` | Line-wise variants |
| `:join` `:indent` `:unindent` | Join with next line / (un)indent |
| `:move_line_up` `:move_line_down` `:duplicate_line` | Line shuffling |
| `:toggle_case` `:toggle_comment` `:replace_char [c]` | In-place edits |
| `:insert` `:append` `:insert_bol` `:append_eol` | Enter insert mode |
| `:visual` `:visual_line` `:visual_block` | Toggle visual modes |
| `:put [reg]` `:put!` | Paste after / before (over the selection if active) |
| `:search` `:search_next` `:search_prev` `:search_word` `:search_selection` | Search family |
| `:center` `:scrollup` `:scrolldown` | View motion (half-page scrolls) |
| `:scroll_line_up` `:scroll_line_down` | Scroll the viewport one line (vim `C-y`/`C-e`, VSCode `Ctrl+Up/Down`) |

## Misc

| Command | Action |
|---|---|
| `:prompt [prefill]` | Open the `:` prompt, optionally pre-filled (`cmapn(" mn", "prompt task_note", ...)` — bind "prompt ready for X" without C code) |
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
| `:ftmap <ext> <filetype>` | Map an extension or basename to a filetype (session only; persistent via `fs_filetype_register()` in config) |

## Hooks

`core` also installs the editor-wide hooks: cursor-shape change on
mode change, undo grouping. These are why your block cursor turns
into a beam in INSERT mode.

## Notes

`core` ships no keybinds — those belong to the keymap plugins
(`vim_keybinds`, `emacs_keybinds`, `vscode_keybinds`). It only owns
the command surface.
