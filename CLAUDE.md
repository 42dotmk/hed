# hed

A small terminal editor written in C, built around a plugin system.
Vim-style modal editing by default; Emacs and VSCode keymaps ship as
plugins and can be swapped at runtime. Tree-sitter for highlighting,
ripgrep/fzf for search, tmux for runner panes, OSC 52 for clipboard.

## Install

Pre-built Linux x86_64 binary:

```bash
wget -O hed https://github.com/42dotmk/hed/releases/latest/download/hed-linux-x86_64 \
  && chmod +x hed \
  && ./hed
```

The binary is built with treesitter support; if `libtree-sitter` isn't
installed on the host, the editor still runs (grammars are loaded with
`dlopen` and missing-ts is handled gracefully). For the full
highlighting experience: `sudo apt-get install libtree-sitter0`. To
install grammars on demand from inside hed, also grab the `tsi`
helper:

```bash
wget -O tsi https://github.com/42dotmk/hed/releases/latest/download/tsi-linux-x86_64 \
  && chmod +x tsi && sudo mv tsi /usr/local/bin/
```

Or build from source:

```bash
make
./build/hed file.c
```

---

## What's distinctive

- **Plugin-first architecture.** Most features — keymaps, dired, lsp,
  viewmd, tmux, formatting, clipboard, auto-pair, smart-indent — live
  in `plugins/<name>/` and are activated by a single manifest
  (`config_init` in `src/config.c`). Adding a plugin is one directory
  + one line of config.
- **Three keymaps, swappable at runtime.** `:keymap vim` (default),
  `:keymap emacs` (modeless, C-key + M- prefix), `:keymap vscode`
  (modeless, Ctrl-key cluster, shift-arrow selection). Toggle with
  `:keymap-toggle`.
- **Real Meta/Alt + modifier-CSI parsing.** `<M-x>`, `<C-Left>`,
  `<S-Right>`, `<C-S-Left>`, `<F1>` all work. xterm modifier matrix
  decoded for arrows / Home / End / function keys.
- **Tree-sitter highlighting** for 14+ languages, loaded as `.so`
  files at runtime. `:tsi <lang>` installs a new grammar from GitHub.
- **No flicker** on terminals that support DEC mode 2026 (Synchronized
  Output) — every frame is bracketed atomically.
- **Tmux integration** as first-class. `<space>ts` sends the paragraph
  under the cursor to a runner pane; `:tmux_toggle` opens/closes it.
- **System clipboard** via OSC 52 (the `clipboard` plugin) — works
  over SSH, no `pbcopy`/`xclip` shelling out.
- **Last-write-wins keybinds.** Plugin defaults are overridable from
  config; later registrations replace earlier ones for the same key.

---

## Build

```
make                                    # default
make clean
make run                                # build then run
make test                               # Unity unit tests
make tags                               # ctags -R
make PLUGINS_DIR=$HOME/my-hed-plugins   # use an out-of-tree plugin set
```

The Makefile auto-discovers `*.c` recursively under `src/` and
`$(PLUGINS_DIR)` (defaults to `plugins/`).

### Dependencies

| Required | Optional |
|---|---|
| gcc / clang (C11) | ripgrep (`:rg`, `:ssearch`) |
| libtree-sitter | fzf (`:fzf`, `:c`, `:recent`, history fzf) |
| libdl | tmux (runner pane) |
| POSIX terminal | lazygit (`:git`) |
|  | bat (fzf preview) |
|  | ctags (`:tag`, `gd`) |
|  | clang-format / rustfmt / prettier / black / gofmt (`:fmt`) |

Outputs `build/hed` and `build/tsi` (the tree-sitter grammar
installer).

---

## Configuration

`src/config.c` is the user-facing surface. The harness calls
`config_init()` once after subsystems are ready:

```c
void config_init(void) {
    /* Plugins. 1 = enabled now, 0 = loaded but inactive (available
     * for runtime swap, e.g. via :keymap). */
    plugin_load(&plugin_core,             1);
    plugin_load(&plugin_vim_keybinds,     1);
    plugin_load(&plugin_emacs_keybinds,   0);
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

    /* Personal overrides — last-write-wins, beats plugin defaults. */
    cmapn(" ff", "fzf");
    /* ... */
}
```

After editing `config.c`, run `:reload` from inside hed to rebuild
and restart.

---

