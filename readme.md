# hed

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
and

Then run `hed` and you're in.

---

## Build from source manually

If you want to skip the installer:

```zsh
git clone --recursive https://github.com/42dotmk/hed.git

cd hed

make
```

The `Makefile` will:

- Compile the binary and place it in `build/hed`
- Download portable `fzf` and `ripgrep` into `build/bin`
- If you have `cargo` and `node`, it'll install plugins in `plugins/` that
  need it (`copilot-language-server` for `:copilot login`, etc.)
- Symlink `build/hed` into `~/.local/bin` (for convenience)

---

## Plugins

**Every user-facing feature is a plugin** in `plugins/<name>/`.

| Plugin | What it does |
|---|---|
| [`aishell`](plugins/aishell/README.md) | Toggle a tmux pane running your AI assistant (Claude, Copilot, etc.) — configurable spawn command |
| [`auto_pair`](plugins/auto_pair/README.md) | Auto-insert matching brackets and quotes |
| [`autosave`](plugins/autosave/README.md) | Idle/timer autosave to per-cwd cache dir, with recovery prompt on reopen |
| [`clipboard`](plugins/clipboard/README.md) | OSC 52 yank to system clipboard (works over SSH) |
| [`core`](plugins/core/README.md) | Default `:` command set (`:q`, `:w`, `:e`, `:bn`, `:fzf`, `:rg`, …) |
| [`copilot`](plugins/copilot/README.md) | GitHub Copilot ghost-text suggestions via `copilot-language-server`, with a `[copilot]` alternatives pane |
| [`ctags`](plugins/ctags/README.md) | Ctags integration for symbol navigation |
| [`dired`](plugins/dired/README.md) | oil.nvim-style directory browser |
| [`emacs_keybinds`](plugins/emacs_keybinds/README.md) | Modeless Emacs keymap (`C-a/C-e`, `M-x`, `C-x` cluster) |
| [`example`](plugins/example/README.md) | Starter template — copy and rename for your own |
| [`fmt`](plugins/fmt/README.md) | `:fmt` runs an external formatter on the buffer |
| [`git`](plugins/git/README.md) | Git integration |
| [`hed_themes`](plugins/hed_themes/README.md) | Theme management |
| [`keymap`](plugins/keymap/README.md) | `:keymap` and `:keymap-toggle` for runtime swap |
| [`lsp`](plugins/lsp/README.md) | LSP client (work in progress) |
| [`mail`](plugins/mail/README.md) | Mail integration |
| [`mail_git_patch`](plugins/mail_git_patch/README.md) | Git patch mail integration |
| [`man`](plugins/man/README.md) | Manual pages viewer |
| [`markdown`](plugins/markdown/README.md) | Markdown rendering support |
| [`multicursor`](plugins/multicursor/README.md) | Extra cursors that mirror every keypress (`:mc_add_below`, `:mc_add_above`) |
| [`open`](plugins/open/README.md) | File opening utilities |
| [`pickers`](plugins/pickers/README.md) | Fuzzy pickers |
| [`quickfix_preview`](plugins/quickfix_preview/README.md) | Live preview of the quickfix entry under the cursor |
| [`reload`](plugins/reload/README.md) | `:reload` rebuilds and execs the new binary |
| [`scratch`](plugins/scratch/README.md) | `:scratch` ephemeral unnamed buffer |
| [`sed`](plugins/sed/README.md) | `:sed <expr>` pipes the buffer through external sed |
| [`selectlist`](plugins/selectlist/README.md) | Selection list functionality |
| [`session`](plugins/session/README.md) | Save / restore the open-buffer list per cwd |
| [`shell`](plugins/shell/README.md) | `:shell` / `!` prompt; capture tokens splice stdout into the buffer or yank register |
| [`smart_indent`](plugins/smart_indent/README.md) | Carry indent onto new lines |
| [`tags`](plugins/tags/README.md) | Tags integration |
| [`tmux`](plugins/tmux/README.md) | Runner pane integration |
| [`translate`](plugins/translate/README.md) | Translation support |
| [`treesitter`](plugins/treesitter/README.md) | Syntax highlighting; grammars via `dlopen` |
| [`viewmd`](plugins/viewmd/README.md) | Markdown live preview in the browser |
| [`vim_keybinds`](plugins/vim_keybinds/README.md) | Default modal Vim keymap |
| [`vscode_keybinds`](plugins/vscode_keybinds/README.md) | Modeless VSCode keymap (`Ctrl+S`, shift-arrow selection) |
| [`whichkey`](plugins/whichkey/README.md) | While a multi-key chord is in progress, list the candidate completions in a 1–4 column table |
| [`yazi`](plugins/yazi/README.md) | Launch the [`yazi`](https://yazi-rs.github.io/) file manager as a chooser; selected paths open as buffers |

### Adding your own plugin

1. `cp -r plugins/example plugins/myplugin`
2. Rename `example` → `myplugin` in the source files
3. Add `plugin_load(&plugin_myplugin, 1)` to `src/config.c`
4. Build with `make`

See [`plugins/example/README.md`](plugins/example/README.md) for the
full recipe and the plugin contract.

### Out-of-tree plugins

Keep your plugin set anywhere on disk and point the build at it:

```sh
make PLUGINS_DIR=$HOME/my-hed-plugins
```

---

## Quickstart

```bash
hed
```

A new buffer comes up.

- `:e` to open files
- `:q` to quit
- `:w` to save

Try `:fzf` to explore the project.

---

## Features

- **Modal editing** (Vim-style) by default, but also ships **Emacs** and
  **VSCode** keymaps you can swap to at runtime (use `:keymap` to
  switch)
- **Tree-sitter syntax** highlighting (with grammar via `dlopen`)
- **Fzf** integration for fuzzy file and command search
- **Tmux** runner pane (to test snippets, open REPLs, etc.)
- **OSC 52** clipboard (to yank to system clipboard, works over SSH)
- **LSP** integration (work in progress)

- **Split windows**, **reloads**, **sessions**
- **Git**, **mail**, **AI shell**, **file manager** (Yazi)
- **Auto-save**, **auto-recover**, **backup**, **undo/redo**

---

## Why

Hed is for you if:

- You like to read documentation, not just code
- You want a single binary with no dependencies (except Tmux)
- You like to hack on your editor, not just use it
- You are a **plugin developer** or **plugin consumer**
- You want to **reinvent the editor** or **tune your own**

The editor was built for one thing: to be a framework and a template,
not a feature-rich text editor.

---

## Files

```
plugins/             # all user-facing functionality (see catalogue above)
```

- `main.c` — Entry point
- `editor.c` — Core editor logic
- `config.c` — Initializer, plugins
- `keybinds.c` — Key mappings
- `commands.c` — Command handling
- `buffer.c` — Buffer operations
- `hooks.c` — Event system
- `terminal.c` — ANSI renderering, Tmux
- `utils/` — Various helpers (search, fzf, etc.)

---

## Plugins Directory

```
plugins/
├── aishell/             # AI shell (like Claude, Copilot)
├── auto_pair/           # Auto-insert matching brackets and quotes
├── autosave/            # Idle/timer autosave
├── clipboard/           # OSC 52 yank to system clipboard
├── core/                # Default `:` commands
├── copilot/             # GitHub Copilot
├── ctags/               # Ctags integration
├── dired/               # oil.nvim-style directory browser
├── emacs_keybinds/      # Emacs keymap
├── example/             # Plugin template
├── fmt/                 # External formatter
├── git/                 # Git integration
├── hed_themes/          # Theme management
├── keymap/              # Runtime keymap swap
├── lsp/                 # LSP client (experimental)
├── mail/                # Mail integration
├── mail_git_patch/      # Git patch mail
├── man/                 # Manual page viewer
├── markdown/            # Markdown rendering
├── multicursor/         # Extra cursors
├── open/                # File opening utilities
├── pickers/             # Fuzzy pickers
├── quickfix_preview/    # Quickfix preview
├── reload/              # Reload binary
├── scratch/             # Scratch buffer
├── sed/                 # External sed
├── selectlist/          # Selection list
├── session/             # Session management
├── shell/               # Shell integration
├── smart_indent/        # Carry indent onto new lines
├── tags/                # Tags integration
├── tmux/                # Tmux runner pane
├── translate/           # Translation support
├── treesitter/          # Tree-sitter syntax highlighting
├── viewmd/              # Markdown preview
├── vim_keybinds/        # Vim keymap
├── vscode_keybinds/     # VSCode keymap
├── whichkey/            # Which-key tooltip
└── yazi/                # Yazi file manager
```

---

## Configuration

To customize, edit `src/config.c`:

```c
void config_init() {
  plugin_load(&plugin_core,             1);
  plugin_load(&plugin_vim_keybinds,     1);
  plugin_load(&plugin_emacs_keybinds,   0);  // registered, swappable
  plugin_load(&plugin_vscode_keybinds,  0);
  plugin_load(&plugin_treesitter,       1);
  plugin_load(&plugin_clipboard,        1);
  plugin_load(&plugin_dired,            1);
  plugin_load(&plugin_tmux,             1);
  plugin_load(&plugin_aishell,          1);
  plugin_load(&plugin_copilot,          1);
  plugin_load(&plugin_example,          0);  // registered, swappable

  // Add your own plugins here.
  // plugin_load(&plugin_myplugin, 1);

  // Personal overrides — last-write-wins, beats plugin defaults.
  // cmapn(" ff", "fzf");  // override plugin keymap
  // cmapn(" q", "quit");  // override plugin keymap
}
```

## Keybindings

All keybindings are user-overridable. Default is:

- `:keymap` to switch between Vim, Emacs, VSCode
- `:plugins` to list currently loaded plugins
- `:reload` to rebuild and exec the new binary

---

## FAQ

### Can I use this with a LSP?

Yes. The LSP client is in `plugins/lsp/` and is not yet enabled by default.

### How do I override a keybinding?

In `src/config.c`, after plugin_load statements, use
`cmapn(" key", "command")` to override or map keys.

### How do I write my own plugin?

See the [`plugins/example/README.md`](plugins/example/README.md) to get
started.

---

## Related Projects

- [`heds`](https://github.com/42dotmk/heds) — A simple, hackable
  terminal web browser to complement `hed`
- [`heds`](https://github.com/42dotmk/heds) — A web browser for the
  terminal to complement `hed`
- [`hed-fzf`](https://github.com/42dotmk/hed-fzf) — Fzf integration
  for `hed`
- [`hed-shell`](https://github.com/42dotmk/hed-shell) — Shell
  integration for `hed`
- [`hed-tmux`](https://github.com/42dotmk/hed-tmux) — Tmux integration
  for `hed`

---

## License

MIT