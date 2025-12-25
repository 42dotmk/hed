# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**hed** is a modal terminal editor written in C23 with Vim-like bindings. It integrates tree-sitter for syntax highlighting, ripgrep for search, fzf for fuzzy finding, and tmux for runner pane support. The codebase emphasizes a small core (~13,000 LOC) with explicit C APIs for extensibility through commands, keybindings, and hooks.

**Key Features**:
- Modal editing (Normal/Insert/Visual/Visual Block/Command modes)
- Tree-sitter syntax highlighting with 12+ language grammars
- Quickfix list with live preview synchronization
- Fuzzy finding integration (fzf)
- Tmux runner pane integration
- Macro recording and playback
- Code folding (manual, bracket-based, indent-based)
- Directory browser (dired/oil.nvim-like)
- Modal (floating) windows
- Ctags integration
- Jump list and recent files tracking
- Undo/redo with snapshot-based history
- Vim-like registers and text objects

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

# Run tests
make test

# Generate ctags
make tags
```

The build system:
- Uses clang with C23 standard (`-std=c23`)
- Strict warnings: `-Wall -Wextra -pedantic`
- Links against `libtree-sitter` and `libdl` dynamically
- Produces two binaries: `build/hed` (the editor) and `build/tsi` (tree-sitter language installer)
- Copies tree-sitter grammars from `ts-langs/` during build

## Running hed

```bash
# Open files
./build/hed [file ...]

# Execute a command at startup
./build/hed -c "e other.txt"
./build/hed -c "q!"

