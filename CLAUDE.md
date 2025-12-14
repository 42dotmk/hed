# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

hed is a modal terminal editor written in C23 with Vim-like bindings. It integrates tree-sitter for syntax highlighting, ripgrep for search, fzf for fuzzy finding, and tmux for runner pane support. The codebase emphasizes a small core with explicit C APIs for extensibility through commands, keybindings, and hooks.

## Build Commands

```bash
# Build the project (outputs build/hed and build/tsi)
make

# Clean build artifacts
make clean

# Format all source files with clang-format
make fmt

# Run the editor after building
make run
```

The build system:
- Uses clang with C23 standard (`-std=c23`)
- Links against `libtree-sitter` dynamically (`-ltree-sitter`)
- Produces two binaries: `build/hed` (the editor) and `build/tsi` (tree-sitter language installer)
- Copies tree-sitter grammars from `ts-langs/` during build

## Running hed

```bash
# Open files
./build/hed [file ...]

# Execute a command at startup
./build/hed -c "e other.txt"
./build/hed -c "q!"
```

Logs are written to `.hedlog` in the current directory.

## Architecture Overview

### Core Data Flow

1. **Main Loop** (`src/main.c`): Uses `select()` to wait for stdin input, then calls `ed_process_keypress()` to handle keys and `ed_render_frame()` to redraw the screen.

2. **Editor State** (`src/editor.c`): Global `Ed E` structure contains:
   - `buffers`: Array of all open buffers
   - `current_buffer`: Index of active buffer
   - `mode`: Current editor mode (Normal/Insert/Visual/Command)
   - Window layout tree and focus tracking
   - Quickfix list, jump list, search state, registers

3. **Buffer System** (`src/buf/`):
   - `Buffer`: Contains rows, cursor position, filename, filetype, tree-sitter state
   - `Row`: Line data with both raw chars and rendered display (for tabs/unicode)
   - `textobj.c`: Text object operations (words, paragraphs, delimiters)

4. **Command Execution** (`src/commands.c`):
   - Commands registered via `command_register(name, callback, desc)` in `src/config.c`
   - User types `:command args` which gets parsed and dispatched to the callback
   - Command callbacks receive args as a string

5. **Keybinding Dispatch** (`src/keybinds.c`):
   - Keybinds registered per-mode via `keybind_register(mode, key_sequence, callback)`
   - Built-in keybind functions in `src/keybinds_builtins.c`
   - User configuration in `src/config.c` using `mapn/mapi/mapv/mapvb` macros

6. **Hook System** (`src/hooks.c`, `src/hooks.h`):
   - Events: char insert/delete, line insert/delete, buffer lifecycle, mode change, cursor movement
   - Callbacks registered with mode and filetype filters
   - Quickfix preview synchronization implemented via cursor movement hook in `src/user_hooks_quickfix.c`

### Key Subsystems

**Tree-sitter Integration** (`src/utils/ts.c`):
- Grammars loaded dynamically as `.so` files from `$HED_TS_PATH` (defaults to `~/.config/hed/ts` or `ts-langs/`)
- Each buffer has a `TSState*` in `buf->ts_internal`
- Parsing triggered when buffer dirty flag changes
- Highlight queries loaded from `queries/<lang>/highlights.scm`
- Language auto-detection by file extension in `buf_detect_filetype()`

**Quickfix List** (`src/utils/quickfix.c`):
- Global `E.quickfix` stores entries with filename:line:col:message
- Special read-only buffer with filetype "quickfix"
- Populated by `:rg`, `:ssearch`, `:cadd` commands
- Cursor movement in any buffer triggers preview update via hook
- Navigation: `:cnext`, `:cprev`, `:copenidx N`

**Window Layout** (`src/ui/wlayout.c`, `src/ui/window.h`):
- Binary tree structure for splits (horizontal/vertical)
- Each leaf node is a `Window` with: buffer_index, cursor, row_offset, col_offset, wrap settings
- Focus moves directionally with `windows_focus_left/right/up/down()`
- Quickfix window is a special window with `is_quickfix=1`

