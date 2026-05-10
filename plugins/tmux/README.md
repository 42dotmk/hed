← [hed](../../readme.md)

# tmux

Treats a sibling tmux pane as a runner — send the paragraph under
the cursor (or a specific command) into a shell pane and watch the
output update next to your editor.

## Commands

| Command | Action |
|---|---|
| `:tmux_toggle` | Open / close the runner pane |
| `:tmux_send <cmd>` | Send a literal command to the runner pane |
| `:tmux_send_line` | Send the paragraph under the cursor |
| `:tmux_send_selection` | Send the current visual selection |
| `:tmux_kill` | Kill the runner pane (and the shell in it) |

The runner pane is a regular tmux pane, opened to the right of the
window hed is running in.

## Default keybinds

| Key | Mode | Action |
|---|---|---|
| `<space>tt` | normal | `:tmux_toggle` |
| `<space>tT` | normal | `:tmux_kill` |
| `<space>ts` | normal | `:tmux_send_line` (send paragraph under cursor) |
| `<space>ts` | visual | `:tmux_send_selection` (send the highlighted text) |

## What "paragraph under cursor" means

Lines from the previous blank line down to the next blank line
(matches `{` / `}` Vim motions). The whole paragraph is sent as a
single multi-line command — useful for one-shot REPL fragments,
SQL statements, or shell scripts.

## Sending a visual selection

In any visual mode (`v`, `V`, `<C-v>`), `<space>ts` (or
`:tmux_send_selection`) sends the highlighted text to the last
focused tmux pane and exits visual mode. Character-, line-, and
block-wise selections are all joined with newlines, matching the
behaviour of `:tmux_send_line` for paragraphs.

## Requirements

- `tmux` on `$PATH`
- hed must itself be running inside a tmux session (otherwise there
  is no current pane to split off from)

If tmux isn't available, the commands fail with a status-line
message and the rest of the editor keeps working.

## Notes

- The runner pane is just a tmux pane with a shell — anything
  interactive (REPLs, `htop`, `man`) works. `:tmux_send` is
  literally `tmux send-keys`.
- Add `set -as terminal-features ',alacritty:sync'` (or your outer-
  terminal equivalent) to your `~/.tmux.conf` so synchronized-output
  escapes propagate from hed through tmux. Without it tmux consumes
  the sync escape itself, which is harmless but means flicker
  reduction stops at tmux.