# Open a directory (launches dired browser)
./build/hed /path/to/directory
./build/hed .
```

Logs are written to `.hedlog` in the current directory.

## Architecture Overview

### Core Data Flow

1. **Main Loop** (`src/main.c`):
   - Uses `select()` to wait for stdin input or macro queue events
   - Calls `ed_read_key()` to get the next keypress (from stdin or macro buffer)
   - Calls `ed_process_keypress()` to handle keys based on current mode
   - Calls `ed_render_frame()` to redraw the screen
   - Supports startup commands via `-c "<command>"` flag

2. **Editor State** (`src/editor.c`, `src/editor.h`):
   Global `Ed E` structure contains:
   - `buffers`: Vector of all open buffers
   - `current_buffer`: Index of active buffer
   - `mode`: Current editor mode (Normal/Insert/Visual/Visual Block/Command)
   - Window layout tree and focus tracking
   - Quickfix list with preview synchronization
   - Jump list for buffer navigation history (Ctrl-O/Ctrl-I)
   - Recent files tracking
   - Command history (persistent)
   - Registers (Vim-like named registers a-z, 0-9, ", :, .)
   - Macro recording/playback state
   - Search state (current query and regex mode)
   - Working directory tracking

3. **Buffer System** (`src/buf/`):
   - `Buffer`: Contains rows, cursor position, filename, filetype, tree-sitter state, folding regions
   - `Row`: Line data with both raw chars and rendered display (for tabs/unicode)
   - `textobj.c`: Text object operations (words, paragraphs, delimiter pairs)
   - Support for read-only buffers (quickfix, dired)
   - Per-buffer undo/redo stacks

4. **Command Execution** (`src/commands.c`):
   - Commands registered via `command_register(name, callback, desc)` in `src/config.c`
   - User types `:command args` which gets parsed and dispatched to the callback
   - Command callbacks receive args as a string
   - 87+ built-in commands available

5. **Keybinding Dispatch** (`src/keybinds.c`):
   - Keybinds registered per-mode via `keybind_register(mode, key_sequence, callback)`
   - Built-in keybind functions in `src/keybinds_builtins.c`
   - User configuration in `src/config.c` using `mapn/mapi/mapv/mapvb` macros
   - Multi-key sequences supported (`dd`, `gg`, `yy`)
   - Leader key sequences (`<space>ff`, `<space>sd`)
   - Numeric count support for operators

6. **Hook System** (`src/hooks.c`, `src/hooks.h`):
   - Events: char insert/delete, line insert/delete, buffer lifecycle, mode change, cursor movement
   - Callbacks registered with mode and filetype filters
   - Quickfix preview synchronization implemented via cursor movement hook in `src/user_hooks_quickfix.c`
   - Auto-pairing, smart indentation, cursor shape changes
   - Dired buffer lifecycle management

### Key Subsystems

#### Tree-sitter Integration (`src/utils/ts.c`)
- Grammars loaded dynamically as `.so` files from `$HED_TS_PATH` (defaults to `~/.config/hed/ts` or `ts-langs/`)
- Each buffer has a `TSState*` in `buf->ts_internal`
- Parsing triggered when buffer dirty flag changes
- Highlight queries loaded from `queries/<lang>/highlights.scm`
- Language auto-detection by file extension in `buf_detect_filetype()`
- Commands: `:ts on|off|auto`, `:tslang <name>`, `:tsi <lang>`
- **Included grammars**: bash, c, c-sharp, commonlisp, html, javascript, make, markdown, org, python, rust, toml, unifieddiff, zsh

#### Directory Browser - Dired (`src/dired.c`, `src/dired.h`)
**NEW FEATURE** - oil.nvim-like directory browser:
- Opens directories as special "dired" buffers with filetype "dired"
- Lists files and directories sorted alphabetically
- Navigation keybinds (in dired buffers):
  - `<Enter>`: Open file or navigate into directory
  - `-`: Go to parent directory
  - `~`: Return to original directory
- Read-only buffer with automatic state management
- Maintains per-buffer state (origin path, current path)
- Integrated with buffer lifecycle hooks for cleanup
- Open with: `:e .`, `:e /path/to/dir`, or `./build/hed .`

#### Macro System (`src/macros.c`, `src/macros.h`)
**NEW FEATURE** - Vim-style macro recording and playback:
- Record to named registers (a-z)
- Playback with numeric count support
- Last macro repeat with `@@`
- Special key encoding: `<Esc>`, `<CR>`, `<C-x>`, etc.
- Transparent to underlying functions (simulates keyboard input)
- Commands: `:record [reg]`, `:play [reg]`
- Keybinds: `q<reg>` to record, `@<reg>` to play, `@@` to repeat last
- Macro queue system allows execution without blocking on stdin

#### Code Folding (`src/utils/fold.c`, `src/fold_methods/`)
**NEW FEATURE** - Code folding with multiple detection methods:

**Fold Methods**:
1. **Manual** (`FOLD_METHOD_MANUAL`) - User-created folds only
2. **Bracket** (`FOLD_METHOD_BRACKET`) - Auto-detect `{` `}` pairs
3. **Indent** (`FOLD_METHOD_INDENT`) - Indentation-based regions

**Commands**:
- `:foldnew [start] [end]` - Create fold
- `:foldrm` - Remove fold at cursor
- `:foldtoggle` - Toggle fold at cursor
- `:foldmethod <method>` - Set auto-detection method
- `:foldupdate` - Regenerate folds based on current method

**Keybindings**:
- `za` - Toggle fold at cursor
- `zo` - Open fold
- `zc` - Close fold
- `zR` - Open all folds
- `zM` - Close all folds

**Implementation**:
- Per-buffer fold regions stored in `FoldList`
- Collapsed folds shown as single line with `[...]` indicator
- Cursor automatically adjusted when navigating collapsed folds
- Rendering optimizations skip collapsed line content

#### Modal Windows (`src/ui/winmodal.c`, `src/ui/winmodal.h`)
**NEW FEATURE** - Floating window support:
- Windows that draw on top of the layout tree
- Can be centered or custom positioned
- Block input to underlying windows when shown
- Support borders and decorations
- Commands: `:modal` (convert current to modal), `:unmodal` (back to layout)
- Each window has `is_modal` and `visible` flags
- Use cases: popup menus, centered focus windows, overlays

#### Quickfix List (`src/utils/quickfix.c`)
- Global `E.quickfix` stores entries with filename:line:col:message
- Special read-only buffer with filetype "quickfix"
- Populated by `:rg`, `:ssearch`, `:cadd` commands
- Cursor movement in **any buffer** triggers preview update via hook
- Navigation: `:cnext`, `:cprev`, `:copenidx N`, `:ctoggle`
- Keybinds: `gn`/`<C-n>` (next), `gp`/`<C-p>` (prev), `<space>tq` (toggle)
- Live preview synchronization shows context of current quickfix entry

#### Ctags Integration (`src/utils/ctags.c`)
**NEW FEATURE** - Tag-based code navigation:
- Uses standard ctags file format
- `:tag <name>` - Jump to tag definition
- `gd` - Jump to tag under cursor
- Searches using ripgrep for fast lookup
- Parses tag file format: `TAG FILEPATH REGEX_STRING`
- Generate tags with `make tags` or `ctags -R .`

#### Window Layout (`src/ui/wlayout.c`, `src/ui/window.h`)
- Binary tree structure for splits (horizontal/vertical)
- Each leaf node is a `Window` with: buffer_index, cursor, row_offset, col_offset, wrap settings, gutter_mode
- Focus moves directionally with `windows_focus_left/right/up/down()`
- Quickfix window is a special window with `is_quickfix=1`
- Weight-based sizing for flexible split distribution
- UTF-8 borders and configurable decorations
- Dynamic recalculation on split/close/resize

#### Fuzzy Finding (`src/utils/fzf.c`)
- Shell out to `fzf` with optional `bat` preview
- Used by `:fzf` (file picker), `:recent`, `:c` (command picker), `:rg` (interactive ripgrep)
- Results parsed and fed to commands (e.g., open files, populate quickfix)
- Multiple file selection support
- Keybinds: `<space>ff` or `<space><space>` (file picker), `<space>c` (command picker)

#### Tmux Integration (`src/utils/tmux.c`)
- `:tmux_toggle` creates/shows a runner pane in the same tmux window
- `:tmux_send <cmd>` sends text to that pane
- Keybind `<space>ts` sends current line to runner
- Pane identified by hed's PID as target name
- Automatic pane creation/reuse
- `:tmux_kill` terminates the runner pane

#### Undo System (`src/utils/undo.c`)
- Per-buffer undo stacks store snapshots of rows
- Captures state before modifications via buffer modification hooks
- Commands `:undo` and `:redo` (also `u` and `<C-r>`)
- Snapshot-based approach preserves entire buffer state
- Efficient for large files with sparse edits

#### Register System (`src/registers.c`)
Vim-like register support:
- `"` - Unnamed (default clipboard)
- `0` - Yank register (last yank)
- `1-9` - Delete history (rotating)
- `a-z` - Named registers
- `:` - Last command
- `.` - Last keybind sequence

