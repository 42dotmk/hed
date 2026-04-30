# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**hed** is a terminal editor written in C11 with a small core and a plugin
system. It integrates tree-sitter for syntax highlighting, ripgrep for
search, fzf for fuzzy finding, and tmux for runner pane support. Default
keybinds are Vim-style; alternate Emacs and VSCode keymaps ship as
plugins and can be hot-swapped at runtime via `:keymap`.

**Key features:**
- Plugin architecture — most user-facing features are plugins; config is
  a manifest of which to load
- Three keymap variants: `vim_keybinds` (default), `emacs_keybinds`,
  `vscode_keybinds` (latter two are modeless via the editor's
  `ed_set_modeless()` toggle)
- Modal editing (Normal / Insert / Visual / Visual Line / Visual Block /
  Command modes) when running under a modal keymap
- Tree-sitter syntax highlighting with 14+ language grammars
- Quickfix list with live preview synchronization
- Fuzzy finding (fzf) for files, recents, commands, history, jump list
- Tmux runner pane integration
- Macro recording and playback
- Code folding (manual / bracket / indent)
- Directory browser (oil.nvim-style)
- Modal (floating) windows
- Ctags integration
- Jump list, recent files, command history (persistent)
- Undo/redo with snapshot-based history
- Vim-like registers and text objects
- System clipboard via OSC 52 (no external `pbcopy`/`xclip` needed)
- Meta/Alt key support in the input layer (`<M-x>`)

## Build Commands

```bash
make                           # build → build/hed and build/tsi
make clean                     # remove build/
make fmt                       # clang-format -i on all sources
make run                       # build then run
make test                      # unity unit tests
make tags                      # ctags -R
make PLUGINS_DIR=/path/to/plugins   # use an external plugin set
```

The build system:
- Uses `gcc` with C11 + `-Wall -Wextra -pedantic`
- Links against `libtree-sitter` and `libdl`
- Produces `build/hed` (editor) and `build/tsi` (tree-sitter installer)
- Auto-discovers `*.c` recursively under `src/` and `$(PLUGINS_DIR)`
- Default `PLUGINS_DIR` is `plugins/` (top-level)

## Running hed

```bash
./build/hed [file ...]                  # open files
./build/hed -c "<command>"              # run a command at startup
./build/hed /path/to/dir                # open directory (dired)
./build/hed .                           # current dir as dired
```

Logs are written to `.hedlog` in the current directory.

## Architecture Overview

### The Plugin System

Most features live in `plugins/<name>/` and are activated by
`config_init()` in `src/config.c`. The plugin interface (3 functions, 1
struct) is in `src/plugin.{c,h}`:

```c
typedef struct Plugin {
    const char *name, *desc;
    int  (*init)(void);
    void (*deinit)(void);
} Plugin;

int plugin_load(const Plugin *p, int enabled);   // register and optionally init
int plugin_enable(const Plugin *p);              // run init() if not already
int plugin_disable(const Plugin *p);             // run deinit() if any
```

Each plugin exposes `extern const Plugin plugin_<name>` via a small
header. `config.c` includes those headers and calls `plugin_load(&...,
1|0)` — explicit, link-checked, no string lookup. There is no master
list / manifest file; the config IS the manifest.

`plugin_load(p, 0)` registers a plugin without calling `init()` —
useful for keymap plugins that should be available for runtime swap but
not active at boot. `:plugins` command lists loaded plugins and their
enabled state.

### Core Data Flow

1. **Main loop** (`src/main.c`): `select()` on stdin → `ed_read_key()`
   → `ed_process_keypress()` → `ed_render_frame()`. Supports startup
   commands via `-c`.

2. **Editor state** (`src/editor.{c,h}`): global `Ed E` with buffers,
   active mode, layout tree, quickfix, jump list, recents, command
   history, registers, macro queue, search state, cwd. Also owns the
   modeless flag (`g_modeless`).

3. **Buffer system** (`src/buf/`): `Buffer` (rows + cursor + filename +
   filetype + tree-sitter state + folds + undo), `Row` (raw chars +
   render), `textobj.c` (text object extraction), `buf_helpers.c`.

4. **Command execution** (`src/commands.{c,h}`): registered via
   `command_register(name, callback, desc)` (or the `cmd(...)` macro).
   Plugins register their own commands inside their `init()`.

