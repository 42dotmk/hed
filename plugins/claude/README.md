← [hed](../../readme.md)

# claude

Opens [Claude Code](https://claude.com/claude-code) in a dedicated
tmux pane and lets you push text into it from the editor.

Piggybacks on the `tmux` plugin's named-pane registry, so the
lifecycle (create on first toggle, hide via `break-pane`, show via
`join-pane`, kill) is identical to the runner pane — just with
`claude` as the spawn command instead of a shell.

## Commands

| Command | Action |
|---|---|
| `:claude_toggle` | Open / hide the claude pane |
| `:claude_send <text>` | Send literal text to the claude pane |
| `:claude_kill` | Kill the claude pane (and the process in it) |

## Requirements

- `tmux` on `$PATH`, and hed running inside a tmux session.
- The `tmux` plugin loaded (this plugin depends on its
  `tmux_pane_*` API).
- The `claude` CLI on `$PATH`. Without it, the pane opens and
  immediately exits.

## Notes

- The pane is named `claude` in tmux's pane registry, so
  `:tmux_toggle` (the runner pane) and `:claude_toggle` are
  independent — you can keep both around at once.
- `:claude_send` is a thin wrapper around `tmux send-keys`. Anything
  that works at the Claude Code prompt works here.