Features:
- Shared clipboard across buffers
- Block selection support
- Append to named registers (uppercase A-Z)
- Integration with system clipboard (planned)

#### Jump List (`src/utils/jump_list.c`)
- Vim-style jump list navigation
- Tracks buffer switches with cursor positions
- `<C-o>` / `<space>jb` - Jump backward
- `<C-i>` / `<space>jf` - Jump forward
- Maintains history of where you've been for easy navigation back

#### Recent Files (`src/utils/recent_files.c`)
- Persistent tracking of opened files
- `:recent` command launches fzf picker with recent files
- Stored in history file between sessions
- Keybind: `<space>fr` (fuzzy recent files)

### Extension Points (src/config.c)

All user-facing customization lives in `src/config.c`:

1. **user_commands_init()**: Register commands using `cmd(name, callback, desc)`
   - Callback signature: `void callback(const char *args)`
   - Example: `cmd("mycommand", cmd_mycommand, "my custom command");`

2. **user_keybinds_init()**: Register keybindings using mode-specific macros
   - `mapn(key, callback, desc)` - Normal mode function keybind
   - `mapi(key, callback, desc)` - Insert mode function keybind
   - `mapv(key, callback, desc)` - Visual mode function keybind
   - `mapvb(key, callback, desc)` - Visual block mode function keybind
   - `cmapn(key, "command")` - Normal mode command keybind
   - `cmapv(key, "command")` - Visual mode command keybind
   - Key sequences like `<space>ff` are literal spaces in strings: `" ff"`
   - Multi-key sequences: `"dd"`, `"gg"`, `"yy"`