## Plugins

Each in `plugins/<name>/` with its own `README.md`. Summary:

| Plugin | What it does |
|---|---|
| `core` | Default `:` commands + a few editor-wide hooks. Owns `:goto`, `:modeless`, `:plugins`. |
| `vim_keybinds` | Default Vim keymap and text objects. |
| `emacs_keybinds` | Emacs keymap (`C-a`/`C-e`/`C-n`/`C-p`/`C-x` cluster/M- bindings). Modeless. |
| `vscode_keybinds` | VSCode keymap (`Ctrl+S/N/O/P/W/Z/Y/X/C/V/F/G/D`, shift-arrow selection, `Ctrl+Left/Right` word motion). Modeless. |
| `keymap` | `:keymap`, `:keymap-toggle` for runtime keymap swap. |
| `clipboard` | OSC 52 yank → system clipboard. |
| `quickfix_preview` | Cursor → preview sync in quickfix buffers. |
| `dired` | Directory browser (oil.nvim-style). |
| `lsp` | LSP client (WIP). Owns `cJSON`, `:lsp_*` commands. |
| `viewmd` | Markdown live preview. |
| `tmux` | Runner pane. `:tmux_toggle/send/kill/send_line`. |
| `fmt` | `:fmt` runs an external formatter by filetype. |
| `auto_pair` | Auto-insert `()`/`[]`/`{}`/quotes. |
| `smart_indent` | Carry indentation onto new lines. |
| `example` | Starter template — copy and rename to make your own. |

### Adding your own plugin

1. `cp -r plugins/example plugins/myplugin`
2. Rename `example` → `myplugin` inside the files (one symbol, two
   filenames).
3. Implement commands/keybinds/hooks inside `myplugin_init()`.
4. In `src/config.c`:

   ```c
   #include "myplugin/myplugin.h"
   ...
   plugin_load(&plugin_myplugin, 1);
   ```

5. `make` picks it up automatically.

For plugins outside the tree:

```sh
make PLUGINS_DIR=$HOME/my-hed-plugins
```

The Makefile compiles `*.c` from there and adds it to the include
path. See `plugins/example/README.md` for the full recipe.

---

## Keymaps

### Vim (default)

Standard Vim modal: Normal / Insert / Visual / Visual-Line / Visual-
Block / Command. `i`/`a`/`o` enter insert, `<Esc>` exits. `hjkl`,
`w`/`b`/`e`, `0`/`$`, `gg`/`G`, `{`/`}`, `<C-u>`/`<C-d>`. Operators
`d`/`c`/`y` with text objects (`diw`, `ci(`, `ya"`). `42G` jumps to
line 42; `5j` moves down five lines. Folds `za`/`zo`/`zc`/`zR`/`zM`.
Macros `q<reg>`/`@<reg>`/`@@`. Search `/`/`?`, `n`. Visual `v`/`V`/
`<C-v>`. See `plugins/vim_keybinds/README.md`.

### Leader cluster (`<space>` in Vim mode)

Defined in `src/config.c` (your config). Defaults:

```
<space><space>  fzf file picker
<space>ff       fzf
<space>fr       recent files
<space>fc/c     command picker
<space>sd/sa/ss rg / rgword / ssearch in file
<space>ts       send paragraph to tmux
<space>tt/tT    tmux toggle / kill
<space>tq       toggle quickfix
<space>tk       keymap-toggle
<space>ws/wv    split / vsplit
<space>ww/h/j/k/l   focus next / left / down / up / right
<space>jb/jf    jump back / forward
<space>fh/fj    history / jump-list fzf
```

### Emacs

`:keymap emacs`. Modeless (always-insert). `C-a/C-e` line start/end,
`C-b/C-f/C-n/C-p` motion, `C-d` delete forward, `C-k` kill to EOL,
`C-y` yank. `M-f/M-b` word forward/back, `M-x` command palette,
`M-d`/`M-w`. `C-x` cluster: `C-x C-s` save, `C-x C-c` quit, `C-x C-f`
fzf, `C-x b/k/0/2/3/o/u`. Shift+arrow extends a selection;
`Ctrl+Shift+arrow` extends word-wise; `Shift+Home/End` extends to
bol/eol. See `plugins/emacs_keybinds/README.md`.

### VSCode

