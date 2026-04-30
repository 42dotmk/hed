# clipboard

Mirrors yank operations into the system clipboard using the OSC 52 terminal
escape sequence. Wraps `kb_yank_line`, `kb_operator_yank`, and
`kb_visual_yank_selection` so any `y`, `yy`, or visual-mode `y` also pushes
the unnamed register to the OS clipboard.

## Why OSC 52

No external tools (`pbcopy`/`xclip`/`wl-copy`) needed. The terminal emulator
performs the clipboard write, so it works transparently over SSH.

## Requirements

- A terminal that supports OSC 52 with clipboard writes enabled.
  Tested: kitty, WezTerm, Alacritty, foot, iTerm2, Ghostty.
  Not supported: Apple Terminal, older GNOME Terminal.
- Inside tmux, add to `~/.tmux.conf`:

  ```tmux
  set -g set-clipboard on
  ```

## Limitations

- Some terminals cap OSC 52 payload size (kitty default ~75KB; others a few KB).
  Large yanks may be silently truncated by the terminal.
- One-way: this plugin only writes to the clipboard. Pasting from the system
  clipboard still uses the platform's normal terminal paste (Ctrl+Shift+V etc).

## Enable

In `src/config.c`'s `user_hooks_init()`:

```c
plugin_enable("clipboard");
```