5. **Keybinding dispatch** (`src/keybinds.{c,h}`):
   - Per-mode: `keybind_register(mode, sequence, callback, desc)`
   - Macros in `src/hed.h`: `mapn` / `mapi` / `mapv` / `mapvb` / `mapvl`
     for function callbacks, `cmapn` / `cmapv` / `cmapi` for command
     callbacks (run `:cmd args`)
   - Multi-key sequences (`dd`, `gg`, `<C-x><C-s>`)
   - Numeric prefix support — count is consumed by callbacks via
     `keybind_get_and_clear_pending_count()` (see `kb_goto_file_start`)
   - **Last-write-wins**: re-registering the same `(mode, sequence,
     filetype)` replaces the prior binding (`remove_duplicate` in
     `keybinds.c`). This lets later plugins or user config override
     plugin defaults.

6. **Hook system** (`src/hooks.{c,h}`):
   - Events: char/line insert/delete, buffer lifecycle (open/close/
     switch/save), buffer pre-open and pre-save (intercept hooks),
     mode change, cursor movement, keypress
   - Pre-open / pre-save hooks let plugins claim ownership by setting
     `event->consumed = 1` (this is how `dired` intercepts directory
     opens without core knowing about it)

7. **Modeless mode** (`src/editor.c`): when `ed_set_modeless(1)` is on,
   any attempt to enter `MODE_NORMAL` is silently redirected to
   `MODE_INSERT`. Used by `emacs_keybinds` and `vscode_keybinds`.
   Toggleable at runtime via `:modeless on|off|toggle`.

### Key Subsystems

#### Tree-sitter (`src/utils/ts.c`)
Grammars are `.so` files loaded from `$HED_TS_PATH` (defaults to
`~/.config/hed/ts` or `ts-langs/`). Each buffer has a `TSState*` in
`buf->ts_internal`. Highlight queries from `queries/<lang>/highlights.scm`.
Commands: `:ts on|off|auto`, `:tslang <name>`, `:tsi <lang>`.
Included grammars: bash, c, c-sharp, commonlisp, html, javascript, make,
markdown, org, python, rust, toml, unifieddiff, zsh.

#### Renderer (`src/terminal.c`)
Builds each frame into a single `Abuf` (append buffer) and writes it in
one `write()` call. Wraps each frame in **DEC mode 2026 (Synchronized
Output)** so modern terminals commit atomically and don't flicker.
Older terminals ignore the escape harmlessly. Inside tmux, `set -as
terminal-features ',alacritty:sync'` (or similar for your outer term)
in `~/.tmux.conf` is needed for the sync to propagate.

#### Input layer (`src/editor.c`)
`ed_read_key()` parses raw bytes into key codes:
- Plain ASCII / control characters
- CSI sequences (arrows, Home, End, PageUp/Down, Delete)
- **Meta/Alt keys**: `ESC <byte>` becomes `KEY_META | byte`. Encodes as
  `<M-x>`, `<M-/>`, etc. for use in `mapi`/`mapn`.

#### Quickfix
`E.qf` stores entries (file:line:col:text). The `quickfix_preview`
plugin (cursor hook scoped to `quickfix` filetype) keeps `E.qf.sel` in
sync with cursor and previews the target window. Populated by `:rg`,
`:ssearch`, `:cadd`. Navigation: `:cnext`, `:cprev`, `:copenidx N`,
`:ctoggle`. Default keybinds: `gn`/`<C-n>`, `gp`/`<C-p>`, `<space>tq`.

#### Window layout (`src/ui/wlayout.c`, `src/ui/window.h`)
Binary tree of splits. Leaves are `Window`s with buffer index, cursor,
offsets, gutter mode, wrap setting. Directional focus via
`windows_focus_left/right/up/down`. Quickfix is a special window with
`is_quickfix=1`. Modal (floating) windows draw on top via
`src/ui/winmodal.c`.

#### Other in-core subsystems
- **Macros** (`src/macros.c`): `q<reg>` / `@<reg>` / `@@`, plus
  `:record`, `:play`. Macro queue lets recorded keys execute without
  blocking on stdin.
- **Folding** (`src/utils/fold.c`, `src/fold_methods/`): manual,
  bracket-based, indent-based. Per-buffer `FoldList`, collapsed lines
  shown as `[...]`. `za`/`zo`/`zc`/`zR`/`zM`.
- **Ctags** (`src/utils/ctags.c`): `:tag <name>` and `gd` use ripgrep
  to look up tags. `make tags` to (re)generate.
