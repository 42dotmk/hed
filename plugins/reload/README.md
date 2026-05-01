# reload

Rebuild hed and replace the running process with the new binary,
without losing your open buffers.

## Usage

```
:reload
```

Steps:

1. Run `make -j16` in the current working directory.
2. If the build fails, abort and surface the status to you.
3. If it succeeds, ask the `session` plugin to write the open-buffer
   list to `~/.cache/hed/<encoded-cwd>/session`.
4. Set `HED_RELOAD=1` in the environment.
5. `execl ./build/hed`. Same PID, fresh code.

After exec, the new process sees `HED_RELOAD=1`, reads the session
file via the `session` plugin's `HOOK_STARTUP_DONE` handler, reopens
your buffers, and removes the session file.

## Why a separate plugin

Reload is the only sensible name for a "rebuild and exec myself"
command, but the persistence half — saving and restoring the open-
buffer list — is reusable. So:

- `reload` owns the build / exec mechanic.
- `session` owns `session_save` / `session_restore` and the
  `HED_RELOAD` env var contract.

If you load `reload` without `session`, `:reload` still rebuilds and
relaunches, but doesn't reopen buffers.

## Notes

- Assumes the cwd is the repo root and that `./build/hed` is the
  output. If you run hed from elsewhere, `:reload` won't find the
  binary to exec.
- The build step uses `make -j16`. Edit `plugins/reload/reload.c` if
  you want a different parallelism.
- Default leader binding: `<space>rr` is wired to `:reload`.