3. **user_hooks_init()**: Register hooks for events
   - Example: `hook_register_mode(HOOK_MODE_CHANGE, on_mode_change_callback);`
   - Hook types in `src/hooks.h`: char/line insert/delete, buffer lifecycle, mode change, cursor movement
   - Additional quickfix hooks in `src/user_hooks_quickfix.c`
   - Built-in hooks for auto-pairing, smart indentation, cursor shape

After modifying `src/config.c`, use `:reload` from within hed to rebuild and restart.

## Built-in Commands Reference

### File I/O
- `:e <file>` - Edit file or directory (dired)
- `:w [file]` - Write buffer
- `:q` - Quit (fails if unsaved changes)
- `:q!` - Force quit without saving
- `:wq` - Write and quit
- `:qa` - Quit all windows

### Buffer Management
- `:ls` - List all buffers
- `:bn` - Next buffer
- `:bp` - Previous buffer
- `:b <N>` - Switch to buffer N
- `:bd` - Delete (close) current buffer
- `:new` - Create new empty buffer

### Window Management
- `:split` - Horizontal split
- `:vsplit` - Vertical split
- `:wclose` - Close current window
- `:modal` - Convert current window to modal (floating)
- `:unmodal` - Convert modal window back to layout

### Search & Navigation
- `:rg [pattern]` - Ripgrep search (no args = interactive fzf)
- `:ssearch <pattern>` - Search in current file
- `:rgword` - Ripgrep for word under cursor
- `:fzf` - Fuzzy file finder
- `:recent` - Fuzzy recent files picker
- `:c` - Fuzzy command picker
- `:tag <name>` - Jump to ctag
- `/` - Search forward in buffer
- `?` - Search backward in buffer

### Quickfix
- `:copen` - Open quickfix window
- `:cclose` - Close quickfix window
- `:ctoggle` - Toggle quickfix window
- `:cnext` - Next quickfix entry
- `:cprev` - Previous quickfix entry
- `:copenidx <N>` - Open Nth quickfix entry
- `:cadd <file:line:col:text>` - Add entry to quickfix
- `:cclear` - Clear quickfix list

### Tree-sitter
- `:ts on|off|auto` - Toggle tree-sitter highlighting
- `:tslang <name>` - Force language for current buffer
- `:tsi <lang>` - Install tree-sitter grammar

### Folding
- `:foldnew [start] [end]` - Create fold region
- `:foldrm` - Remove fold at cursor
- `:foldtoggle` - Toggle fold at cursor
- `:foldmethod manual|bracket|indent` - Set fold detection method
- `:foldupdate` - Regenerate folds

### Macros
- `:record [register]` - Start/stop recording macro
- `:play [register]` - Play macro from register

### Tmux Integration
- `:tmux_toggle` - Toggle tmux runner pane
- `:tmux_send <cmd>` - Send command to runner pane
- `:tmux_kill` - Kill runner pane

### Tools & Utilities
- `:fmt` - Format buffer with external formatter
- `:git` - Launch lazygit
- `:shell <cmd>` - Execute shell command
- `:reload` - Rebuild and restart hed
- `:ln on|off|rln` - Line number display mode
- `:rln on|off` - Relative line numbers
- `:wrap on|off` - Soft wrap
- `:logclear` - Clear .hedlog file

