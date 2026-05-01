# scratch

An ephemeral, unnamed buffer always one command away. Useful for quick
notes, paste-and-edit, or any throwaway text you don't want to commit
to a file.

## Usage

```
:scratch
```

Opens the shared `[scratch]` buffer in a vertical split. If the buffer
is already shown in some window, focuses it instead of creating a
second split. Contents persist for the lifetime of the session.

The buffer has no filename and is kept non-dirty, so `:q` from inside
it just closes the split.

## Configure

Loaded by default in `src/config.c`. To bind it to a leader key, add:

```c
cmapn(" z", "scratch");   /* <space>z opens scratch */
```

## Limitations

- One scratch buffer per session. Open a regular `:e file` if you want
  more than one.
- Contents are lost on quit — by design. Save with `:w <path>` if you
  want to keep them.
