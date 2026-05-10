← [hed](../../readme.md)

# scratch

An ephemeral, unnamed buffer always one command away. Useful for
quick notes, paste-and-edit, or any throwaway text you don't want to
commit to a file.

## Usage

```
:scratch
```

Opens the shared `[scratch]` buffer in a vertical split. If the
buffer is already shown in some window, focuses it instead of
creating a second split.

The buffer is kept non-dirty so `:q` from inside it just closes the
split. Contents persist for the lifetime of the editor session — if
you open `:scratch` again later, you find the same text.

## Default leader binding

`<space>z` is mapped to `:scratch` in the default `src/config.c`.

## Limitations

- One scratch buffer per session. Open a regular `:e file` if you
  want more than one.
- Contents are lost on quit — by design. Save with `:w <path>` if
  you want to keep them.
- The buffer has no filename, so syntax highlighting and formatting
  default to plain text.
