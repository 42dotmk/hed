← [hed](../../readme.md)

# fmt

`:fmt` runs an external formatter against the current buffer's file
on disk, then reloads the buffer to pick up the result. Filetype-
dispatched: the formatter is selected based on the buffer's filetype.

## Default formatter table

| Filetype | Tool |
|---|---|
| `c`, `cpp` | `clang-format -i` |
| `rust` | `rustfmt` |
| `go` | `gofmt -w` |
| `python` | `black` |
| `javascript`, `typescript`, `json`, `html`, `css`, `markdown` | `prettier --write` |
| `shell` | `shfmt -w` |
| `lua` | `stylua` |

If the formatter for your filetype isn't installed, `:fmt` reports a
status-line error and leaves the buffer alone.

## Usage

```
:fmt              # format current buffer
```

A typical workflow: bind it to a key and call before saving. With
the default leader cluster, `<space>cf` is wired to `:fmt`.

## Notes

`fmt` writes the buffer to disk first (if dirty), then runs the
formatter on the on-disk file, then reloads. If the formatter
doesn't support `--write`-style in-place editing, this plugin won't
work for it as-is — patch the formatter table in
`plugins/fmt/fmt.c` if you need a different invocation pattern.

The formatter table is hardcoded (see the project roadmap — making
it user-configurable is on the list). To add a filetype, edit the
table and rebuild.