- **Undo** (`src/utils/undo.c`): per-buffer snapshot stacks captured
  via mode-change and char/line modification hooks. `u` / `<C-r>`,
  `:undo` / `:redo`.
- **Registers** (`src/registers.c`): vim-like `"` (unnamed), `0` (yank),
  `1`–`9` (delete history), `a`–`z` (named), `:` (last command), `.`
  (last keybind sequence). Append with uppercase `A`–`Z`.
- **Jump list** (`src/utils/jump_list.c`): `<C-o>`/`<space>jb` back,
  `<C-i>`/`<space>jf` forward.
- **Recent files** (`src/utils/recent_files.c`): `:recent` with fzf,
  `<space>fr`. Persisted across sessions.
- **History** (`src/utils/history.c`): persistent command-line history,
  `:hfzf` for fuzzy search.

## Plugins (in `plugins/`)

| Plugin | What it owns |
|---|---|
| `core` | Default `:command` set + a few editor-wide hooks (cursor shape, undo registration). `:goto`, `:modeless`, `:plugins` live here. |
| `vim_keybinds` | Default Vim modal keymap + textobjects (`hjkl`, `w`/`b`/`e`, operators, `gg`/`G`, `dd`/`yy`, fold keys, etc.) |
| `emacs_keybinds` | Emacs keymap with `C-a/C-e/C-n/C-p/C-b/C-f/C-d/C-k/C-y/C-s`, `C-x` prefix cluster, `M-f`/`M-b`/`M-x`/etc. Sets `ed_set_modeless(1)`. |
| `vscode_keybinds` | VSCode keymap with `Ctrl+S/N/O/P/W/Z/Y/X/C/V/F/G/D` + `Home`/`End`. Sets `ed_set_modeless(1)`. |
| `keymap` | `:keymap vim\|emacs\|vscode` and `:keymap-toggle` for runtime swap. |
| `clipboard` | Mirrors yank to system clipboard via OSC 52. |
| `quickfix_preview` | Cursor hook that previews quickfix entries. |
| `dired` | Directory browser. Owns `<CR>`/`-`/`~`/`cd` keybinds + `HOOK_BUFFER_OPEN_PRE`/`SAVE_PRE` interceptors. |
| `lsp` | LSP client (WIP). Lifecycle hooks + `:lsp_*` commands. |
| `viewmd` | Markdown live preview. |
| `tmux` | Runner pane integration. `:tmux_toggle` / `:tmux_send` / `:tmux_kill` / `:tmux_send_line`. |
| `fmt` | `:fmt` runs an external formatter (clang-format, rustfmt, prettier, ...) by filetype. |
| `auto_pair` | Auto-insert matching brackets/quotes in insert mode. |
| `smart_indent` | Carry previous line's indentation onto new lines. |
| `example` | Starter template — copy and rename to make your own plugin. |

Each plugin has its own `README.md`. See `plugins/example/README.md`
for the recipe to add a new plugin.

## Configuration (`src/config.c`)

`config_init()` is a single function with a manifest:

```c
void config_init(void) {
    plugin_load(&plugin_core,             1);
    plugin_load(&plugin_vim_keybinds,     1);
    plugin_load(&plugin_emacs_keybinds,   0);   // loaded but not active
    plugin_load(&plugin_vscode_keybinds,  0);
    plugin_load(&plugin_keymap,           1);
    plugin_load(&plugin_clipboard,        1);
    plugin_load(&plugin_quickfix_preview, 1);
    plugin_load(&plugin_dired,            1);
    plugin_load(&plugin_lsp,              1);
    plugin_load(&plugin_viewmd,           1);
    plugin_load(&plugin_auto_pair,        1);
    plugin_load(&plugin_smart_indent,     1);
    plugin_load(&plugin_fmt,              1);
    plugin_load(&plugin_tmux,             1);

    /* Personal overrides — last-write-wins beats plugin defaults. */
    cmapn(" ff", "fzf");
    /* ... */
}
```

To add a personal plugin: copy `plugins/example/`, rename, register in
`config.c`. To use an out-of-tree plugin set: `make
PLUGINS_DIR=$HOME/my-hed-plugins` and reference its headers from your
config.

After modifying `src/config.c`, run `:reload` from within hed to
rebuild and restart.

## Built-in Commands Reference

