# fmt

`:fmt` — run an external formatter against the current buffer, then reload
the file from disk to pick up the changes. Formatter is selected by the
buffer's filetype.

## Built-in rules

| Filetype | Command |
|---|---|
| `c`, `cpp` | `clang-format -i` |
| `rust` | `rustfmt` |
| `go` | `gofmt -w` |
| `python` | `black` |
| `javascript`, `typescript`, `html`, `css`, `markdown` | `prettier --write` |
| `json` | `prettier --parser json --write` |

If the buffer has no filename or the filetype isn't in the table, `:fmt`
prints a status message and does nothing.

## Caveats

- Treated as save+reload — your changes are written to disk before the
  formatter runs.
- Does NOT integrate with the undo stack. After `:fmt`, undoing brings
  back the pre-format file but only down to the last save snapshot.
- Raw mode is temporarily disabled around the `system()` call so the
  formatter can write to stdout/stderr without interfering with hed's
  rendering.

## Adding a new formatter

Edit the `rules[]` table in `fmt.c`. `%s` in the template is replaced
with the shell-escaped buffer path. PRs welcome to make this data-driven
via config (e.g., a `:setformatter <ft> <cmd>` command).

## Enable

In `src/config.c`'s `config_init()`:

```c
plugin_load(&plugin_fmt, 1);
```
