# session

Persists and restores the open-buffer list for the current cwd.

## Commands

```
:session-save      write open buffers to ~/.cache/hed/<encoded-cwd>/session
:session-restore   open every file from that session, switching to the
                   one marked current
```

The session file is plain text — one path per line, current buffer
prefixed with `* `, others with `  `. Easy to edit by hand.

## Auto-restore after :reload

The `reload` plugin sets `HED_RELOAD=1` and writes the session file
just before `execl`-ing the new binary. On startup, this plugin
checks the env var via `HOOK_STARTUP_DONE`, restores the buffers,
and removes the file. No manual step needed.

## Underlying API

The save/restore primitives live next to the plugin in `session.h` so
other plugins can compose on top:

```c
#include "session/session.h"

EdError session_save(const char *path);
EdError session_restore(const char *path);
```

The `reload` plugin uses this directly to hand the open-buffer list
across an `execl` boundary.