`:keymap vscode`. Modeless. File: `Ctrl+S` save, `Ctrl+N` new,
`Ctrl+O/P` fzf, `Ctrl+W` close. Edit: `Ctrl+Z/Y` undo/redo,
`Ctrl+X/C/V` cut/copy/paste (line-wise without selection, region-wise
with). Find: `Ctrl+F` search, `Ctrl+D` next-occurrence, `Ctrl+G`
goto. Selection: same shift-arrow / Ctrl-Shift-arrow / Shift-Home/End
shape as Emacs. Command palette: `F1` or `Alt+P` (terminals can't
deliver `Ctrl+Shift+P`). See `plugins/vscode_keybinds/README.md`.

---

## Selected commands

```
:e <file>            edit file or directory
:w / :q / :wq        write / quit / write-quit
:bn / :bp / :ls      buffer next / prev / list
:split / :vsplit     window splits
:goto <line>         jump to line N
:goto <motion> [n]   apply text-object motion N times
:rg / :ssearch / :rgword
:fzf / :recent / :c  fzf pickers
:tag <name>          ctags lookup
:fmt                 run external formatter
:tmux_toggle / :tmux_send <cmd>
:foldnew/rm/toggle/method/update
:keymap [name]       query or switch keymap
:keymap-toggle       cycle vim → emacs → vscode
:modeless on|off     toggle always-insert
:plugins             list loaded plugins
:reload              rebuild and restart
:tsi <lang>          install tree-sitter grammar
:lsp_connect / :lsp_hover / :lsp_definition (WIP)
```

`:keybinds` lists every binding currently registered.

---

## Architecture summary

### The plugin interface

```c
typedef struct Plugin {
    const char *name, *desc;
    int  (*init)(void);
    void (*deinit)(void);
} Plugin;

int plugin_load(const Plugin *p, int enabled);
int plugin_enable(const Plugin *p);
int plugin_disable(const Plugin *p);
```

`plugin_load(p, 1)` registers and runs `init()`. `plugin_load(p, 0)`
just registers (useful for keymap plugins enabled later). Each
plugin's `init()` registers its commands, keybinds, and hooks. There
is no master list / manifest file — `config.c`'s `#include`s and
`plugin_load` calls are the manifest, and unknown symbols become
link errors.

### Hook system

Events: char/line insert/delete, buffer open/close/switch/save,
buffer pre-open and pre-save (intercept hooks), mode change, cursor
movement, keypress.

Pre-open / pre-save let plugins claim ownership of an action by
setting `event->consumed = 1`. The `dired` plugin uses this to handle
directory opens without core knowing about it.

### Keybind dispatch

- Per-mode registration. `mapn`, `mapi`, `mapv`, `mapvb`, `mapvl`,
  `cmapn`, `cmapv`, `cmapi` macros in `src/hed.h`.
- Multi-key sequences (`dd`, `<C-x><C-s>`).
- Numeric prefix: `42G` consumes the count via
  `keybind_get_and_clear_pending_count()`.
- **Last-write-wins** on `(mode, sequence, filetype)` tuples — later
  bindings replace earlier ones. This is how plugin defaults stay
  overridable.

### Modeless

`ed_set_modeless(1)` redirects `MODE_NORMAL → MODE_INSERT` inside
`ed_set_mode`. Used by emacs and vscode keymaps so the user never
sits in normal mode. Toggle with `:modeless on|off|toggle`.

### Renderer

Each frame is built into a single append buffer and written in one
`write()` call, bracketed by `\033[?2026h` … `\033[?2026l`. Modern
terminals (kitty, alacritty 0.13+, wezterm, foot, iTerm2, ghostty)
buffer until ESU and commit atomically. Older terminals ignore the
escape — no flicker fix, no regression.

For tmux: `set -as terminal-features ',alacritty:sync'` (or your
outer-terminal equivalent) lets sync propagate. Without it, tmux
consumes the escape itself but doesn't relay sync semantics.

### Input

`ed_read_key()` in `src/editor.c` parses:
- Plain bytes / ASCII control chars
- Single-byte ESC = `'\x1b'`
- ESC + non-CSI byte = `KEY_META | byte` → `<M-x>`
- CSI sequences: arrows, Home/End, PageUp/Down, Delete, F1–F12
- Modified CSI: `ESC [1;<mod><letter>` and `ESC [<n>;<mod>~`
  decoded with the full xterm modifier matrix (Shift/Alt/Ctrl
  combinations) — yields `KEY_META`, `KEY_CTRL`, `KEY_SHIFT` flags
  OR'd onto the base key.
