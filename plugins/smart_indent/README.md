← [hed](../../readme.md)

# smart_indent

Carries indentation onto new lines. When you press `<Enter>` in
INSERT mode, the new line starts with the same leading whitespace
(spaces and tabs) as the line you came from.

## Behavior

- Mirrors leading whitespace exactly — if the previous line started
  with two tabs and four spaces, the new line does too.
- After `{`, `[`, `(` at the end of the previous line: adds one
  level of additional indent on top of the carried indent.
- After a closing brace at the start of the new line (e.g., you type
  `}` on a freshly auto-indented line): nothing — current behavior
  doesn't dedent. Type `<BS>` to clean up.

## Notes

The indent unit is a single tab character. If you use spaces, the
carry-over still works (it copies whatever was there) but the extra
level after `{`/`[`/`(` is a tab. If you want spaces-only, edit
`plugins/smart_indent/smart_indent.c`.

The plugin hooks `HOOK_LINE_INSERT` (newline) — it does not run for
re-indenting on `<Tab>` mid-line, paste, or block edits.

## Disable

Set `plugin_load(&plugin_smart_indent, …)` to `0` in `src/config.c`
and `:reload`.
