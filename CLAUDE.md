# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Hed is a minimal vim-like text editor written in C23, featuring modal editing, multiple buffers, window splits, and extensibility through hooks, commands, and keybindings. The project emphasizes simplicity while providing modern features like tree-sitter syntax highlighting, fuzzy finding (fzf), ripgrep integration, and a quickfix list.

## Build System

Build the project:
```bash
make
```

Run the built binary:
```bash
make run
# or directly:
./build/hed [file1] [file2] ...
```

Clean build artifacts:
```bash
make clean
```

The build system uses `clang` with flags defined in `compile_flags.txt`. Tree-sitter support is enabled via `-DUSE_TREESITTER`.

## Code Architecture

### Module Organization

The codebase is organized into clear subsystems under `src/`:

- **buf/** - Buffer and row data structures
  - `buffer.c/h` - File/document management, supports up to 256 buffers
  - `row.c/h` - Individual text line representation
  - `buf_helpers.c/h` - Buffer manipulation utilities

- **ui/** - User interface components
  - `window.c/h` - Window management (splits, focus, decorations)
  - `wlayout.c/h` - Layout tree for complex splits and borders
  - `abuf.c/h` - Append buffer for efficient terminal rendering

- **lib/** - Core utilities
  - `sizedstr.c/h` - Dynamic string with length tracking
  - `log.c/h` - Debug logging to `.hedlog`
  - `ansi.h` - ANSI escape sequences
  - `strutil.c/h` - String manipulation helpers

- **utils/** - High-level features
  - `fzf.c/h` - Fuzzy file finder integration
  - `quickfix.c/h` - Vim-style quickfix list for navigation
  - `ts.c/h` - Tree-sitter syntax highlighting
  - `undo.c/h` - Undo/redo system
  - `history.c/h` - Command history
  - `recent_files.c/h` - Recently opened files
  - `jump_list.c/h` - Buffer navigation history (Ctrl-O/Ctrl-I)
  - `bottom_ui.c/h` - Status and command line rendering

- **Core modules** (top-level src/)
  - `editor.c/h` - Global editor state (`Ed E`) and main event loop
  - `terminal.c/h` - Terminal control (raw mode, rendering)
  - `commands.c/h` - Extensible command system (`:w`, `:q`, etc.)
  - `keybinds.c/h` - Extensible keybinding system
  - `hooks.c/h` - Event hook system for extensibility
  - `registers.c/h` - Clipboard and register management
  - `config.c` - User configuration (keybinds, commands, hooks)

### Core Data Flow

1. **Global State**: The `Ed E` global in `editor.h` holds:
   - Array of buffers (`Buffer buffers[MAX_BUFFERS]`)
   - Array of windows (`Window windows[8]`)
   - Window layout tree (`WLayoutNode *wlayout_root`)
   - Quickfix list, clipboard, search query, command history, etc.

2. **Event Loop** (`main.c`):
   ```c
   while (1) {
       ed_render_frame();     // Render UI
       ed_process_keypress(); // Handle input
   }
   ```

3. **Window System**:
   - Each `Window` references a buffer via `buffer_index`
   - Windows can be split horizontally/vertically
   - The `WLayoutNode` tree manages split geometry and decorations
   - One window is always focused (receives input)

4. **Buffer-Window Relationship**:
   - Buffers hold file content and are independent of display
   - Windows are viewports into buffers
   - Multiple windows can show the same buffer
   - The focused window's buffer is the "current buffer"

5. **Rendering Pipeline**:
   - `ed_render_frame()` computes layout and builds output in an `Abuf`
   - Layout tree (`wlayout_compute()`) calculates window positions
   - Each window renders its portion of the buffer
   - Status line and command line rendered at bottom
   - Final `Abuf` flushed to terminal in one write

### Extensibility System

Three primary extension points defined in `config.c`:

1. **Commands** (`commands.c/h`):
   ```c
   void command_register(const char *name, CommandCallback cb, const char *desc);
   ```
   Users define commands in `user_commands_init()`. Examples: `:w`, `:rg`, `:fzf`

2. **Keybindings** (`keybinds.c/h`):
   ```c
   void keybind_register(int mode, const char *sequence, KeybindCallback cb);
   void keybind_register_command(int mode, const char *sequence, const char *cmdline);
   ```
   Users define bindings in `user_keybinds_init()`. Supports multi-key sequences (`dd`, `gg`) and Ctrl combos (`<C-s>`).

3. **Hooks** (`hooks.c/h`):
   Event system for reacting to editor actions:
   - `HOOK_MODE_CHANGE` - Mode transitions
   - `HOOK_BUFFER_OPEN/CLOSE/SAVE/SWITCH` - Buffer lifecycle
   - `HOOK_CHAR_INSERT/DELETE` - Text modifications
   - `HOOK_LINE_INSERT/DELETE` - Line operations
   - `HOOK_CURSOR_MOVE` - Cursor movement

   Hooks can filter by mode and filetype. Example in `config.c`:
   ```c
   hook_register_mode(HOOK_MODE_CHANGE, on_mode_change);
   ```

### Special Features

- **Quickfix List**: Vim-style error/search results navigation. Commands: `:copen`, `:cnext`, `:cprev`, `:cclear`. Populated by `:rg` or `:shq`.

- **FZF Integration**: `:fzf` opens fuzzy file finder. Keybind `<space><space>` in normal mode.

- **Ripgrep Integration**: `:rg <pattern>` searches and populates quickfix list.

- **Tree-sitter**: Optional syntax highlighting. Enable with `-DUSE_TREESITTER` in `compile_flags.txt`. Commands: `:ts on|off|auto`, `:tslang <name>`.

- **Window Splits**: `:split` (horizontal), `:vsplit` (vertical), `:wfocus` (cycle), `:wclose` (close current).

- **Messages Buffer**: Special readonly buffer for logging editor messages (accessible via buffer list).

## Development Patterns

### Adding a New Command

1. Declare callback in `commands.h`:
   ```c
   void cmd_mycommand(const char *args);
   ```

2. Implement in `commands.c`:
   ```c
   void cmd_mycommand(const char *args) {
       // Implementation
       ed_set_status_message("Command executed");
   }
   ```

3. Register in `config.c` in `user_commands_init()`:
   ```c
   command_register("mycommand", cmd_mycommand, "description");
   ```

### Adding a New Keybinding

In `config.c`, add to `nmode_bindings()` (or `imode_bindings()`, `vmode_bindings()`):

```c
// Direct callback:
mapn("x", kb_delete_char);

// Multi-key sequence:
mapn("dd", kb_delete_line);

// Execute a command:
cmapn(" fm", "mycommand");
```

Macros: `mapn` (normal), `mapi` (insert), `mapv` (visual), `cmapn` (command in normal).

### Working with Buffers

Get current buffer:
```c
Buffer *buf = buf_cur();
```

Create/open buffer:
```c
int idx = buf_new("filename.txt");  // Returns buffer index
Buffer *buf = buf_open_file("path"); // Returns buffer pointer
```

Switch buffer:
```c
buf_switch(index);
```

The current buffer is `E.buffers[E.current_buffer]` but prefer `buf_cur()`.

### Working with Windows

Get current window:
```c
Window *win = window_cur();
```

Window contains `buffer_index` pointing to `E.buffers[]`. The focused window is `E.windows[E.current_window]`.

Attach buffer to window:
```c
win_attach_buf(window_cur(), buf);
```

### Tree-sitter Integration

Tree-sitter state is per-buffer (`buf->ts_internal`). The `ts.c/h` module handles:
- Language detection from filetype
- Incremental parsing
- Syntax highlighting queries

Languages are loaded from `ts-langs/` directory.

### Logging

Debug logging to `.hedlog`:
```c
log_msg("Debug message: %d", value);
```

View logs:
```bash
tail -f .hedlog
```

Clear logs: `:logclear`

## Key Design Decisions

- **Single global state** (`Ed E`): Simplifies access patterns, all modules can access editor state
- **Index-based references**: Windows reference buffers by index, not pointers (allows array reordering)
- **Immediate mode rendering**: Full screen redrawn each frame, buffered in `Abuf` for efficiency
- **Modal editing**: Three modes (NORMAL, INSERT, COMMAND) inspired by vim
- **Extension via registration**: Commands, keybindings, and hooks registered at init time
- **Raw mode terminal**: Full control via termios and ANSI sequences
- **No configuration files**: All configuration done in C code (`config.c`)

## Common Development Workflows

Testing a change interactively:
```bash
make && ./build/hed test.txt
```

Viewing editor logs during development:
```bash
# Terminal 1:
tail -f .hedlog

# Terminal 2:
./build/hed
```

Debugging crashes - logs are written before most crashes:
```bash
cat .hedlog
```

## Code Style Notes

- C23 standard with `-Wall -Wextra -pedantic`
- Include paths configured in `compile_flags.txt` (`-Isrc`, `-Isrc/ui`, etc.)
- Header files in same directory as implementation (except `hed.h` which includes all)
- Use `Ed E` for global state, `buf_cur()` for current buffer, `window_cur()` for current window
- Prefer explicit buffer/window parameters over implicit current buffer/window in functions
