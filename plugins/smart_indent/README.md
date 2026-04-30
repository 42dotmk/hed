# smart_indent

When the user inserts a newline in insert mode, copy the previous line's
leading whitespace onto the new line so indentation persists naturally.

Treats tabs as 4 spaces (matches the editor's default tab visual width).

## Hook

`HOOK_CHAR_INSERT` in `MODE_INSERT`, all filetypes. Activates only when
the inserted character is `\n`.

## Enable

In `src/config.c`'s `config_init()`:

```c
plugin_load(&plugin_smart_indent, 1);
```
