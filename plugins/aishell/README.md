← [hed](../../readme.md)

# aishell

Opens an AI assistant (Claude Code, Copilot, etc.) in a dedicated tmux
pane and lets you push text into it from the editor.

Piggybacks on the `tmux` plugin's named-pane registry, so the
lifecycle (create on first toggle, hide via `break-pane`, show via
`join-pane`, kill) is identical to the runner pane — just with your
configured command instead of a shell.

## Configuration

Set the spawn command in `src/config.h` (or your custom config) before
loading the plugin:

```c
/* Default is "claude" — change to use something else: */
aishell_set_spawn_cmd("copilot");
aishell_set_spawn_cmd("aider");
aishell_set_spawn_cmd("windsurf");
```

## Commands

| Command | Action |
|---|---|
| `:ai_toggle` | Open / hide the AI shell pane |
| `:ai_send <text>` | Send literal text to the AI shell pane |
| `:ai_kill` | Kill the AI shell pane (and the process in it) |

## Requirements

- `tmux` on `$PATH`, and hed running inside a tmux session.
- The `tmux` plugin loaded (this plugin depends on its
  `tmux_pane_*` API).
- Your configured AI shell CLI on `$PATH`. Without it, the pane opens
  and immediately exits.

## Notes

- The pane is named `aishell` in tmux's pane registry, so
  `:tmux_toggle` (the runner pane) and `:aishell_toggle` are
  independent — you can keep both around at once.
- `:aishell_send` is a thin wrapper around `tmux send-keys`. Anything
  that works at your AI shell prompt works here.