### Undo/Redo
- `:undo` - Undo last change (also `u`)
- `:redo` - Redo last undo (also `<C-r>`)

## Common Keybindings

### Normal Mode - Motion
- `h/j/k/l` - Left/Down/Up/Right
- `w/b` - Next/previous word
- `e` - End of word
- `0/$` - Start/end of line
- `gg/G` - Start/end of file
- `{/}` - Previous/next paragraph
- `<C-u>/<C-d>` - Half page up/down

### Normal Mode - Editing
- `i/a` - Insert before/after cursor
- `I/A` - Insert at line start/end
- `o/O` - Open line below/above
- `x` - Delete character
- `dd` - Delete line
- `yy` - Yank (copy) line
- `p/P` - Paste after/before cursor
- `u` - Undo
- `<C-r>` - Redo
- `.` - Repeat last change

### Normal Mode - Text Objects
- `diw/daw` - Delete inner/around word
- `di(/da(` - Delete inner/around parentheses
- `ci{/ca{` - Change inner/around braces
- `yi"/ya"` - Yank inner/around quotes

### Normal Mode - Leader Key (`<space>`)
- `<space>ff` or `<space><space>` - Fuzzy file finder
- `<space>fr` - Fuzzy recent files
- `<space>c` - Fuzzy command picker
- `<space>sd` - Ripgrep search
- `<space>ss` - Search in file
- `<space>ts` - Send line to tmux
- `<space>tq` - Toggle quickfix
- `<space>ws/wv` - Window split/vsplit
- `<space>ww/wh/wj/wk/wl` - Window focus navigation
- `<space>jb/jf` - Jump backward/forward

### Normal Mode - Folding
- `za` - Toggle fold
- `zo` - Open fold
- `zc` - Close fold
- `zR` - Open all folds
- `zM` - Close all folds

### Normal Mode - Other
- `gd` - Go to definition (ctags)
- `gn/<C-n>` - Next quickfix entry
- `gp/<C-p>` - Previous quickfix entry
- `q<reg>` - Start/stop macro recording
- `@<reg>` - Play macro
- `@@` - Repeat last macro
- `:` - Enter command mode
- `/` - Search forward
- `?` - Search backward

### Visual Mode
- `v` - Enter visual mode (character)
- `V` - Enter visual mode (line)
- `<C-v>` - Enter visual block mode
- `d/y/c` - Delete/yank/change selection
- `<` / `>` - Indent/dedent

### Insert Mode
- `<Esc>` or `<C-c>` - Return to normal mode
- Auto-pairing for `(`, `[`, `{`, `<`
- Smart indentation on newline

### Dired Mode (in directory buffers)
- `<Enter>` - Open file or enter directory
- `-` - Go to parent directory
- `~` - Go to home directory

## Code Style and Patterns

- **Error Handling**: Functions return `EdError` enum (ED_OK, ED_ERR_*). Check with `if (err != ED_OK)`.
- **Bounds Checking**: Use `BOUNDS_CHECK(idx, len)` and `PTR_VALID(ptr)` macros from `src/lib/errors.h`
- **String Safety**: Use `SizedStr` type for length-tracked strings (see `src/lib/sizedstr.h`)
- **Logging**: Use `log_msg(fmt, ...)` which writes to `.hedlog`. Initialize with `log_init()`.
- **Status Messages**: Use `ed_set_status_message(fmt, ...)` to show user feedback in status line
- **Memory Management**: Manual malloc/free. Always null-check allocations.
- **Vectors**: Use type-safe vector macros from `src/lib/vector.h` for growable arrays

## Common Patterns

### Getting Current Buffer
```c
Buffer *buf = buf_cur();
if (!buf) return;
```

### Executing Commands Programmatically
```c
command_invoke("split", NULL);
command_invoke("e", "/path/to/file");
```

### Modifying Buffer Content
```c
// Insert character at cursor
buf_insert_char(buf, c);

// Delete current line
buf_delete_line(buf, buf->cursor.y);

// Insert new line
buf_row_insert_in(buf, at_row, text, text_len);
```