- SS3: `ESC O P/Q/R/S` for F1–F4 (xterm convention)

`KEY_F1`…`KEY_F12` constants in `src/editor.h`. `key_to_string`
emits `<F1>`…`<F12>` and combinations like `<C-Left>`, `<S-Right>`,
`<M-C-S-Left>`.

---

## File layout

```
src/
├── main.c                # Entry point + select() loop
├── editor.{c,h}          # Global state, ed_set_mode, ed_set_modeless
├── config.{c,h}          # Plugin manifest + personal overrides
├── plugin.{c,h}          # Plugin runtime
├── commands.{c,h}        # Command registry / dispatch
├── command_mode.{c,h}    # `:` UI: completion + Tab→fzf
├── keybinds.{c,h}        # Keybind registry / dispatch
├── keybinds_builtins.{c,h}   # kb_* callbacks
├── hooks.{c,h}           # Hook system
├── hook_builtins.{c,h}   # hook_change_cursor_shape
├── terminal.{c,h}        # Raw mode, ANSI, render loop
├── registers.{c,h}       # Yank / paste registers
├── macros.{c,h}          # Macro recording / playback
├── hed.h                 # Master include + map/cmap macros
├── buf/                  # Buffer + Row + textobj + helpers
├── ui/                   # Window, layout tree, modal windows, abuf
├── utils/                # ts, quickfix, fzf, fold, undo, ctags,
│                         # jump_list, history, recent_files,
│                         # bottom_ui, term_cmd, sed, yank
├── fold_methods/         # bracket / indent fold detection
├── lib/                  # vector, log, errors, sizedstr, strutil,
│                         # safe_string, file_helpers, ansi, theme
└── commands/             # cmd_misc / cmd_search / cmd_util / etc.

plugins/                  # Default, override with PLUGINS_DIR
├── core/                 # Default :commands + minimal hooks
├── vim_keybinds/         # Default Vim keymap
├── emacs_keybinds/       # Emacs keymap (modeless)
├── vscode_keybinds/      # VSCode keymap (modeless)
├── keymap/               # :keymap swap
├── clipboard/            # OSC 52 yank
├── quickfix_preview/     # Cursor → preview sync
├── dired/                # Directory browser
├── lsp/                  # LSP client + cJSON
├── viewmd/               # Markdown live preview
├── tmux/                 # Runner pane integration
├── fmt/                  # External formatters
├── auto_pair/            # Bracket auto-pairing
├── smart_indent/         # Indent carry-over
└── example/              # Starter template

test/        # Unity unit tests
ts/          # Tree-sitter language installer source
ts-langs/    # Compiled tree-sitter .so files
queries/     # Highlight queries by language
tasks/       # Project notes
```

---

## Code conventions

- **Errors** return `EdError` enum (`ED_OK`, `ED_ERR_*`).
- **Bounds**: `BOUNDS_CHECK(idx, len)` and `PTR_VALID(ptr)` from
  `src/lib/errors.h`.
- **Strings**: `SizedStr` (length-tracked) for owned buffers.
- **Vectors**: type-safe macros in `src/lib/vector.h`.
- **Logging**: `log_msg(fmt, ...)` writes to `.hedlog`.
- **Status**: `ed_set_status_message(fmt, ...)` for user feedback.
- **Memory**: manual `malloc`/`free`, null-check allocations.
- **No external dependencies** beyond `libtree-sitter` and `libdl`.

---

## Roadmap

- LSP completion / formatting / diagnostics wiring
- Plugin `deinit()` actually unregistering hooks/cmds/keybinds (needs
  `hook_unregister`, `command_unregister`, `keybind_unregister` —
  none exist yet, so disabling a loaded plugin is currently a no-op)
- Decouple the remaining core ↔ plugin call sites: `lsp_init` in
  `editor.c` and the LSP fd-pump in `main.c` (would migrate to a
  generic plugin-driven select-loop registry)
- More fold methods, more text objects
- Configurable formatter table (currently hard-coded in
  `plugins/fmt/fmt.c`)
- Optional kitty keyboard protocol support — would unlock
  `Ctrl+Shift+letter` and the rest of the modifier matrix that
  xterm-style escapes can't deliver
