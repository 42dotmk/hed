← [hed](../../readme.md)

# dired

A directory browser inspired by `oil.nvim`: opening a directory drops
you into a buffer where each line is a directory entry. Navigate with
your normal motions, hit `<CR>` to descend or open a file.

## Usage

```
:e .              # open current directory in dired
:e some/dir       # open a specific directory
```

Any time `:e` is given a directory, dired claims the open via the
`HOOK_BUFFER_OPEN_PRE` intercept and renders the listing instead of
core's default file-open path.

## Keybinds (NORMAL mode, dired buffer only)

| Key | Action |
|---|---|
| `<CR>` | Open the entry under the cursor (file or subdir) |
| `-` | Go to parent directory |
| `~` | Go to `$HOME` |
| `cd` | Change hed's cwd to this directory |

`hjkl`, `gg`, `G`, search, etc. all work — it's a normal buffer.

## Display

Each line renders as `<type-marker> <name>`:

- `d` — directory
- `l` — symlink (target shown after `->`)
- ` ` — regular file

Hidden entries (starting with `.`) are shown.

## Notes

The dired buffer is read-only — you can't rename or delete entries by
editing the listing (yet). Use `:shell` for filesystem operations.
