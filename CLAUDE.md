# HED

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

The binary statically links the tree-sitter runtime, so no
`libtree-sitter` system package is required — the editor runs with
syntax highlighting out of the box. Grammars are loaded on demand
with `dlopen`. To install grammars from inside hed, also grab the
`tsi` helper:

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
  (`config_init` in `src/config.h`). Adding a plugin is one directory
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
make EXTRA_PLUGIN_DIRS=$HOME/my-hed-plugins  # stock plugins + your own
make PLUGINS_DIR=$HOME/my-hed-plugins   # replace the stock plugin set
```

The Makefile auto-discovers `*.c` recursively under `src/`,
`$(PLUGINS_DIR)` (defaults to `plugins/`), and each dir in
`$(EXTRA_PLUGIN_DIRS)` (space-separated, additive). If
`~/.config/hed/config.c` exists (or `USER_CONFIG=path` is given),
it is compiled in as the user config — see Configuration.

### Dependencies

| Required | Optional |
|----------|----------|
| gcc / clang (C11) | ripgrep (`:rg`, `:ssearch`) |
| libdl | fzf (`:fzf`, `:c`, `:recent`, history fzf) |
|  | tmux (runner pane) |
| POSIX terminal | lazygit (`:git`) |
|  | bat (fzf preview) |
|  | ctags (`:tag`, `gd`) |
|  | clang-format / rustfmt / prettier / black / gofmt (`:fmt`) |

Outputs `build/hed` and `build/tsi` (the tree-sitter grammar
installer).

---

## Configuration

Config is split in two layers, both compiled in:

- **Base config** — `src/config.h`, shipped in-tree. Defines
  `config_load_default_plugins()` (the stock plugin set) and
  `config_load_defaults()` (default theme + leader keybinds).
- **User config** — `~/.config/hed/config.c`, optional and
  user-owned (override the path with `make USER_CONFIG=path`). If
  present, the Makefile compiles it in; it must define
  `config_user_init()`.

The harness calls `config_init()` once after subsystems are ready;
it loads the defaults, then calls `config_user_init()` — declared
`weak`, so the build links fine without a user config. The user
config is therefore purely additive: `plugin_load()` enables extra
plugins, and rebinding a key overrides the default via
last-write-wins. To *remove* stock plugins, edit `src/config.h`.

```c
/* ~/.config/hed/config.c */
#include "hed.h"

extern const Plugin plugin_myplugin;   /* from EXTRA_PLUGIN_DIRS */

