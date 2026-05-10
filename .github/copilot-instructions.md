# Copilot Instructions for hed

This document provides essential context for AI coding assistants working on the hed terminal editor codebase.

## Build, Test & Lint

### Build & Run
```bash
make                    # Build hed and tsi binaries
make run                # Build and run hed immediately
make clean              # Remove build/ directory
make distclean           # Clean + remove vendored tree-sitter cache (use after CFLAGS changes)
```

### Testing
```bash
make test               # Run all Unity unit tests
make -C test test       # Run tests from test/ directory directly
```

**Test structure:** Tests use the Unity framework. Test files live in `test/` with `test_*.c` files. The test Makefile (`test/makefile`) compiles against source modules directly (e.g., `src/buf/textobj.c`). To run a single test file:
```bash
make -C test clean && make -C test test_textobj
```

### Code Formatting & Tags
```bash
make fmt                # Run clang-format on all source and header files
make tags               # Generate ctags for code navigation
```

### Optional Tools
- **ripgrep** (`:rg`, `:ssearch`): Fast text search within the editor
- **fzf** (`:fzf`, `:recent`, `:c`): File picker and command palette
- **tmux** (`:tmux_*`): Runner pane integration
- **ctags** (`:tag`, `gd`): Code navigation
- **Formatters** (`:fmt`): clang-format, rustfmt, prettier, black, gofmt
- **lazygit** (`:git`): Git integration

## High-Level Architecture

### Plugin-First Design
hed is built around a plugin system where **most features live as plugins**, not in core. This includes keymaps (vim/emacs/vscode), clipboard, LSP, tmux runner, directory browser, markdown preview, and code formatting.

**Plugin lifecycle:**
1. Plugins are loaded via `plugin_load(&plugin_name, enabled)` in `src/config.c`
2. Each plugin implements `plugin_init()` which registers commands, keybinds, and hooks
3. Plugin registration uses no global manifest file — the includes and `plugin_load` calls *are* the manifest
4. **Last-write-wins**: Later keybind registrations override earlier ones for the same `(mode, sequence, filetype)` tuple

### File Organization

**Core subsystems** (`src/`):
- **editor.c/h**: Global editor state, mode management, modeless toggle
- **config.c/h**: Plugin manifest & user configuration
- **keybinds.c/h**: Keybind registry & multi-key sequence dispatch
- **commands.c/h**: Command registry & `:` command dispatch
- **hooks.c/h**: Event system for buffer operations, mode changes, keypresses
- **input.c/h**: Terminal input parsing (xterm CSI modifier matrix, Meta/Alt, F-keys)
- **plugin.c/h**: Plugin loading & enable/disable lifecycle

**Buffer subsystem** (`src/buf/`):
- **buffer.c/h**: Buffer state (lines, undo, metadata)
- **row.c/h**: Line representation & text operations
- **textobj.c/h**: Text object motions & ranges (tested via `test/test_textobj.c`)

**UI subsystem** (`src/ui/`):
- **window.c/h**: Individual pane/window state
- **wlayout.c/h**: Window tree layout (splits, focus)
- **abuf.c/h**: Append buffer for atomic rendering
- **winmodal.c/h**: Modal windows (command mode, search)

**Utilities** (`src/utils/`): fzf integration, quickfix, undo, jump list, history, recent files, bottom status bar, term_cmd for external commands, sed-style transformations, yank registers

**Keybind system** (`src/keybinds_builtins.c`): Default action callbacks (movement, deletion, etc.)

**Commands** (`src/commands/`): Implementation of `:` commands (misc, search, util, etc.)

### Modeless & Mode Switching
- `ed_set_modeless(1)` redirects all `MODE_NORMAL → MODE_INSERT`, used by emacs/vscode keymaps
- Mode is tracked per-editor state; plugins can hook mode changes via the hook system
- Keybind dispatch is mode-aware: `mapn`, `mapi`, `mapv` register Normal/Insert/Visual modes

