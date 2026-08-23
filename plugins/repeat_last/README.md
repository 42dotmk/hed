# repeat_last

Vim-style dot repeat: `.` repeats the last change.

The plugin records the raw key run of the last buffer-modifying
command into the `.` register — an operator with its arguments
(`diw`, `dfx`), a simple edit (`x`, `J`, `p`), or a whole insert
session (`ciwNEW<Esc>`) — and registers `:repeat`, which replays that
run through the macro queue. The vim keymap binds `.` to `:repeat`.

## How it works

Two observational core hooks do the heavy lifting:

- `HOOK_KEY_RAW` fires from `ed_read_key()` for every *real*
  keypress, including keys read inside command callbacks (operator
  arguments, `f`/`t` targets) that never reach `HOOK_KEYPRESS`.
  Replayed keys (macro queue, multicursor replay) don't fire it, so a
  repeat never re-records itself.
- `HOOK_DISPATCH_POST` fires after each main-loop key is fully
  dispatched. Back in plain normal mode with no pending sequence or
  count, the run is complete: it's saved to the `.` register if it
  changed the buffer, discarded otherwise.

Change detection uses `undo_mod_generation()` — a monotonic counter
bumped by every recorded mutation — rather than `buf->dirty`, which
several in-row edit paths never touch. Undo/redo (`u`, `U`, `<C-r>`),
ex commands, and `.` itself never become the repeat target.

## Notes

- A run longer than 4KB of serialized keys is dropped rather than
  half-repeated.
- Bracketed-paste bodies are read raw by core and are not part of a
  run; repeating a change that contained a paste replays everything
  but the pasted text.