void config_user_init(void) {
    /* 1 = enabled now, 0 = loaded but inactive (available for
     * runtime swap, e.g. via :keymap). */
    plugin_load(&plugin_myplugin, 1);

    /* Overrides — last-write-wins, beats defaults. */
    cmapn(" ff", "recent", "recent files");
}
```

After editing either config, run `:reload` from inside hed to
rebuild and restart. Note: `:reload` runs plain `make`, so
`EXTRA_PLUGIN_DIRS` must come from the environment (it is `?=` in
the Makefile), not the command line, to survive reloads.

---

## Plugins

Each in `plugins/<name>/` with its own `README.md`. Summary:

| Plugin | What it does |
|---|---|
| `aishell` | AI shell integration |
| `auto_pair` | Auto-insert `()`/`[]`/`{}`/quotes. |
| `autosave` | Auto-save functionality |
| `clipboard` | OSC 52 yank → system clipboard. |
| `core` | Default `:` commands + a few editor-wide hooks. Owns `:goto`, `:modeless`, `:plugins`. |
| `copilot` | GitHub Copilot integration |
| `ctags` | Ctags integration for symbol navigation |
| `dired` | Directory browser (oil.nvim-style). |
| `emacs_keybinds` | Emacs keymap (`C-a`/`C-e`/`C-n`/`C-p`/`C-x` cluster/M- bindings). Modeless. |
| `example` | Starter template — copy and rename to make your own. |
| `fmt` | `:fmt` runs an external formatter by filetype. |
| `git` | Git integration |
| `hed_themes` | Theme management |
| `keymap` | `:keymap`, `:keymap-toggle` for runtime keymap swap. |
| `lsp` | LSP client (WIP). Owns `cJSON`, `:lsp_*` commands. |
| `mail` | Mail integration |
| `mail_git_patch` | Git patch mail integration |
| `man` | Manual pages viewer |
| `markdown` | Markdown rendering support |
| `mouse` | Mouse: click places cursor, drag selects, wheel scrolls. `:mouse on\|off\|toggle`. |
| `multicursor` | Multiple cursor support |
| `open` | File opening utilities |
| `pickers` | Fuzzy pickers |
| `quickfix_preview` | Cursor → preview sync in quickfix buffers. |
| `reload` | Reload functionality |
| `scratch` | Scratch buffer support |
| `sed` | Stream editor integration |
| `selectlist` | Selection list functionality |
| `session` | Session management |
| `shell` | Shell integration |
| `smart_indent` | Carry indentation onto new lines. |
| `tags` | Tags integration |
| `tmux` | Runner pane. `:tmux_toggle/send/kill/send_line`. |
| `translate` | Translation support |
| `treesitter` | Tree-sitter integration |
| `viewmd` | Markdown live preview. |
| `vim_keybinds` | Default Vim keymap and text objects. |
| `vscode_keybinds` | VSCode keymap (`Ctrl+S/N/O/P/W/Z/Y/X/C/V/F/G/D`, shift-arrow selection, `Ctrl+Left/Right` word motion). Modeless. |
| `whichkey` | Which-key tooltip support |
| `yazi` | Yazi file manager integration |

### Adding your own plugin

1. `cp -r plugins/example plugins/myplugin`
2. Rename `example` → `myplugin` inside the files (one symbol, two
   filenames).
3. Implement commands/keybinds/hooks inside `myplugin_init()`.
4. In `src/config.h`:

   ```c
   #include "myplugin/myplugin.h"
   ...
   plugin_load(&plugin_myplugin, 1);
   ```

5. `make` picks it up automatically.

For plugins outside the tree, keep them in your own dir and load
them from your user config (`~/.config/hed/config.c`):

```sh
export EXTRA_PLUGIN_DIRS=$HOME/my-hed-plugins   # additive to plugins/
make
```

The Makefile compiles `*.c` from each extra dir and adds it to the
include path. Plugin symbol names must be unique across all dirs —
a duplicate is a link error. `PLUGINS_DIR=` still exists to swap
out the stock set entirely. See `plugins/example/README.md` for
the full recipe.

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

Defined in `src/config.h` (defaults) and extendable from
`~/.config/hed/config.c`. Defaults:

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
`Ctrl+O/P` fzf, `Ctrl+E` recent, `Ctrl+W` close, `Ctrl+\` split,
`Ctrl+PageUp/Down` buffer prev/next. Edit: `Ctrl+Z/Y` undo/redo,
`Ctrl+X/C/V` cut/copy/paste (line-wise without selection, region-wise
with), `Del`/`Ctrl+Backspace`/`Ctrl+Del` forward/word deletes,
`Alt+Up/Down` move line, `Shift+Alt+Down` duplicate, `Ctrl+/` comment,
`Shift+Alt+F` format. Find: `Ctrl+F` search, `F3` next, `Ctrl+G` goto
line, `Alt+Left/Right` jump back/forward, `F12` ctags definition.
Multi-cursor: `Ctrl+D` next occurrence, `Ctrl+Alt+Up/Down` add
cursor, `Ctrl+K Ctrl+S` sync edits. Selection: shift-arrow /
Ctrl-Shift-arrow / Shift-Home/End as in Emacs, plus `Ctrl+A` select
all and `Ctrl+L` select line. Folds: `Ctrl+K` chords. Command
palette: `F1` or `Alt+P` (terminals can't deliver `Ctrl+Shift+P`).
See `plugins/vscode_keybinds/README.md`.

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
is no master list / manifest file — the `#include`s and
`plugin_load` calls in `src/config.h` (plus the user config's
`extern` declarations) are the manifest, and unknown symbols become
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
├── main.c                  # Entry point + select() loop
├── editor.{c,h}            # Global state, ed_set_mode, ed_set_modeless
├── terminal.{c,h}          # Raw mode, ANSI, render loop
├── select_loop.{c,h}       # fd + timer registry for the main select() loop
├── plugin.{c,h}            # Plugin runtime
├── hooks.{c,h}             # Hook system
├── hook_builtins.{c,h}     # hook_change_cursor_shape
├── config.h                # Base config: plugin manifest + defaults
│                           # (user config: ~/.config/hed/config.c)
├── hed.h                   # Master include + map/cmap macros
├── input/                  # Input + dispatch:
│                           #   input, keybinds, keybinds_builtins,
│                           #   macros, registers, prompt, command_mode,
│                           #   picker
├── commands/               # Full command system:
│                           #   registry.{c,h} + cmd_misc / cmd_util /
│                           #   commands_buffer / commands_ui /
│                           #   cmd_builtins.h
├── buf/                    # Buffer + Row + textobj + attrspan +
│                           # virtual_text + helpers
├── ui/                     # Window, layout tree, modal windows, abuf,
│                           # ask, bottom_ui (status bar)
├── fs/                     # File I/O + path helpers
├── utils/                  # Editor-domain helpers (own state, depend
│                           # on Buffer/Window/E):
│                           #   undo, history, recent_files, jump_list,
│                           #   quickfix, fzf, yank, term_cmd, fold,
│                           #   fold_methods (registry only)
└── lib/                    # Stateless leaves (no editor state, no
                            # singletons; drop-in reusable):
                            #   ansi, cursor, errors, log, path_limits,
                            #   safe_string, sizedstr, stb_ds, strutil,
                            #   theme, vector

