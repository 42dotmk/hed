# clipboard

Mirrors yanks into the system clipboard via OSC 52 — the terminal
escape sequence that ships clipboard data over your existing TTY.
Works over plain SSH with no `xclip`, `pbcopy`, or `wl-copy`
shelling out.

## How it hooks in

No keybinds: the plugin registers for `HOOK_YANK`, which core fires
after any yank lands in the registers (`y`, `yy`, visual `y`,
`:yank`, `:yank_line`, ...). Deletes don't fire it. On each yank the
unnamed register is mirrored to the system clipboard.

Local registers are still populated as normal; the plugin just additionally pushes a base64-
encoded OSC 52 sequence to the terminal.

## Terminal support

OSC 52 needs to be enabled on your terminal. Most modern terminals
support it out of the box: alacritty, kitty, wezterm, foot, ghostty,
iTerm2, Windows Terminal. tmux requires `set -g set-clipboard on`.

If your terminal doesn't honor OSC 52, the yank still goes to hed's
internal registers (so `p` works as expected); it just doesn't reach
the system clipboard.

## Disable

Set `plugin_load(&plugin_clipboard, …)` to `0` in `src/config.c` and
`:reload`.