### Rendering
- Single append buffer per frame (`abuf`)
- Bracketed by DEC mode 2026 (Synchronized Output) for flicker-free rendering on modern terminals
- `write()` call is atomic

### Input Parsing
- Plain ASCII and control chars directly
- ESC = single escape
- ESC + non-CSI = `KEY_META | byte` (e.g., `<M-x>`)
- CSI sequences: arrows, Home/End, PageUp/Down, function keys
- Full xterm modifier matrix decoded for `<C-Left>`, `<S-Right>`, `<M-C-S-Left>`, etc.
- Helper: `key_to_string()` converts key codes to readable notation (e.g., `<F1>`, `<C-Left>`)

### Synchronous Design
- Editor is single-threaded with a `select()` loop (`src/main.c`)
- All I/O is non-blocking or delegated to subprocesses
- Plugins can integrate async tools (tmux, fzf) via `:` commands that shell out

## Key Conventions

### Error Handling
- Functions return `EdError` enum: `ED_OK`, `ED_ERR_INVALID_INDEX`, `ED_ERR_OUT_OF_BOUNDS`, etc.
- Use `BOUNDS_CHECK(idx, len)` and `PTR_VALID(ptr)` macros from `src/lib/errors.h`
- No exceptions; errors propagate up the call stack

### String Handling
- **SizedStr**: Owned, length-tracked strings for dynamic buffers
- **strutil.h**: String utilities (trim, split, etc.)
- **safe_string.h**: Bounds-checked string operations

### Data Structures
- **vector.h**: Type-safe vector macros (VECTOR_INIT, VECTOR_PUSH, VECTOR_FREE, etc.)
- **stb_ds.h** (vendored): Hash maps & dynamic arrays

### Memory
- Manual `malloc`/`free`; always null-check allocations
- No external dependencies beyond libtree-sitter and libdl
- Tree-sitter runtime is vendored as a static archive (`vendor/tree-sitter`)

### Logging & Status
- `log_msg(fmt, ...)` writes to `.hedlog` for debugging
- `ed_set_status_message(fmt, ...)` displays feedback in the UI status line

### Keybind Macros (src/hed.h)
```c
mapn("<key>", "command_name");        // Normal mode
mapi("<key>", "command_name");        // Insert mode
mapv("<key>", "command_name");        // Visual mode
mapvl("<key>", "command_name");       // Visual-Line mode
mapvb("<key>", "command_name");       // Visual-Block mode
cmapn("<key>", "command_name");       // Command mode
```

### Command Registration
```c
command_register("cmd_name", cmd_impl_func, "Brief help text");
```
Commands can be invoked via `:cmd_name [args]` or bound to keys.

### Hook Registration
```c
hook_register(HOOK_BUFFER_PRE_OPEN, hook_handler_func);
hook_register(HOOK_CHAR_INSERT, hook_handler_func);
```
Hook types: `HOOK_CHAR_INSERT`, `HOOK_CHAR_DELETE`, `HOOK_LINE_INSERT`, `HOOK_LINE_DELETE`, `HOOK_BUFFER_OPEN`, `HOOK_BUFFER_CLOSE`, `HOOK_BUFFER_SWITCH`, `HOOK_BUFFER_SAVE`, `HOOK_BUFFER_PRE_OPEN`, `HOOK_BUFFER_PRE_SAVE`, `HOOK_MODE_CHANGE`, `HOOK_CURSOR_MOVE`, `HOOK_KEY_PRESS`.

Set `event->consumed = 1` to claim ownership of an action (e.g., dired plugin handles directory opens).

### Adding a Plugin
1. Copy `plugins/example` → `plugins/myplugin`
2. Rename `example` → `myplugin` in files and function symbols
3. Implement `myplugin_init()` to register commands, keybinds, hooks
4. In `src/config.c`: `#include "myplugin/myplugin.h"` and `plugin_load(&plugin_myplugin, 1);`
5. `make` auto-discovers and compiles new plugin sources

