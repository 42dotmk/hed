# auto_pair

When the user types an opening bracket or quote in insert mode, automatically
inserts the matching closing one and steps the cursor back so they can keep
typing inside the pair.

Pairs handled: `()`, `[]`, `<>`, `{}`, `""`, `''`, `` `` ``.

## Hook

`HOOK_CHAR_INSERT` in `MODE_INSERT`, all filetypes.

## Enable

In `src/config.c`'s `config_init()`:

```c
plugin_load(&plugin_auto_pair, 1);
```
