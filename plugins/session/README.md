← [hed](../../readme.md)

# session

Saves and restores the list of open buffers for the current cwd.
Used by the `reload` plugin to survive `:reload`, and exposed
directly so you can take ad-hoc workspace snapshots.

## Commands

```
:session-save        # write open buffers to the cache file
:session-restore     # restore from the cache file
```

The cache file lives at `~/.cache/hed/<encoded-cwd>/session`. The
cwd is encoded by replacing `/` with `%`, so each working directory
gets its own session.

## File format

Plain text, one buffer per line. The current buffer is prefixed with
`* `, others with `  `. Buffers without a filename (scratch, `[No
Name]`) are skipped on save.

```
* /home/me/project/src/main.c
  /home/me/project/src/foo.c
  /home/me/project/Makefile
```

Easy to edit by hand, easy to grep.

## Auto-restore after `:reload`

The `reload` plugin sets `HED_RELOAD=1` and writes the session file
just before `execl`-ing the new binary. On startup, this plugin
checks the env var via the `HOOK_STARTUP_DONE` hook, restores the
buffers, deletes the file, and unsets the env var. No manual step
needed.

## Underlying API

```c
#include "session/session.h"

EdError session_save(const char *path);
EdError session_restore(const char *path);
```

Pure mechanic — no policy. Decides nothing about where to put the
file, when to fire, or whether to clean up. Compose those concerns
on top.

## Notes

- `session_restore` closes any empty unnamed placeholder buffer that
  startup left behind, so the restored list isn't padded with a
  bogus `[No Name]` entry.
- The session file is overwritten on every save — there's no
  multi-session history yet. If you want named sessions, copy the
  file out before saving.