### File I/O
- `:e <file>` — edit file or open directory (dired)
- `:w [file]` — write
- `:q` / `:q!` / `:wq` — quit / force quit / write & quit

### Buffer / Window
- `:ls`, `:bn`, `:bp`, `:b <N>`, `:bd`, `:new`, `:refresh`
- `:split`, `:vsplit`, `:wclose`, `:wfocus`, `:wh|wj|wk|wl`
- `:modal` / `:unmodal`

### Navigation
- `:goto <line>` — jump to line N
- `:goto <motion> [count]` — apply text-object motion ([count] times)
- `:tag <name>`, `:rg`, `:rgword`, `:ssearch`, `:fzf`, `:recent`, `:c`
- `/`, `?` — search forward / backward in buffer

### Quickfix
- `:copen`, `:cclose`, `:ctoggle`, `:cnext`, `:cprev`, `:copenidx <N>`,
  `:cadd <file:line:col:text>`, `:cclear`

### Tree-sitter / formatting
- `:ts on|off|auto`, `:tslang <name>`, `:tsi <lang>`
- `:fmt` (filetype-driven)

### Folding / macros / undo
- `:foldnew`, `:foldrm`, `:foldtoggle`, `:foldmethod`, `:foldupdate`
- `:record [reg]`, `:play [reg]`
- `:undo`, `:redo`, `:repeat`

### Tmux
- `:tmux_toggle`, `:tmux_send <cmd>`, `:tmux_kill`, `:tmux_send_line`

### Plugin / mode / keymap
- `:plugins` — list loaded/enabled plugins
- `:keymap [vim|emacs|vscode]` — query or switch keymap
- `:keymap-toggle` — cycle vim → emacs → vscode → vim
- `:modeless on|off|toggle` — gate the always-insert redirect

### Misc
- `:git` (lazygit), `:shell <cmd>`, `:reload`, `:logclear`,
  `:ln`, `:rln`, `:wrap`, `:cd`, `:pwd`, `:reg`, `:put`, `:echo`,
  `:history`, `:hfzf`, `:jfzf`, `:keybinds`, `:sed`

## Common Keybindings (default `vim_keybinds`)

### Normal mode — motion
`h`/`j`/`k`/`l`, `w`/`b`/`e`, `0`/`$`, `gg`/`G`, `{`/`}`, `<C-u>`/`<C-d>`,
`%` (matching bracket), `*` / `<C-*>` (find under cursor),
`gf` / `gF` (open / search file under cursor)

Numeric prefix: `42G`, `42gg` jump to line 42. `5j` → down 5 lines.

### Normal mode — editing
`i`/`a`/`I`/`A`/`o`/`O`, `x`, `dd`, `yy`, `D`, `C`, `S`, `J`, `r`, `~`,
`p`, `<<` / `>>`, `gc` (toggle comment), `u` / `<C-r>`, `.`

### Normal mode — operators + text objects
`d`/`c`/`y` followed by a motion or text object:
`diw`, `daw`, `ci(`, `da{`, `yi"`, etc.

### Normal mode — leader (`<space>`)
- `<space>ff` / `<space><space>` — fzf
- `<space>fr` — recent files
- `<space>c` / `<space>fc` — command picker
- `<space>sd` / `<space>sa` / `<space>ss` — rg / rgword / ssearch
- `<space>ts` — send paragraph to tmux
- `<space>tt` / `<space>tT` — toggle / kill tmux pane
- `<space>tq` — toggle quickfix
- `<space>ws` / `<space>wv` — split / vsplit
- `<space>ww` / `<space>wh` / `<space>wj` / `<space>wk` / `<space>wl` —
  window focus
- `<space>jb` / `<space>jf` — jump back / forward
- `<space>tk` — keymap-toggle
- `<space>fh` / `<space>fj` — history / jump-list fzf

### Folding
`za`, `zo`, `zc`, `zR`, `zM`, `zz` (center)

### Visual mode
`v`, `V`, `<C-v>` to enter; `<Esc>` to exit. `y`, `d`, `c`, `<` / `>`.

### Insert mode
`<Esc>` exits to normal, `<CR>` newline, `<Tab>` insert tab, `<BS>` /
`<C-h>` backspace. Auto-pairing for `()` / `[]` / `{}` / `<>` / quotes.
Smart indent on `<CR>`.

### Macros
`q<reg>` start/stop, `@<reg>` play, `@@` repeat last.

