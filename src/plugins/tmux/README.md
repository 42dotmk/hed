# tmux

tmux runner pane integration. Spawns (or reuses) a sibling pane in the
current tmux window and sends commands to it. The pane is identified by
hed's PID, so it survives reuse across hed sessions only if PID matches.

## Commands

- `:tmux_toggle` — create/show the runner pane
- `:tmux_send <cmd>` — send literal text to the runner pane
- `:tmux_kill` — kill the runner pane

## Keybind

- `<space>ts` (normal mode) — send the paragraph under the cursor to the
  runner pane

## Notes

- Requires hed to be running inside tmux. Outside tmux the commands are
  no-ops with a status message.
- Command-mode history navigation in `src/command_mode.c` still calls
  this plugin's `tmux_history_*` API directly. That coupling is similar
  to the LSP fd-pump in `editor.c`/`main.c` — physically isolated, not
  yet logically decoupled.

## Enable

In `src/config.c`'s `config_init()`:

```c
plugin_load(&plugin_tmux, 1);
```
