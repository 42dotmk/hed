# reload

`:reload` rebuilds hed via `make -j16` and `execl`s the freshly built
binary in place. Open buffers survive the swap as long as the
`session` plugin is also loaded.

## Contract with the session plugin

Before exec, `reload` writes the open-buffer list to
`~/.cache/hed/<encoded-cwd>/session` (via `session_save`) and sets
`HED_RELOAD=1` in the environment. The `session` plugin reads both
on startup and restores the list, then unsets the env var and
removes the file.

If the `session` plugin isn't loaded, `:reload` still works — it just
won't restore buffers on the other side.