**Fuzzy Finding** (`src/utils/fzf.c`):
- Shell out to `fzf` with optional `bat` preview
- Used by `:fzf` (file picker), `:recent`, `:c` (command picker), `:rg` (interactive ripgrep)
- Results parsed and fed to commands (e.g., open files, populate quickfix)

**Tmux Integration** (`src/utils/tmux.c`):
- `:tmux_toggle` creates/shows a runner pane in the same tmux window
- `:tmux_send <cmd>` sends text to that pane
- Keybind `<space>ts` sends current line
- Pane identified by hed's PID as target name

**Undo System** (`src/utils/undo.c`):
- Undo stack per buffer stores snapshots of rows
- Captures state before modifications via buffer modification hooks
- Commands `:undo` and `:redo` (also `u` and `Ctrl-r`)

### Extension Points (src/config.c)

All user-facing customization lives in `src/config.c`:

1. **user_commands_init()**: Register commands using `cmd(name, callback, desc)`
   - Callback signature: `void callback(const char *args)`
   - Example: `cmd("mycommand", cmd_mycommand, "my custom command");`

2. **user_keybinds_init()**: Register keybindings using mode-specific macros
   - `mapn(key, callback)` - Normal mode function keybind
   - `mapi(key, callback)` - Insert mode function keybind
   - `mapv(key, callback)` - Visual mode function keybind
   - `mapvb(key, callback)` - Visual block mode function keybind
   - `cmapn(key, "command")` - Normal mode command keybind
   - `cmapv(key, "command")` - Visual mode command keybind
   - Key sequences like `<space>ff` are literal spaces in strings: `" ff"`

3. **user_hooks_init()**: Register hooks for events
   - Example: `hook_register_mode(HOOK_MODE_CHANGE, on_mode_change_callback);`
   - Hook types in `src/hooks.h`: char/line insert/delete, buffer lifecycle, mode change, cursor movement
   - Additional quickfix hooks in `src/user_hooks_quickfix.c`

After modifying `src/config.c`, use `:reload` from within hed to rebuild and restart.

## Code Style and Patterns

- **Error Handling**: Functions return `EdError` enum (ED_OK, ED_ERR_*). Check with `if (err != ED_OK)`.
- **Bounds Checking**: Use `BOUNDS_CHECK(idx, len)` and `PTR_VALID(ptr)` macros from `src/lib/errors.h`
- **String Safety**: Use `SizedStr` type for length-tracked strings (see `src/lib/sizedstr.h`)
- **Logging**: Use `log_msg(fmt, ...)` which writes to `.hedlog`. Initialize with `log_init()`.
- **Status Messages**: Use `ed_set_status_message(fmt, ...)` to show user feedback in status line
- **Memory Management**: Manual malloc/free. Always null-check allocations.

## Common Patterns

**Getting Current Buffer**:
```c
Buffer *buf = buf_cur();
if (!buf) return;
```

**Executing Commands Programmatically**:
```c
command_invoke("split", NULL);
command_invoke("e", "/path/to/file");
```

**Modifying Buffer Content**:
```c
// Insert character at cursor
buf_insert_char(buf, c);

// Delete current line
buf_delete_line(buf, buf->cursor.y);

// Insert new line
buf_row_insert_in(buf, at_row, text, text_len);
```

**Registering a Command**:
```c
// In user_commands_init():
cmd("mycommand", cmd_mycommand, "description");

// Implement callback:
void cmd_mycommand(const char *args) {
    Buffer *buf = buf_cur();
    if (!buf) return;
    // ... use args ...
    ed_set_status_message("Command executed: %s", args);
}
```

## Tree-sitter Language Management

Tree-sitter grammars are stored as shared objects (`.so` files) and loaded dynamically:

**Installing a Language**:
```bash
# From hed command line:
:tsi <lang>

# Or manually with tsi binary:
./build/tsi <lang>
```

This clones `tree-sitter-<lang>` from GitHub, builds `<lang>.so`, and copies it to `ts-langs/` along with highlight queries to `queries/<lang>/`.