plugins/                    # Stock set; add dirs with EXTRA_PLUGIN_DIRS,
                            # replace with PLUGINS_DIR
├── core/                   # Default :commands + minimal hooks
├── vim_keybinds/           # Default Vim keymap
├── emacs_keybinds/         # Emacs keymap (modeless)
├── vscode_keybinds/        # VSCode keymap (modeless)
├── keymap/                 # :keymap swap
├── clipboard/              # OSC 52 yank
├── quickfix_preview/       # Cursor → preview sync
├── dired/                  # Directory browser
├── lsp/                    # LSP client + cJSON
├── viewmd/                 # Markdown live preview
├── tmux/                   # Runner pane integration
├── fmt/                    # External formatters
├── auto_pair/              # Bracket auto-pairing
├── smart_indent/           # Indent carry-over
├── folds/                  # bracket + indent fold methods +
│                           # filetype defaults
├── pickers/                # fzf-backed implementations registered
│                           # via picker_invoke (src/input/picker.{c,h})
├── treesitter/             # Tree-sitter highlighter (self-driven via
│                           # HOOK_BUFFER_OPEN / HOOK_RENDER_PRE)
├── markdown/               # Markdown highlighter + fold method
└── example/                # Starter template

test/        # Unity unit tests
ts/          # Tree-sitter language installer source
ts-langs/    # Compiled tree-sitter .so files
queries/     # Highlight queries by language
tasks/       # Project notes
```

### `lib/` vs `utils/`

These two are *not* interchangeable; pick the right one when adding
a file.

- **`lib/`** — stateless leaves. No editor state, no singletons, no
  dependency on `editor.h`, no inclusion of `Buffer`/`Window`/`E`.
  Anything you could lift into another C project unchanged. Examples:
  `sizedstr`, `strutil`, `errors`, `log`, `vector`, `ansi`, `theme`.
- **`utils/`** — editor-domain helpers. Own runtime state, depend on
  `Buffer`/`Window`/`E`, or hook into the core lifecycle. Not reusable
  outside hed, but also not part of the core runtime spine. Examples:
  `undo`, `history`, `jump_list`, `quickfix`, `fzf`, `fold_methods`
  (the registry — methods themselves are in `plugins/folds/`).

If a file would need to grow editor state to do its job, it belongs
in `utils/`. If you have to fight to keep it from doing so, it might
belong in the core runtime spine (top-level `src/`) instead.

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
  
