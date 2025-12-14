# hed

Modal terminal editor written in C23. hed keeps a small core but ships useful modern tools: tree-sitter highlights, fuzzy file/search pickers, ripgrep-backed quickfix lists, window splits, tmux runner support, and an explicit C API for adding commands, keybindings, and hooks.
## Highlights
- Normal / Insert / Command plus Visual (char + block) modes with Vim-like motions and operators, undo/redo, registers, and shared clipboard across buffers.
- Unlimited buffers and multiple windows with vertical/horizontal splits; quickfix pane stays in sync with the cursor for previews.
- Search stack: `/` and `*` for intra-file search, ripgrep integrations (`:rg`, `:ssearch`, `:rgword`) that populate quickfix, and jump list navigation (Ctrl-o/Ctrl-i).
- Tree-sitter highlighting with queries in `queries/` and prebuilt grammars in `ts-langs/`; runtime toggle and per-buffer language selection.
- Fuzzy tools: `:fzf` file picker, `:recent` recent files, `:c` command picker, optional `bat` previews.
- Integrations: tmux runner pane (`:tmux_toggle`, `:tmux_send`), lazygit wrapper (`:git`), shell passthrough (`:shell`), formatter hook (`:fmt`), optional word wrap and line-number toggles.

## Requirements
- clang, make, POSIX terminal.
- `libtree-sitter` headers/runtime (`-ltree-sitter` is linked).
- Optional but recommended: `ripgrep`, `fzf`, `tmux`, `lazygit`, `bat` (for previews), `nnn` (directory browsing)

## Build
```bash
make            # builds build/hed and build/tsi, copies ts-langs/
make run        # run the binary after building
make clean      # remove build artifacts
```
Outputs: `build/hed` (editor) and `build/tsi` (tree-sitter grammar installer).

## Run
```bash
./build/hed [file ...]
```
Use `-c "<command>"` to run a command at startup (same as typing `:<command>`), e.g., `-c "e other.txt"` or `-c "q!"`. Multiple filenames open multiple buffers. Logs go to `.hedlog` (clear with `:logclear`).

## Quickstart Keys
- Modes: `ESC` -> Normal, `i`/`a` enter Insert/Append, `o`/`O` open a new line, `v` Visual, `Ctrl-v` block-visual, `:` Command line.
- Movement: `h j k l` or arrows, `0`/`$`, `gg`/`G`, `Ctrl-u`/`Ctrl-d`, `%` match bracket, `*` find under cursor, `/` search, `n` next match.
- Editing: `x` delete char, `dd` delete line, `dw`/`db` delete word forward/back, `yy` yank line, `p` paste, `u` undo, `Ctrl-r` redo, `J` join lines, `>>`/`<<` indent/outdent, `cc` toggle comment on the line.
- Visual: `v` or `Ctrl-v` to start, `y` yank selection, `d` delete selection, `i`/`a` jump to insert/append.
- Buffers: `:e <file>`, `:ls` or `<space>bb` list, `:b N` to switch, `:bd[ N]` close, `:bn`/`:bp` next/prev.
- Files: `:w`, `:q`, `:wq`, `:q!`, `:new` opens an empty split.
- Windows: `:split` / `:vsplit`, `<space>ws`/`wv` split, `<space>ww` cycle focus, `<space>wh|wj|wk|wl` move focus, `<space>wd` close window.
- Search & quickfix: `<space><space>` or `<space>ff` run `:fzf`, `<space>fr` recent files, `<space>sd` (`:rg`) and `<space>ss` (`:ssearch`), `<space>sa` ripgrep word under cursor, quickfix toggle `<space>tq`, next/prev `Ctrl-n`/`Ctrl-p` or `gn`/`gp`, `:copen`/`:cclose`/`:cclear`.
- Tmux runner: `<space>tt` toggle runner pane, `<space>tT` kill it, `<space>ts` send current line, or `:tmux_send <cmd>`.
- Misc: `<space>cf` format buffer (`:fmt`), `<space>tw` toggle wrap, `<space>qq` force quit, `<space>rm` runs `make` via shell, `<space>gg` opens lazygit.

Key chord notation uses literal spaces; `<space>ff` means press Space then `f` then `f` in Normal mode.

## Tree-sitter
- hed looks for grammars in `$HED_TS_PATH` (defaults to `$XDG_CONFIG_HOME/hed` or `$XDG_HOME/.config/hed`, falling back to `ts-langs/`). Included: c, c-sharp, html, make, python, rust.
- Install another grammar: `./build/tsi <lang>` (clones `tree-sitter-<lang>`, builds `<lang>.so` into `ts-langs/`, copies queries into `queries/<lang>/`).
- Commands: `:ts on|off|auto` (auto detects by extension), `:tslang <name>` to force a language, `:tsi <name>` installs via the helper.

## Search + Quickfix Workflow
- `:rg <pattern>` (non-interactive) populates quickfix; `:rg` with no args opens an interactive fzf+rg picker.
- `:ssearch` searches the current file with live reload and can populate quickfix.
- Quickfix commands: `:copen`, `:cclose`, `:ctoggle`, `:cnext`/`cprev`, `:copenidx N`, `:cclear`. The quickfix buffer follows the cursor and previews the selected item.

## Configuration
- All user-facing customization lives in `src/config.c`:
  - `user_commands_init` registers commands (`cmd(name, fn, desc)` macro).
  - `user_keybinds_init` sets keybindings for each mode (`mapn/mapi/mapv` and `cmapn/cmapv` to map commands).
  - `user_hooks_init` wires editor hooks (e.g., mode change, buffer lifecycle, cursor movement). Extra quickfix hook logic is in `src/user_hooks_quickfix.c`.
- Extend in C, and run `:reload` from inside hed to rebuild and restart.

## Project Layout
- `src/` core editor (buffers, windows, terminal, commands, keybinds, hooks, undo, quickfix, tmux/fzf/rg helpers, tree-sitter glue).
- `src/ui/` rendering helpers and window layout tree.
- `src/buf/` buffer and row data structures.
- `src/utils/` higher-level features (tree-sitter, quickfix, fzf, tmux, jump list, history, recent files).
- `ts/ts_lang_install.c` grammar installer; `ts-langs/` shared objects; `queries/` highlight queries.

## Troubleshooting
- Logs: `tail -f .hedlog`, clear with `:logclear`.
- If fzf/rg/tmux/lazygit are missing, related commands/keybinds will fail silently or set a status message.