### Registering a Command
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

### Registering a Keybind
```c
// In user_keybinds_init():

// Function keybind (normal mode)
mapn("x", kb_delete_char, "delete character");

// Command keybind (normal mode)
cmapn(" ff", "fzf");

// Multi-key sequence
mapn("dd", kb_delete_line, "delete line");
```

### Registering a Hook
```c
// In user_hooks_init():
static void on_insert_hook(HookEventData *event) {
    // Access event data
    Buffer *buf = event->buffer;
    char c = event->c;
    // ... handle event ...
}

hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", on_insert_hook);
```

### Using Vectors
```c
// Define a vector type
DECLARE_VECTOR(IntVec, int)

// Create and use
IntVec vec = {0};
vector_push(&vec, 42);
vector_push(&vec, 99);

for (int i = 0; i < vec.len; i++) {
    log_msg("Item: %d", vec.items[i]);
}

vector_free(&vec);
```

## Tree-sitter Language Management

Tree-sitter grammars are stored as shared objects (`.so` files) and loaded dynamically:

### Installing a Language
```bash
# From hed command line:
:tsi <lang>

# Or manually with tsi binary:
./build/tsi <lang>
```

This clones `tree-sitter-<lang>` from GitHub, builds `<lang>.so`, and copies it to `ts-langs/` along with highlight queries to `queries/<lang>/`.

### Language Configuration
- `:ts on|off|auto` - Toggle tree-sitter (auto = detect by extension)
- `:tslang <name>` - Force language for current buffer
- File extension mapping in `buf_detect_filetype()` in `src/buf/buffer.c`

### Included Grammars
bash, c, c-sharp, commonlisp, html, javascript, make, markdown, org, python, rust, toml, unifieddiff, zsh

## Testing and Debugging

- **Logs**: `tail -f .hedlog` to watch live logs, `:logclear` to truncate
- **Unit Tests**: `make test` runs Unity test framework tests
- **Valgrind**: Build with debug symbols (already in compile_flags.txt: `-g -O0`)
- **GDB**: `gdb ./build/hed` then `run [args]`
- **Test Framework**: Unity (lightweight C unit testing) in `test/` directory

## File Organization

