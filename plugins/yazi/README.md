← [hed](../../README.md)

# yazi

Launch the [`yazi`](https://yazi-rs.github.io/) file manager as a
chooser. Selected paths come back as new buffers in hed.

## What happens

`:yazi` (also bound to `<space>fy` in the default leader map):

1. Build a temp chooser-file path under `$TMPDIR` (or `/tmp`).
2. Hand the terminal off to yazi via the same TUI handoff helper used
   by `:git`/lazygit — raw mode is suspended while yazi owns the
   screen, restored on exit.
3. yazi runs as `yazi --chooser-file=<tmp> [start_dir]`. The user
   selects one or more entries and presses Enter.
4. After yazi exits, the chooser file (one absolute path per
   selected entry, newline-terminated) is read back. Every non-empty
   line is opened in hed via `buf_open_or_switch`.

If the user quits yazi without selecting (`q`), the chooser file is
empty and nothing opens.

## Commands

```
:yazi              open at cwd
:yazi <path>       open at <path>
```

## Dependencies

`yazi` on `$PATH`. See https://yazi-rs.github.io/ for install.

## Notes

- Multiple-select is supported — yazi writes one path per line and
  hed opens all of them.
- `start_dir` defaults to the editor's logical cwd (`E.cwd`), not
  the cwd of the focused buffer. If you want to browse from the
  current buffer's directory, pass it explicitly: `:yazi <C-r>%`
  (or whatever your keymap exposes for "current file path").
- The chooser file is always cleaned up after yazi exits, even on
  error.
