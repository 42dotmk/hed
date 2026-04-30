# HED

Modal terminal editor written in C11. hed keeps a small core but ships
useful modern tools: tree-sitter highlights, fuzzy file/search pickers,
ripgrep-backed quickfix lists, window splits, tmux runner support, and
an explicit C API for adding commands, keybindings, and hooks.

> **hed is intended to be built from source.** The editor is composed
> of a minimal **core** plus a collection of **plugins** — without the
> plugins, even basic editing is unusable. Everything user-facing —
> keymaps, the command set, file browser, formatter, clipboard, LSP,
> tree-sitter, runner pane, auto-pair, smart indent — lives in
> `plugins/<name>/`. Your configuration is itself a C file
> (`src/config.c`) where you decide which plugins load and which
> overrides win. You have complete freedom to fork, swap, rip out, or
> add new plugins; rebuild with `make`, then `:reload` from inside hed
> to pick up changes without restarting your session.

## Highlights

- Normal / Insert / Command plus Visual (char + block) modes with
  Vim-like motions and operators, undo/redo, registers, and shared
  clipboard across buffers.
- Three swappable keymaps out of the box: Vim (default), Emacs,
  VSCode. Switch at runtime with `:keymap <name>`.
- Unlimited buffers and multiple windows with vertical/horizontal
  splits; quickfix pane stays in sync with the cursor for previews.
- Search stack: `/` and `*` for intra-file search, ripgrep
  integrations (`:rg`, `:ssearch`, `:rgword`) that populate quickfix,
  and jump list navigation.
- Tree-sitter highlighting with grammars loaded on demand via
  `dlopen`; runtime toggle and per-buffer language selection.
- Fuzzy tools: `:fzf` file picker, `:recent` recent files, `:c`
  command picker, optional `bat` previews.
- Integrations: tmux runner pane, lazygit wrapper, shell passthrough,
  formatter hook, OSC 52 system clipboard over SSH.

## Build from source (the recommended path)

```bash
git clone --recursive https://github.com/42dotmk/hed.git
cd hed
make            # builds build/hed and build/tsi
./build/hed [file ...]
```

If you cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

The makefile auto-discovers `*.c` recursively under `src/` and
`plugins/`. To use a plugin set living outside the repo:

```bash
make PLUGINS_DIR=$HOME/my-hed-plugins
```

After editing `src/config.c` (or any plugin), run `:reload` from
inside hed to rebuild and restart the running editor.

Targets:

```bash
make            # build
make run        # build then run
make clean      # remove build artifacts
make test       # Unity unit tests
make tags       # ctags -R
make install    # copy build/hed and build/tsi to /usr/local/bin
```

Outputs: `build/hed` (editor) and `build/tsi` (tree-sitter grammar
installer). Use `-c "<command>"` to run a `:` command at startup,
e.g., `./build/hed -c "e other.txt"`. Logs go to `.hedlog`.

### Source build requirements

- gcc or clang (C11), `make`, POSIX terminal
- `libdl` (always available on Linux)
- The vendored tree-sitter runtime is included as a submodule under
  `vendor/tree-sitter` and built into a static archive — **no
  `libtree-sitter` system package required.**

Optional runtime tools (each plugin degrades cleanly if its tool is
missing):

- `ripgrep` — `:rg`, `:ssearch`, `:rgword`
- `fzf` — `:fzf`, `:recent`, `:c`, history fzf
- `git` + `cc` — needed by `tsi` to build new tree-sitter grammars
- `tmux`, `lazygit`, `bat`, `ctags`,
  `clang-format` / `rustfmt` / `prettier` / `black` / `gofmt`

## Install (prebuilt binary)

If you'd rather skip the build step, a Linux x86_64 binary is
attached to each release:

```bash
curl -fsSL https://github.com/42dotmk/hed/releases/latest/download/install.sh | bash
```

The script drops `hed` and `tsi` into `~/.local/bin`, offers to
download portable static `fzf` and `ripgrep` binaries into the same
directory, and lets you multi-select tree-sitter grammars to install
up front. Override the destination with `HED_INSTALL_DIR=...`.

The published binary statically links the tree-sitter runtime;
grammars are loaded on demand via `dlopen`.

