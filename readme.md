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

`make` only compiles. It builds two binaries into `build/`:

- `build/hed` — the editor
- `build/tsi` — the tree-sitter grammar installer

Then symlink both into `~/.local/bin` (rebuilds are picked up
automatically — no reinstall needed):

```zsh
make install-dev
```

For a system-wide / packaging install instead, `make install` copies the
binaries, man pages and license into an FHS layout under `PREFIX`
(default `/usr/local`), honoring `DESTDIR`:

```zsh
sudo make install PREFIX=/usr            # -> /usr/bin, /usr/share/man, ...
make install PREFIX=/usr DESTDIR=./stage # stage for a package
```

The extras — portable `fzf`/`ripgrep`, tree-sitter grammars, and
`copilot-language-server` (for `:copilot login`) — are fetched by the
one-line `install.sh` above, not by the build.

### Packages

Homebrew (Linux x86_64):

```zsh
brew tap 42dotmk/hed && brew install hed
```

Pre-built `.deb` and `.rpm` are attached to each
[release](https://github.com/42dotmk/hed/releases/latest), and an
`apt`/`dnf` repo is served from GitHub Pages once enabled (see
[`packaging/`](packaging/README.md)). Distro recipes (Arch `PKGBUILD`, Void
template) and the full packaging notes — including how to publish to the
AUR — live in [`packaging/`](packaging/README.md) too.

### Man pages

`make man` generates roff man pages from this README and every
`plugins/*/README.md` (one per plugin) into `man/man1/` — `hed.1`,
`hed-tmux.1`, `hed-vim_keybinds.1`, and so on. Requires `pandoc`.

The build bakes that path into the binary, so the `man` plugin can
open them from inside the editor with `:man hed`, `:man hed-tmux`,
etc. — no system install needed. To put them on the system `MANPATH`
(so `man hed` / `apropos hed` work in any shell too):

```zsh
make install-man        # -> ~/.local/share/man/man1
```

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
| [`folds`](plugins/folds/) | Bracket + indent fold methods and per-filetype defaults |
| [`git`](plugins/git/README.md) | Git integration |
| [`hed_themes`](plugins/hed_themes/) | Theme management |
| [`keymap`](plugins/keymap/README.md) | `:keymap` and `:keymap-toggle` for runtime swap |
| [`lsp`](plugins/lsp/README.md) | LSP client (work in progress) |
| [`mail`](plugins/mail/README.md) | Mail integration |
| [`mail_git_patch`](plugins/mail_git_patch/README.md) | Git patch mail integration |
| [`man`](plugins/man/) | Manual pages viewer |
| [`markdown`](plugins/markdown/) | Markdown rendering support |
| [`mouse`](plugins/mouse/README.md) | Click to place cursor, drag to select, wheel to scroll (`:mouse on\|off\|toggle`) |
| [`multicursor`](plugins/multicursor/README.md) | Extra cursors that mirror every keypress (`:mc_add_below`, `:mc_add_above`) |
| [`open`](plugins/open/) | File opening utilities |
| [`pickers`](plugins/pickers/README.md) | Fuzzy pickers |
| [`quickfix_preview`](plugins/quickfix_preview/README.md) | Live preview of the quickfix entry under the cursor |
| [`reload`](plugins/reload/README.md) | `:reload` rebuilds and execs the new binary |
| [`scratch`](plugins/scratch/README.md) | `:scratch` ephemeral unnamed buffer |
| [`search`](plugins/search/) | In-file / project search (rg / ssearch) helpers |
| [`sed`](plugins/sed/README.md) | `:sed <expr>` pipes the buffer through external sed |
| [`selectlist`](plugins/selectlist/) | Selection list functionality |
| [`session`](plugins/session/README.md) | Save / restore the open-buffer list per cwd |
| [`shell`](plugins/shell/README.md) | `:shell` / `!` prompt; capture tokens splice stdout into the buffer or yank register |
| [`smart_indent`](plugins/smart_indent/README.md) | Carry indent onto new lines |
| [`tasks`](plugins/tasks/README.md) | Task list / TODO management |
| [`tmux`](plugins/tmux/README.md) | Runner pane integration |
| [`translate`](plugins/translate/) | Translation support |
| [`treesitter`](plugins/treesitter/README.md) | Syntax highlighting; grammars via `dlopen` |
| [`viewmd`](plugins/viewmd/README.md) | Markdown live preview in the browser |
| [`vim_keybinds`](plugins/vim_keybinds/README.md) | Default modal Vim keymap |
| [`vscode_keybinds`](plugins/vscode_keybinds/README.md) | Modeless VSCode keymap (`Ctrl+S`, shift-arrow selection) |
| [`whichkey`](plugins/whichkey/README.md) | While a multi-key chord is in progress, list the candidate completions in a 1–4 column table |
| [`yazi`](plugins/yazi/README.md) | Launch the [`yazi`](https://yazi-rs.github.io/) file manager as a chooser; selected paths open as buffers |

### Adding your own plugin

1. `cp -r plugins/example plugins/myplugin`
2. Rename `example` → `myplugin` in the source files
3. Add `plugin_load(&plugin_myplugin, 1)` to `src/config.h` (or load
   it from your user config — see Configuration)
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
  **VSCode** keymaps you can swap to at runtime (`:keymap <name>` to
  switch, `:keymap-toggle` to cycle)
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
- You want a single self-contained binary (links only `libdl`; the
  tree-sitter runtime is statically vendored in). Tools like `fzf`,
  `ripgrep`, and `tmux` are optional and only used when present
- You like to hack on your editor, not just use it
- You are a **plugin developer** or **plugin consumer**
- You want to **reinvent the editor** or **tune your own**

The editor was built for one thing: to be a framework and a template,
not a feature-rich text editor.

---

## Files

```
src/
├── main.c              # Entry point + select() loop
├── editor.c            # Core editor state, modes
├── config.h            # Base config: plugin manifest + defaults
├── hooks.c             # Event system
├── terminal.c          # ANSI rendering, sync output
├── input/              # keybinds, macros, registers, command mode
├── commands/           # command registry + built-ins
├── buf/                # buffer, rows, text objects
├── ui/                 # windows, layout, status bar
├── fs/                 # file I/O + path helpers
├── utils/              # editor helpers (undo, history, fzf, fold, …)
└── lib/                # stateless leaves (strings, theme, vector, …)

plugins/                # all user-facing functionality (catalogue above)
```

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
├── folds/               # Bracket + indent fold methods
├── git/                 # Git integration
├── hed_themes/          # Theme management
├── keymap/              # Runtime keymap swap
├── lsp/                 # LSP client (experimental)
├── mail/                # Mail integration
├── mail_git_patch/      # Git patch mail
├── man/                 # Manual page viewer
├── markdown/            # Markdown rendering
├── mouse/               # Mouse: click / drag-select / wheel scroll
├── multicursor/         # Extra cursors
├── open/                # File opening utilities
├── pickers/             # Fuzzy pickers
├── quickfix_preview/    # Quickfix preview
├── reload/              # Reload binary
├── scratch/             # Scratch buffer
├── search/              # In-file / project search helpers
├── sed/                 # External sed
├── selectlist/          # Selection list
├── session/             # Session management
├── shell/               # Shell integration
├── smart_indent/        # Carry indent onto new lines
├── tasks/               # Task list / TODO management
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

Config lives in two layers, both compiled in:

- **Base config** — `src/config.h`, shipped in-tree. It defines the
  stock plugin manifest (`config_load_default_plugins()`) and the
  default theme + leader keybinds (`config_load_defaults()`). Edit
  this to change or remove stock plugins.
- **User config** — `~/.config/hed/config.c`, optional and
  user-owned. If present, the Makefile compiles it in and runs it
  *after* the defaults, so it's purely additive: enable extra
  plugins, and override any keybind via last-write-wins.

A user config just needs to define `config_user_init()`:

```c
/* ~/.config/hed/config.c */
#include "hed.h"

extern const Plugin plugin_myplugin;   /* from EXTRA_PLUGIN_DIRS */

void config_user_init(void) {
  plugin_load(&plugin_myplugin, 1);    // 1 = enabled, 0 = swappable

  // Personal overrides — last-write-wins, beats plugin defaults.
  cmapn(" ff", "recent", "recent files");
}
```

After editing either layer, run `:reload` to rebuild and restart.

## Keybindings

All keybindings are user-overridable. Default is:

- `:keymap` shows the current keymap; `:keymap <name>` switches to
  Vim / Emacs / VSCode, and `:keymap-toggle` cycles through them
- `:plugins` to list currently loaded plugins
- `:reload` to rebuild and exec the new binary

---

## FAQ

### Can I use this with a LSP?

Yes. The LSP client is in `plugins/lsp/` and is not yet enabled by default.

### How do I override a keybinding?

In your user config (`~/.config/hed/config.c`) or in
`src/config.h`'s `config_load_defaults()`, use
`cmapn(" key", "command")` to override or map keys. Later
registrations win, so a user-config binding beats the stock default.

### How do I write my own plugin?

See the [`plugins/example/README.md`](plugins/example/README.md) to get
started.

---

## License

MIT
