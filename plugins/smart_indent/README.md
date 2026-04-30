# smart_indent

When the user inserts a newline in insert mode, copy the previous line's
leading whitespace onto the new line so indentation persists naturally.

Whitespace is replicated **byte for byte** — if the previous line was
indented with tabs, the new line gets tabs; spaces, spaces; mixed
("\\t\\t  "), the same mix. No tab→space normalization.

## Hook

`HOOK_CHAR_INSERT` in `MODE_INSERT`, all filetypes. Activates only when
the inserted character is `\n`.

## Enable

In `src/config.c`'s `config_init()`:

```c
plugin_load(&plugin_smart_indent, 1);
```