### Dired
`<CR>` open, `-` parent, `~` home, `cd` chdir.

### Emacs / VSCode keymaps
See `plugins/emacs_keybinds/README.md` and
`plugins/vscode_keybinds/README.md`. Switch with `:keymap emacs` or
`:keymap vscode`.

## Code Style and Patterns

- **Errors**: functions return `EdError` enum. `if (err != ED_OK) ...`
- **Bounds**: use `BOUNDS_CHECK(idx, len)` and `PTR_VALID(ptr)` from
  `src/lib/errors.h`
- **Strings**: `SizedStr` for length-tracked strings (`src/lib/sizedstr.h`)
- **Logging**: `log_msg(fmt, ...)` writes to `.hedlog`
- **Status**: `ed_set_status_message(fmt, ...)` for user feedback
- **Memory**: manual `malloc`/`free`, null-check allocations
- **Vectors**: type-safe macros in `src/lib/vector.h`

### Common patterns

```c
/* Get current buffer/window */
Buffer *buf = buf_cur();
Window *win = window_cur();
if (!buf || !win) return;

/* Invoke a command programmatically */
command_invoke("split", NULL);
command_invoke("e", "/path/to/file");

/* Modify buffer */
buf_insert_char_in(buf, c);
buf_delete_line_in(buf);
buf_row_insert_in(buf, at_row, text, text_len);
```

### Registering a plugin command/keybind/hook

All inside the plugin's `init()`:
```c
static int my_plugin_init(void) {
    cmd("mycmd", cmd_mycmd, "description");
    mapn("<C-x>", kb_my_callback, "do thing");
    cmapn(" ml", "mycmd");
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", on_insert);
    return 0;
}

const Plugin plugin_my = {
    .name = "my", .desc = "my plugin",
    .init = my_plugin_init, .deinit = NULL,
};
```

## Tree-sitter Language Management

```bash
:tsi <lang>           # install grammar from within hed
./build/tsi <lang>    # standalone installer
```

This clones `tree-sitter-<lang>` from GitHub, builds `<lang>.so`, copies
it to `ts-langs/`, and copies highlight queries to `queries/<lang>/`.

Filetype detection is in `buf_detect_filetype()` in
`src/buf/buffer.c`. To force a language: `:tslang <name>`.

## Testing and Debugging

- **Logs**: `tail -f .hedlog`. `:logclear` truncates.
- **Tests**: `make test` runs Unity tests in `test/`
- **Valgrind**: works under default flags
- **GDB**: `gdb ./build/hed` then `run [args]`

## File Organization

