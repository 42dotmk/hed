# autosave

Idle / timer autosave for dirty buffers, with a recovery prompt on
reopen. Crash-safe — the editor cache survives a kill -9 or a power
loss without overwriting your real source files until you `:w`.

## Layout

For an editor cwd of `/mnt/storage/probe/hed` and a buffer named
`src/foo.c`, the autosave file lives at:

```
~/.cache/hed/%mnt%storage%probe%hed/autosave/src/foo.c
```

— same per-cwd cache dir the editor log uses, with the original
relative path mirrored under `autosave/`. Easy to grep, easy to wipe
(`rm -rf` the cwd's cache dir).

Writes are atomic: `<path>.tmp` is created, `fsync`'d, then renamed
over the target. A crash mid-write leaves no half-written autosave.

## Cadence

Two triggers:

- **3 seconds of idleness** after the last keystroke (`HOOK_CHAR_INSERT`
  / `HOOK_CHAR_DELETE` schedule a debounced timer; further edits reset
  it).
- **Every transition out of INSERT mode** (`<Esc>` from insert flushes
  immediately).

Tunable today only by editing `AUTOSAVE_IDLE_MS` in `autosave.c` and
rebuilding.

## Recovery

When a buffer opens (`HOOK_BUFFER_OPEN`), the plugin checks whether a
fresh autosave exists. "Fresh" means: the autosave file's mtime is
strictly newer than the on-disk file's mtime. If yes, the user is
prompted via the shared `ask` helper:

```
autosave found for src/foo.c — restore? (y/n) y
```

- `y` / `Y` (or just Enter, since `"y"` is the default) replaces the
  buffer's rows with the autosave contents and marks the buffer
  dirty (`buf->dirty = 1`). You still have to `:w` to commit.
- Anything else discards the autosave silently.

Stale autosaves (where the on-disk file is at least as new) are
deleted without prompting — they can't tell you anything you don't
already have.

The undo stack is dropped on restore. Otherwise undo would walk the
buffer through ghost edits that never actually happened in this
session, which is more confusing than helpful.

If a prompt is already open when the file opens (multiple files on
the cli, the recovery prompt for the first is still up), the rest
fall back to a status message and you can re-prompt with
`:autosave restore`.

## On `:w`

`HOOK_BUFFER_SAVE` unlinks the autosave file, so a successful save
returns the buffer to a state with no autosave to recover.

## Skips

- Buffers without a filename.
- Read-only buffers.
- Plugin scratch buffers (titles starting with `[` — `[copilot]`,
  `[scratch]`, `[claude]`, …).
- Files larger than 10 MB (`AUTOSAVE_MAX_BYTES`). Big logs and binary
  blobs would otherwise generate a lot of disk traffic for no
  recovery benefit.

## Commands

```
:autosave                  status (alias for :autosave status)
:autosave on               enable autosave
:autosave off              disable + cancel pending timer
:autosave toggle
:autosave status           show enabled / idle ms / size cap
:autosave restore          re-prompt the recovery dialog for the
                           current buffer (use after the auto-prompt
                           was suppressed by another open prompt)
:autosave now              flush every dirty buffer immediately
```

Defaults to **on**. To start with autosave off, edit `src/config.c`
and pass `0` instead of `1` to `plugin_load(&plugin_autosave, …)`.

## Implementation notes

- Single source file (`autosave.c`, ~300 lines). Uses
  `path_cache_file_for_cwd()` for the base directory, `path_mkdir_p`
  for intermediate dirs, and `ed_loop_timer_after("autosave:idle",
  …)` for the debounced timer (rename-replaces-prior semantics give
  free debounce).
- The recovery prompt is built on `src/ui/ask.h`, the shared one-line
  prompt helper. The callback parses the answer; the plugin makes no
  policy beyond "starts with y or Y means yes."
