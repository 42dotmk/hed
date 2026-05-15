j hed

A small sized(<1MB), hackable terminal editor written in C.

Modal by default, but ships Emacs and VSCode keymaps you can swap to
at runtime. Tree-sitter highlights, ripgrep / fzf integrations,
window splits, tmux runner pane, OSC 52 clipboard, LSP. Every
user-facing feature is a plugin you can rip out, fork, or replace.

---

## Install in 10 seconds
One line install:

```sh
curl -fsSL https://github.com/42dotmk/hed/releases/latest/download/install.sh | bash
```
and follow the prompts.


The installer asks two things:

1. **Source or binary?** Binary is faster (one download, ready to
   run). Source clones the repo into `~/.local/share/hed`, builds it,
   and symlinks `hed` and `tsi` into `~/.local/bin` — pick this if
   you want to hack on plugins (or the core editor itself).
   
2. **Optional tools?** Offers to download portable static `fzf` and
   `ripgrep` binaries into the same `~/.local/bin`, then lets you
   pick tree-sitter grammars to install for syntax highlighting.

No `sudo`. No package manager. Everything ends up under
`~/.local/`.

After install, make sure `~/.local/bin` is on your `PATH`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

Then run `hed` and you're in.

---

## Build from source manually

If you want to skip the installer:

```zsh
git clone --recursive https://github.com/42dotmk/hed.git
cd hed
make
./build/hed
```

If you forgot `--recursive`:

