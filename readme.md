# Hed - A Minimal Vim-like Text Editor

A console-based text editor written in C, inspired by vim. Built with simplicity and efficiency in mind.

## Features

- **Modal Editing**: Normal, Insert, and Command modes
- **Multiple Buffers**: Work with up to 16 files simultaneously
- **Buffer Management**: Switch between files, list all open buffers
- **Vim-like Navigation**: hjkl keys and more
- **File Operations**: Open, save, and quit with familiar commands
- **Search**: Find text with `/` and navigate with `n`
- **Basic Editing**: Cut, copy, paste, and delete operations
<!-- Visual mode removed; selection-based editing is no longer available -->

## Building

Requirements:
- clang compiler
- make
- POSIX-compliant terminal

```bash
make
```

The binary will be created at `build/hed`.

## Usage

```bash
# Open a new file
./build/hed

# Open an existing file
./build/hed filename.txt

# Open multiple files at once
./build/hed file1.txt file2.txt file3.txt

# Or use the make target
make run
```

## Keybindings

### Normal Mode

| Key | Action |
|-----|--------|
| `h` | Move cursor left |
| `j` | Move cursor down |
| `k` | Move cursor up |
| `l` | Move cursor right |
| `i` | Enter insert mode (before cursor) |
| `a` | Enter insert mode (after cursor) |
| `x` | Delete character under cursor |
| `dd` | Delete current line (cut) |
| `yy` | Yank (copy) current line |
| `p` | Paste line below cursor |
| `/` | Search |
| `n` | Find next occurrence |
| `0` | Move to start of line |
| `$` | Move to end of line |
| `gg` | Go to first line |
| `G` | Go to last line |
| `:` | Enter command mode |

### Insert Mode

| Key | Action |
|-----|--------|
| `ESC` | Return to normal mode |
| Any character | Insert character |
| `Enter` | Insert newline |
| `Backspace` | Delete previous character |

### Command Mode

#### File Operations
| Command | Action |
|---------|--------|
| `:q` | Quit (fails if unsaved changes) |
| `:q!` | Force quit (discard changes) |
| `:w` | Save file |
| `:wq` | Save and quit |

#### Buffer Management
| Command | Action |
|---------|--------|
| `:e filename` | Open file in new buffer |
| `:bn` | Switch to next buffer |
| `:bp` | Switch to previous buffer |
| `:b N` | Switch to buffer N (e.g., `:b 2`) |
| `:ls` | List all open buffers |
| `:buffers` | List all open buffers (alias for :ls) |
| `:bd` | Close current buffer |
| `:bd N` | Close buffer N |

## Project Structure

```
hed/
├── Makefile          # Build configuration
├── README.md         # This file
├── include/          # Header files
│   └── hed.h        # Main header
├── src/              # Source files
│   └── hed.c        # Main implementation
└── build/            # Build output (created by make)
    ├── hed          # Compiled binary
    └── hed.o        # Object file
```

## Implementation Details

- **Multiple Buffers**: Supports up to 16 simultaneous file buffers with independent cursor positions
- **Text Buffer**: Uses a row-based data structure for efficient text manipulation
- **Terminal Control**: Raw mode terminal handling using termios
- **Rendering**: ANSI escape sequences for cursor positioning and screen clearing
- **Tab Support**: Tabs are expanded to spaces (configurable tab stop)
- **Shared Clipboard**: Copy/paste works across all buffers

## Limitations

This is a minimal implementation and does not include:
- Undo/redo functionality
- Split windows (horizontal/vertical)
- Syntax highlighting
- Line numbers display
- Command history
- Complex motion commands (w, b, e, etc.)
- Text objects
- Macros
- Configuration file

## License

Free to use and modify.

## Credits

Inspired by vim and based on the "Build Your Own Text Editor" tutorial architecture.
