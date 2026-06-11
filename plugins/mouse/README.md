# mouse

Mouse support: click to place the cursor, drag to select, wheel to
scroll.

## What it does

| Gesture | Effect |
|---|---|
| Left click | Focus the window under the pointer, place the cursor there |
| Left drag | Enter visual mode anchored at the press point, extend while held |
| Wheel up/down | Scroll the window under the pointer by 3 lines |
| `:mouse on\|off\|toggle` | Toggle terminal mouse reporting at runtime |

Clicks map through `window_screen_to_buffer()` (core, `terminal.c`),
which inverts the renderer's visual-row walk — line-number gutter,
collapsed folds, horizontal scroll, soft-wrap sublines and block-below
virtual text rows are all accounted for. Clicks on a virtual-text row
snap to its anchor line; clicks past end-of-line clamp to the last
character; clicks below EOF clamp to the last line.

A selection made by dragging survives button release (vim-like); the
next click or any mode change collapses it. Releases, middle and right
buttons are otherwise ignored.

Mouse events bypass keymap dispatch, macro recording and multicursor
key replay entirely, so they work identically in the vim, emacs and
vscode keymaps and can't corrupt a recorded macro.

## How it works

Core enables xterm button-event tracking (DEC 1002) with SGR encoding
(DEC 1006) while raw mode is active, parses `ESC [ < b ; x ; y M/m`
into a `MouseEvent` (`src/input/input.h`) and fires `HOOK_MOUSE`. This
plugin owns all semantics — disable it (or `:mouse off`) and the
editor is fully keyboard-driven again.

Reporting is re-armed after every shell-out (`:fmt`, `:git`, fzf,
tmux) because `enable_raw_mode()` carries the desired state.

## Caveats

- While reporting is on, the terminal's own selection is unavailable;
  most terminals offer Shift+drag as a bypass, or use `:mouse off`.
- Inside tmux, events reach hed as long as the inner program requests
  them — but `set -g mouse on` makes tmux consume them itself.
- Modal windows and active prompts ignore the mouse.
- Wheel-scrolling drags the cursor along with the view edge, since the
  renderer re-snaps the viewport to the cursor every frame.