```
src/
├── main.c                # Entry point, main loop, select-based fd pump
├── editor.c/h            # Global E state, ed_set_mode, ed_set_modeless
├── config.c/h            # User configuration: plugin manifest + overrides
├── plugin.c/h            # Plugin runtime (load/enable/disable/list)
├── commands.c/h          # Command registration + dispatch
├── command_mode.c/h      # Command-line UI (`:`) with completion + Tab→fzf
├── keybinds.c/h          # Keybind registration + dispatch (last-write-wins)
├── keybinds_builtins.c/h # kb_* callbacks (movement, edit, operators, ...)
├── hooks.c/h             # Hook registration + firing
├── hook_builtins.c/h     # hook_change_cursor_shape (others are plugins)
├── terminal.c/h          # Raw mode, ANSI, render loop, save/load
├── registers.c/h         # Yank/paste registers
├── macros.c/h            # Macro recording/playback + macro queue
├── hed.h                 # Master include + map/cmap macros
├── buf/
│   ├── buffer.c/h        # Buffer struct + open/close/switch
│   ├── row.c/h           # Row struct + render
│   ├── textobj.c/h       # Text-object extraction
│   └── buf_helpers.c/h   # Buffer utility functions
├── ui/
│   ├── window.c/h        # Window struct + focus
│   ├── wlayout.c/h       # Layout tree (splits)
│   ├── winmodal.c/h      # Modal (floating) windows
│   └── abuf.c/h          # Append buffer
├── utils/
│   ├── ts.c/h            # Tree-sitter integration
│   ├── quickfix.c/h      # Quickfix list
│   ├── fzf.c/h           # fzf integration
│   ├── fold.c/h          # Folding system
│   ├── undo.c/h          # Undo/redo
│   ├── ctags.c/h         # Ctags lookup
│   ├── jump_list.c/h
│   ├── history.c/h
│   ├── recent_files.c/h
│   ├── bottom_ui.c/h     # Status + cmdline drawing
│   ├── term_cmd.c/h      # Shell-out helper
│   ├── sed.c/h           # :sed implementation
│   └── yank.c/h          # Yank-from-selection helper
├── fold_methods/
│   ├── fold_methods.c/h
│   ├── fold_bracket.c
│   └── fold_indent.c
├── lib/
│   ├── vector.h          # Type-safe growable arrays
│   ├── log.c/h
│   ├── errors.c/h
│   ├── sizedstr.c/h
│   ├── strutil.c/h
│   ├── safe_string.c/h
│   ├── file_helpers.c/h
│   ├── theme.h
│   ├── ansi.h
│   ├── cursor.h
│   └── cjson/            # JSON parser (used by LSP)
└── commands/
    ├── cmd_builtins.h    # Built-in command declarations
    ├── cmd_misc.c/h
    ├── cmd_search.c/h
    ├── cmd_util.c/h
    ├── commands_buffer.c/h
    └── commands_ui.c/h

plugins/                  # Default location; override with PLUGINS_DIR
├── core/                 # Default :commands + minimal hooks
├── vim_keybinds/         # Default Vim keymap
├── emacs_keybinds/       # Emacs keymap
├── vscode_keybinds/      # VSCode keymap
├── keymap/               # Runtime keymap swap (:keymap)
├── clipboard/            # OSC 52 yank → system clipboard
├── quickfix_preview/     # Cursor → preview sync
├── dired/                # Directory browser
├── lsp/                  # LSP client
├── viewmd/               # Markdown live preview
├── tmux/                 # Runner pane integration
├── fmt/                  # External formatters (:fmt)
├── auto_pair/            # ()/[]/{}/quotes auto-pairing
├── smart_indent/         # Carry indent on newline
└── example/              # Starter template — copy to make your own

test/                     # Unity unit tests
ts/                       # Tree-sitter language installer source
ts-langs/                 # Compiled tree-sitter .so files
queries/                  # Highlight queries by language
tasks/                    # Project notes / roadmap
```

## Dependencies

### Required
- `gcc` (or `clang`) with C11 support
- `libtree-sitter` (headers and library)
- `libdl`
- POSIX terminal

### Optional
- `ripgrep` — `:rg`, `:ssearch`, `:rgword`
- `fzf` — `:fzf`, `:recent`, `:c`, `:hfzf`, `:jfzf`, command-name
  Tab→fzf in command mode
- `tmux` — runner pane integration
- `lazygit` — `:git`
- `bat` — file preview in fzf
- `ctags` — `:tag`, `gd`
- `clang-format`, `rustfmt`, `prettier`, `black`, `gofmt` — used by
  `:fmt` based on filetype

## Philosophy & Design Principles

1. **Small core, plugin shell**: anything that can be a plugin should be
   one. The current core is ~10K LOC; each plugin is typically <300
   LOC.
2. **Explicit, not magic**: plugin manifest is a list of `plugin_load`
   calls — no codegen, no constructors, no string lookup. Misspelling a
   plugin name is a link error.
3. **Last-write-wins keybinds**: plugin defaults are overridable. Order
   in `config.c` is the precedence.
4. **Hook-driven cross-cutting concerns**: pre-open/save intercept hooks
   keep core unaware of dired, plugins, etc.
5. **External tools over reinvention**: ripgrep, fzf, tmux, lazygit,
   prettier — leverage what exists.
6. **Type-safe generics via macros**: `DECLARE_VECTOR`, etc.
7. **Manual memory management**: clear ownership, explicit `malloc`/
   `free`, null checks.
8. **Modal optional**: defaults are Vim, but emacs/vscode keymaps are
   first-class via `ed_set_modeless()`.

## Future Roadmap

- LSP completions / formatting wiring
- Plugin `deinit()` actually unregistering hooks/cmds/keybinds (needs
  `hook_unregister`, `command_unregister`, `keybind_unregister`)
- Generic open-handler / save-handler decoupling for `lsp_init`
  (`editor.c`) and the fd-pump (`main.c`) — currently those are the
  remaining core couplings to plugin code
- `:plugin enable/disable <name>` runtime command (needs unregister)
- More fold methods, more text objects
- Configurable formatter table via runtime command (currently hard-coded
  in `plugins/fmt/fmt.c`)