**Language Configuration**:
- `:ts on|off|auto` - Toggle tree-sitter (auto = detect by extension)
- `:tslang <name>` - Force language for current buffer
- File extension mapping in `buf_detect_filetype()` in `src/buf/buffer.c`

Included grammars: c, c-sharp, html, make, python, rust

## Testing and Debugging

- **Logs**: `tail -f .hedlog` to watch live logs, `:logclear` to truncate
- **Valgrind**: Build with debug symbols (already in compile_flags.txt: `-g -O0`)
- **GDB**: `gdb ./build/hed` then `run [args]`

## File Organization

```
src/
├── main.c              # Entry point, main loop
├── editor.c/h          # Global editor state, core initialization
├── config.c            # User configuration: commands, keybinds, hooks
├── commands.c/h        # Command registration and dispatch
├── keybinds.c/h        # Keybind registration and dispatch
├── keybinds_builtins.c # Built-in keybind functions
├── hooks.c/h           # Hook system implementation
├── user_hooks_quickfix.c # Quickfix preview sync hook
├── terminal.c/h        # Terminal raw mode, ANSI codes
├── registers.c/h       # Yank/paste register management
├── lsp.c/h             # LSP client (if implemented)
├── buf/
│   ├── buffer.c/h      # Buffer data structure and operations
│   ├── row.c/h         # Row (line) data structure
│   ├── textobj.c/h     # Text object extraction (words, paragraphs, etc.)
│   └── buf_helpers.c/h # Buffer utility functions
├── ui/
│   ├── window.c/h      # Window structure and focus management
│   ├── wlayout.c/h     # Window layout tree (splits)
│   └── abuf.c/h        # Append buffer for efficient rendering
├── utils/
│   ├── ts.c/h          # Tree-sitter integration
│   ├── quickfix.c/h    # Quickfix list implementation
│   ├── fzf.c/h         # Fuzzy finder integration
│   ├── tmux.c/h        # Tmux runner pane integration
│   ├── undo.c/h        # Undo/redo system
│   ├── jump_list.c/h   # Jump list (Ctrl-o/Ctrl-i)
│   ├── history.c/h     # Command history
│   ├── recent_files.c/h # Recent files tracking
│   ├── bottom_ui.c/h   # Status line and command line UI
│   └── term_cmd.c/h    # Terminal command execution helpers
├── lib/
│   ├── log.c/h         # Logging to .hedlog
│   ├── errors.c/h      # Error codes and macros
│   ├── sizedstr.c/h    # Length-tracked string type
│   ├── strutil.c/h     # String manipulation utilities
│   ├── safe_string.c/h # Safe string operations
│   ├── file_helpers.c/h # File I/O helpers
│   └── theme.h         # Color theme definitions
└── commands/
    ├── cmd_builtins.h  # Built-in command declarations
    ├── cmd_buffer.c    # Buffer management commands
    ├── cmd_search.c/h  # Search-related commands
    ├── cmd_misc.c      # Miscellaneous commands
    ├── cmd_util.c/h    # Command utilities
    ├── commands_buffer.c/h # Additional buffer commands
    └── commands_ui.c/h # UI-related commands

ts/
├── ts_lang_install.c   # Tree-sitter language installer source
└── build/              # Cloned tree-sitter grammar repos (c, python, etc.)

ts-langs/               # Compiled tree-sitter .so files
queries/                # Tree-sitter highlight queries by language
```

## Dependencies

Required:
- `clang` (C23 support)
- `libtree-sitter` (headers and library)
- POSIX terminal

Optional (for full feature set):
- `ripgrep` - for `:rg`, `:ssearch`, `:rgword`
- `fzf` - for `:fzf`, `:recent`, `:c` fuzzy pickers
- `tmux` - for `:tmux_toggle`, `:tmux_send` runner integration
- `lazygit` - for `:git` command
- `bat` - for file preview in fzf
- `nnn` - for directory browsing via `:shell --skipwait nnn`