```bash
git submodule update --init --recursive
```
	Note: --recursive is needed so it can clone the vendored tree-sitter runtime.`

Targets:

```bash
make           # build build/hed and build/tsi
make install   # symlink build/hed and build/tsi into ~/.local/bin
make run       # build then run
make clean     # remove build/
make test      # Unity unit tests
```

`make install` symlinks rather than copies — rebuilds (`make`,
`:reload`) update the installed binary automatically.

### Build requirements

- gcc or clang, C11
- POSIX terminal, libdl

The vendored tree-sitter runtime is included as a submodule under
`vendor/tree-sitter` and statically linked. **No `libtree-sitter`
system package required.**

### Optional runtime tools

Each integration degrades cleanly if its tool is missing:

| Tool | Used by |
|---|---|
| `ripgrep` | `:rg`, `:ssearch`, `:rgword` |
| `fzf` | `:fzf`, `:recent`, `:c`, history pickers |
| `tmux` | runner pane (`:tmux_toggle`, `:tmux_send_line`) |
| `lazygit` | `:git` |
| `bat` | fzf previews |
| `ctags` | `:tag` |
| `clang-format` / `rustfmt` / `prettier` / `black` / `gofmt` / `shfmt` | `:fmt` |
| [`yazi`](https://yazi-rs.github.io/) | `:yazi` file-manager picker |
| [`copilot-language-server`](https://www.npmjs.com/package/@github/copilot-language-server) | `copilot` plugin (`:copilot login`, ghost-text suggestions) |
| `git`, `cc` | `tsi` (tree-sitter grammar installer) |

---

## Highlights

- Vim-like modal editing by default, with full operator + text-object
  composition (`diw`, `ci(`, `ya"`, `>i{`, …) and the usual undo /
  redo, registers, macros, and search stack.
- **Three swappable keymaps**: Vim (default), Emacs (modeless,
  `C-a/C-e/C-x` cluster), VSCode (modeless, `Ctrl+S/Z/F/D`,
  shift-arrow selection). Toggle at runtime with `:keymap`.
- **whichkey hints**: pause partway through a chord (e.g. after
  `<space>`) and a sorted, 1–4-column table of completions appears
  in the message bar. Toggle with `<space>th`.
- **Multiple cursors**: `:mc_add_below` / `:mc_add_above` add extra
  cursors that mirror every subsequent keystroke through the full
  dispatch pipeline.
- **Tree-sitter highlighting** for any language you install via
  `tsi`. Grammars load on demand with `dlopen`.
- **Fuzzy pickers**: `:fzf` files, `:recent` recent, `:c` commands,
  history fzf, jump-list fzf — all powered by `fzf`.
- **ripgrep + quickfix**: `:rg`, `:rgword`, `:ssearch` populate a
  quickfix buffer with live preview as the cursor moves.
- **Splits & windows**: vertical / horizontal splits, window focus
  navigation, modal floating windows.
- **tmux runner pane**: send the paragraph under your cursor to a
  shell pane next door.
- **System clipboard over SSH** via OSC 52 — no `xclip`, `pbcopy`, or
  `wl-copy` shelling out.
- **`:reload`** rebuilds the editor and execs the new binary in
  place, restoring all open buffers.
- **No flicker** on terminals that support DEC mode 2026 (kitty,
  alacritty, wezterm, foot, ghostty).

---

## Plugins

The core editor knows about buffers, windows, terminal I/O, the
keybind dispatcher, the command registry, and the hook system.
**Every user-facing feature is a plugin** in `plugins/<name>/`.
Each has its own README; here's the catalogue:

| Plugin | What it does |
|---|---|
| [`core`](plugins/core/README.md) | Default `:` command set (`:q`, `:w`, `:e`, `:bn`, `:fzf`, `:rg`, …) |
| [`vim_keybinds`](plugins/vim_keybinds/README.md) | Default modal Vim keymap |
| [`emacs_keybinds`](plugins/emacs_keybinds/README.md) | Modeless Emacs keymap (`C-a/C-e`, `M-x`, `C-x` cluster) |
| [`vscode_keybinds`](plugins/vscode_keybinds/README.md) | Modeless VSCode keymap (`Ctrl+S`, shift-arrow selection) |
| [`keymap`](plugins/keymap/README.md) | `:keymap` and `:keymap-toggle` for runtime swap |
| [`whichkey`](plugins/whichkey/README.md) | While a multi-key chord is in progress, list the candidate completions in a 1–4 column table |
| [`multicursor`](plugins/multicursor/README.md) | Extra cursors that mirror every keypress (`:mc_add_below`, `:mc_add_above`) |
| [`treesitter`](plugins/treesitter/README.md) | Syntax highlighting; grammars via `dlopen` |
| [`clipboard`](plugins/clipboard/README.md) | OSC 52 yank to system clipboard (works over SSH) |
| [`dired`](plugins/dired/README.md) | oil.nvim-style directory browser |
| [`tmux`](plugins/tmux/README.md) | Runner pane integration |
| [`claude`](plugins/claude/README.md) | Toggle a tmux pane running Claude Code (rides the tmux pane registry) |
| [`copilot`](plugins/copilot/README.md) | GitHub Copilot ghost-text suggestions via `copilot-language-server`, with a `[copilot]` alternatives pane |
| [`fmt`](plugins/fmt/README.md) | `:fmt` runs an external formatter on the buffer |
| [`auto_pair`](plugins/auto_pair/README.md) | Auto-insert matching brackets and quotes |
| [`smart_indent`](plugins/smart_indent/README.md) | Carry indent onto new lines |
| [`quickfix_preview`](plugins/quickfix_preview/README.md) | Live preview of the quickfix entry under the cursor |
| [`viewmd`](plugins/viewmd/README.md) | Markdown live preview in the browser |
| [`scratch`](plugins/scratch/README.md) | `:scratch` ephemeral unnamed buffer |
| [`sed`](plugins/sed/README.md) | `:sed <expr>` pipes the buffer through external sed |
| [`shell`](plugins/shell/README.md) | `:shell` / `!` prompt; capture tokens splice stdout into the buffer or yank register |
| [`reload`](plugins/reload/README.md) | `:reload` rebuilds and execs the new binary |
| [`session`](plugins/session/README.md) | Save / restore the open-buffer list per cwd |
| [`autosave`](plugins/autosave/README.md) | Idle/timer autosave to per-cwd cache dir, with recovery prompt on reopen |
| [`yazi`](plugins/yazi/README.md) | Launch the [`yazi`](https://yazi-rs.github.io/) file manager as a chooser; selected paths open as buffers |
| [`lsp`](plugins/lsp/README.md) | LSP client (work in progress) |
| [`example`](plugins/example/README.md) | Starter template — copy and rename for your own |

---

## Configuration

All user-facing customization lives in `src/config.c`:

```c
void config_init(void) {
    plugin_load(&plugin_core,             1);
    plugin_load(&plugin_vim_keybinds,     1);
    plugin_load(&plugin_emacs_keybinds,   0);  // registered, swappable
    plugin_load(&plugin_vscode_keybinds,  0);
    plugin_load(&plugin_treesitter,       1);
    plugin_load(&plugin_clipboard,        1);
    /* ... */

    /* Personal overrides — last-write-wins, beats plugin defaults. */
    cmapn(" ff", "fzf");
    cmapn(" rr", "reload");
    /* ... */
}
```

Edit, run `make`, and `:reload` from inside the editor to pick up
the changes — no need to quit and relaunch.

### Adding your own plugin

```bash
cp -r plugins/example plugins/myplugin
# rename example → myplugin in the source files
# add plugin_load(&plugin_myplugin, 1) to src/config.c
make
```

See [`plugins/example/README.md`](plugins/example/README.md) for the
full recipe and the plugin contract.

### Out-of-tree plugins

Keep your plugin set anywhere on disk and point the build at it:

```bash
make PLUGINS_DIR=$HOME/my-hed-plugins
```

---

## Project layout

```
src/                 # core editor (buffers, windows, terminal,
                     # commands, keybinds, hooks, undo, fold, …)
plugins/             # all user-facing functionality (see catalogue above)
vendor/tree-sitter/  # vendored runtime, statically linked
ts/                  # tsi (grammar installer) source
queries/             # tree-sitter highlight queries by language
test/                # Unity unit tests
```

---

## Troubleshooting

- **Logs**: `~/.cache/hed/<encoded-cwd>/log`. Tail it while hed runs;
  clear with `:logclear`.
- **`:plugins`** lists everything currently loaded.
- **`:keybinds`** lists every binding registered for the active
  mode.
- If `fzf`, `ripgrep`, `tmux`, or `lazygit` are missing, related
  commands fail with a status-line message and the rest of the
  editor keeps working.
- If `:reload` fails to rebuild, the error goes to the status line
  — open `~/.cache/hed/<encoded-cwd>/log` for the full output.