## Plugins

The core editor knows about buffers, windows, terminal I/O, the
keybind dispatcher, the command registry, and the hook system. **All
user-visible behavior is delivered by plugins.** Each plugin is a
self-contained directory under `plugins/` with its own README.

| Plugin | What it does |
|---|---|
| [`core`](plugins/core/README.md) | Default `:` command set (`:q`, `:w`, `:e`, `:bn`, `:bp`, `:fzf`, `:rg`, `:goto`, `:plugins`, …) and a few editor-wide hooks. |
| [`vim_keybinds`](plugins/vim_keybinds/README.md) | Default Vim-style modal keymap: hjkl motion, operators, text objects, visual modes, macros. |
| [`emacs_keybinds`](plugins/emacs_keybinds/README.md) | Emacs-flavored keymap. Modeless (always-insert), `C-a`/`C-e`/`C-x` cluster, `M-` bindings, shift-arrow selection. |
| [`vscode_keybinds`](plugins/vscode_keybinds/README.md) | VSCode-flavored keymap. Modeless. `Ctrl+S/N/O/P/W/Z/Y/X/C/V/F/G/D`, shift-arrow selection. |
| [`keymap`](plugins/keymap/README.md) | Runtime keymap swap. `:keymap <name>`, `:keymap-toggle`. |
| [`treesitter`](plugins/treesitter/README.md) | Syntax highlighting via tree-sitter. Grammars are `.so` files loaded with `dlopen`. |
| [`clipboard`](plugins/clipboard/README.md) | Mirrors yanks into the system clipboard via OSC 52 — works over SSH, no `xclip`. |
| [`dired`](plugins/dired/README.md) | Oil.nvim-style directory browser. Edit a directory listing as if it were a file. |
| [`tmux`](plugins/tmux/README.md) | Runner pane integration. `:tmux_toggle`, `:tmux_send`, send-paragraph-under-cursor. |
| [`fmt`](plugins/fmt/README.md) | `:fmt` runs an external formatter against the buffer, then reloads it. Filetype-dispatched. |
| [`auto_pair`](plugins/auto_pair/README.md) | Auto-insert matching `()` / `[]` / `{}` / quotes in insert mode. |
| [`smart_indent`](plugins/smart_indent/README.md) | Carry indentation onto new lines. |
| [`quickfix_preview`](plugins/quickfix_preview/README.md) | Live-preview the quickfix entry under the cursor. |
| [`viewmd`](plugins/viewmd/README.md) | Markdown live preview. |
| [`lsp`](plugins/lsp/README.md) | LSP client integration (work in progress). |
| [`example`](plugins/example/README.md) | Starter template — copy and rename to make your own. |

To add your own:

```bash
cp -r plugins/example plugins/myplugin
# rename `example` → `myplugin` in the source files,
# add `plugin_load(&plugin_myplugin, 1);` in src/config.c,
# then make.
```

## Configuration

All user-facing customization lives in `src/config.c`:

- The `plugin_load(&plugin_foo, enabled)` calls determine which
  plugins are active. Pass `0` instead of `1` to register but not
  enable a plugin (useful for keymaps that you swap to at runtime).
- Personal overrides go after the plugin manifest. Keybinds are
  last-write-wins on `(mode, sequence, filetype)` tuples — your
  bindings beat plugin defaults.

Edit, run `make`, and `:reload` from inside hed to pick up changes.

## Project Layout

```
src/                 # core editor (buffers, windows, terminal,
                     # commands, keybinds, hooks, undo, fold, …)
plugins/             # all user-facing functionality (see above)
vendor/tree-sitter/  # vendored runtime, statically linked
ts/                  # tsi (grammar installer) source
ts-langs/            # built grammar .so files (cached)
queries/             # highlight queries by language
test/                # Unity unit tests
```

## Troubleshooting

- Logs: `tail -f .hedlog` while hed runs; clear with `:logclear`.
- `:plugins` lists everything currently loaded; `:keybinds` lists
  every binding registered for the current mode.
- If `fzf` / `ripgrep` / `tmux` / `lazygit` are missing, the related
  commands fail with a status-line message and the rest of the editor
  keeps working.
