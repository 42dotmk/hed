# man

View and search system man pages without leaving the editor. Renders
a page into a read-only buffer (titled `[man <topic>(<sect>)]`) so you
can scroll, search, and yank from it like any other buffer.

## Commands

| Command | Action |
|---|---|
| `:man <topic>` | Render the man page for `<topic>` |
| `:man <sect> <topic>` | Pick a specific section (e.g. `:man 3 printf`) |
| `:man` | fzf-pick from `apropos .`, then open the chosen page |

Re-running `:man` on a topic already open switches to the existing
buffer instead of re-rendering it.

## Keybind

In a man buffer, `gd` opens the page for the word under the cursor —
so following a cross-reference like `printf(3)` is a single keystroke.

| Key | Mode | Action |
|---|---|---|
| `gd` | normal (man buffers) | Open the page for the word under the cursor |

## hed's own man pages

`make man` generates roff man pages from the top-level `readme.md`
and every `plugins/*/README.md` into `man/man1/`:

- `readme.md` → `hed.1`
- `plugins/<name>/README.md` → `hed-<name>.1` (`hed-tmux`, `hed-vim_keybinds`, …)

The build bakes that directory into the binary (`-DHED_MAN_DIR`, set
from the `MAN_DIR` makefile variable), and this plugin prepends it to
`MANPATH` when shelling out to `man`. So once you've run `make man`,
the editor can open its own docs:

```
:man hed
:man hed-tmux
:man hed-man
```

A trailing `:` is appended to `MANPATH`, so the system pages stay
reachable — `:man printf` still works alongside `:man hed-tmux`.

To put the pages on the system `MANPATH` too (for `man hed` /
`apropos hed` in any shell):

```sh
make install-man        # -> ~/.local/share/man/man1
```

Generating the pages requires `pandoc`. See `scripts/gen-man.sh`.

## How it works

The plugin shells out to `man` with `MANPAGER=cat` and `MANWIDTH` set
to the current window width (clamped to 40–200 columns), then pipes
through `col -bx` to flatten the overstrike backspaces into plain
text. Output is slurped into a fresh read-only buffer with the `man`
filetype.

The `:man` fzf picker enumerates topics via `apropos . | sort -u` and
hands the lines to `picker_list` — no direct fzf coupling, so it works
with whatever picker is registered.

## Requirements

- `man` and `col` on `$PATH` (standard on any Unix)
- `apropos` for the no-argument fzf picker
- `pandoc` only if you want to (re)generate hed's own man pages

If `man` has no entry for the topic, the plugin reports it on the
status line and is otherwise a no-op.

## Disable

Set `plugin_load(&plugin_man, …)` to `0` in `src/config.h` and
`:reload`. The `:man` command and the man-buffer `gd` binding go away.