**Out-of-tree plugins:**
```bash
make PLUGINS_DIR=$HOME/my-hed-plugins
```
Makefile adds `*.c` from that directory to includes and compilation.

### Code Style
- **Minimal comments**: Only clarify non-obvious intent; don't state the obvious
- **Consistent naming**: CamelCase for types, snake_case for functions/vars
- **Error checking**: Always check return values; explicit error propagation
- **No external dependencies** beyond libtree-sitter and libdl (plus vendored code)

## Testing Guidelines

### Unit Tests
- Use Unity framework (`test/unity/`)
- Test individual modules (e.g., `test_textobj.c` tests `src/buf/textobj.c`)
- Run `make -C test test` to build and execute
- Access `.hedlog` for debugging output during tests

### Integration Testing
- Manual testing in the editor (`make run`)
- Use `:plugins` to verify plugin loading
- Use `:keybinds` to inspect registered keybinds
- `:reload` rebuilds and restarts the editor (useful for plugin development)

### Build Verification
- Always run `make clean && make test` before committing
- Tree-sitter compilation is cached in `vendor/build/` to speed up rebuilds
- If CFLAGS change, use `make distclean` to rebuild the static archive

## Configuration & Customization

User config lives in `src/config.c`:
- `config_init()` is called once after subsystems are ready
- `plugin_load()` activates plugins (1 = enabled now, 0 = loaded but inactive)
- `mapn()`, `mapi()`, etc. register personal keybinds (last-write-wins)
- Personal overrides beat plugin defaults

**After editing config.c:**
- Run `:reload` from inside hed to rebuild and restart immediately
- Or `make && ./build/hed` from the terminal

## Plugins Overview

| Plugin | Purpose |
|--------|---------|
| `core` | Default `:` commands & editor hooks |
| `vim_keybinds` | Vim modal editing (default) |
| `emacs_keybinds` | Emacs modeless keymap |
| `vscode_keybinds` | VSCode modeless keymap |
| `keymap` | Runtime keymap swap (`:keymap`) |
| `clipboard` | OSC 52 yank → system clipboard |
| `quickfix_preview` | Cursor → preview sync in quickfix |
| `dired` | Directory browser (oil.nvim-style) |
| `lsp` | LSP client (WIP) |
| `viewmd` | Markdown live preview |
| `tmux` | Runner pane integration |
| `fmt` | External formatter dispatch (`:fmt`) |
| `auto_pair` | Auto-insert brackets/quotes |
| `smart_indent` | Carry indentation to new lines |
| `treesitter` | Syntax highlighting & queries |
| `claude` | Claude integration |
| `sed` | Sed-style transformations |
| `scratch` | Scratch buffers |
| `session` | Session management |
| `reload` | In-editor `:reload` |
| `whichkey` | Keybind hint menu |
| `yazi` | Yazi file manager integration |
| `multicursor` | Multi-cursor editing |
| `example` | Plugin template (copy to start new plugins) |

## Tree-Sitter Integration

- Grammars are compiled `.so` files, loaded on demand with `dlopen`
- Stored in `~/.config/hed/ts/` at runtime
- Install new grammars: `:tsi <lang>` (uses the `tsi` binary)
- Highlight queries live in `queries/<lang>/highlights.scm`
- See `plugins/treesitter/` for integration details

## Important Implementation Notes

- **No modal complexity in core**: Vim/Emacs/VSCode keymaps are all plugins; the editor doesn't know about modes beyond Normal/Insert/Visual
- **Commands are strings**: Keybinds and hooks dispatch via command names, not function pointers
- **Numeric prefix handling**: Consumed via `keybind_get_and_clear_pending_count()` in keybind handlers
- **Visual mode isn't modeless**: Still requires `<Esc>` to exit; the `modeless` toggle only affects Normal ↔ Insert
- **Atomic rendering**: Frame is built into append buffer, then written in one call, bracketed by DEC 2026 for sync terminals