```
src/
├── main.c              # Entry point, main loop
├── editor.c/h          # Global editor state, core initialization
├── config.c            # User configuration: commands, keybinds, hooks
├── commands.c/h        # Command registration and dispatch
├── keybinds.c/h        # Keybind registration and dispatch
├── keybinds_builtins.c/h # Built-in keybind functions
├── hooks.c/h           # Hook system implementation
├── user_hooks_quickfix.c # Quickfix preview sync hook
├── terminal.c/h        # Terminal raw mode, ANSI codes, rendering
├── registers.c/h       # Yank/paste register management
├── macros.c/h          # Macro recording/playback
├── dired.c/h           # Directory browser (NEW)
├── command_mode.c/h    # Command-line mode with completion
├── lsp.c/h             # LSP client (stub/planned)
├── hed.h               # Main header (includes all subsystems)
├── buf/
│   ├── buffer.c/h      # Buffer data structure and operations
│   ├── row.c/h         # Row (line) data structure
│   ├── textobj.c/h     # Text object extraction (words, paragraphs, etc.)
│   └── buf_helpers.c/h # Buffer utility functions
├── ui/
│   ├── window.c/h      # Window structure and focus management
│   ├── wlayout.c/h     # Window layout tree (splits)
│   ├── winmodal.c/h    # Modal (floating) windows (NEW)
│   └── abuf.c/h        # Append buffer for efficient rendering
├── utils/
│   ├── ts.c/h          # Tree-sitter integration
│   ├── quickfix.c/h    # Quickfix list implementation
│   ├── fzf.c/h         # Fuzzy finder integration
│   ├── tmux.c/h        # Tmux runner pane integration
│   ├── fold.c/h        # Code folding (NEW)
│   ├── undo.c/h        # Undo/redo system
│   ├── ctags.c/h       # Ctags integration (NEW)
│   ├── jump_list.c/h   # Jump list (Ctrl-o/Ctrl-i)
│   ├── history.c/h     # Command history
│   ├── recent_files.c/h # Recent files tracking
│   ├── bottom_ui.c/h   # Status line and command line UI
│   └── term_cmd.c/h    # Terminal command execution helpers
├── fold_methods/       # Fold detection methods (NEW)
│   ├── fold_methods.c/h # Fold method interface
│   ├── fold_bracket.c  # Bracket-based folding
│   └── fold_indent.c   # Indent-based folding
├── lib/
│   ├── vector.h        # Type-safe growable arrays (macros)
│   ├── log.c/h         # Logging to .hedlog
│   ├── errors.c/h      # Error codes and macros
│   ├── sizedstr.c/h    # Length-tracked string type
│   ├── strutil.c/h     # String manipulation utilities
│   ├── safe_string.c/h # Safe string operations
│   ├── file_helpers.c/h # File I/O helpers (path join, dirname, etc.)
│   ├── theme.h         # Color theme definitions (Tokyo Night)
│   ├── ansi.h          # Terminal escape sequences
│   └── cursor.h        # Cursor position type
└── commands/
    ├── cmd_builtins.h  # Built-in command declarations
    ├── cmd_misc.c/h    # Miscellaneous commands
    ├── cmd_search.c/h  # Search-related commands
    ├── commands_buffer.c/h # Buffer management commands
    └── commands_ui.c/h # UI-related commands

test/
├── unity.c/h           # Unity test framework
├── test_textobj.c      # Text object tests
└── makefile            # Test build system

ts/
├── ts_lang_install.c   # Tree-sitter language installer source
└── build/              # Cloned tree-sitter grammar repos

ts-langs/               # Compiled tree-sitter .so files
queries/                # Tree-sitter highlight queries by language
tasks/                  # Project tasks and roadmap
```

## Dependencies

### Required
- `clang` (C23 support)
- `libtree-sitter` (headers and library)
- `libdl` (dynamic loading)
- POSIX terminal

### Optional (for full feature set)
- `ripgrep` - for `:rg`, `:ssearch`, `:rgword`
- `fzf` - for `:fzf`, `:recent`, `:c` fuzzy pickers
- `tmux` - for `:tmux_toggle`, `:tmux_send` runner integration
- `lazygit` - for `:git` command
- `bat` - for file preview in fzf
- `ctags` - for `:tag`, `gd` tag navigation

## Recent Development Focus

Based on recent git commits and file changes:
- Text object implementation and testing (Unity framework)
- Jump list functionality
- Recent files tracking
- Quickfix system enhancements
- Dired (directory browser) implementation
- Buffer/window separation improvements

## Philosophy & Design Principles

1. **Small Core**: Keep the core editor simple and focused (~13K LOC)
2. **Explicit APIs**: Clear, documented C APIs for all subsystems
3. **Single Configuration File**: All user customization in `src/config.c`
4. **Hook-Driven**: Use hooks for cross-cutting concerns (auto-pairing, preview sync)
5. **Tool Integration**: Leverage external tools (ripgrep, fzf, tmux) rather than reimplementing
6. **Type Safety**: Use macros for type-safe generics (vectors, etc.)
7. **Error Handling**: Explicit error returns, bounds checking, null checks
8. **Manual Memory Management**: Clear ownership, explicit malloc/free
9. **Vim-like UX**: Modal editing, motions, operators, text objects
10. **Modern C**: C23 standard with strict warnings

## Future Roadmap

Planned features (from `tasks/` directory):
- LSP client implementation (currently stub)
- Plugin system for dynamic loading
- Directory marks (bookmarks for directories)
- Additional text objects
- More fold methods
- System clipboard integration
