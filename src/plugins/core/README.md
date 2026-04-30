# core

Ships hed's default command set (`:q`, `:w`, `:e`, `:bn`, `:bp`, `:fzf`,
`:rg`, `:tmux_*`, `:fold*`, etc.) and the small handful of editor-wide
hooks (cursor shape on mode change, undo grouping, smart indent,
auto-pair).

This plugin is "the editor's default behavior surface" — the things you
get out of the box. Commands owned by other plugins (`:lsp_*`, `:viewmd`,
`:keymap*`) live with their plugin and are not registered here.

Disabling `core` is technically possible but produces an editor with
almost no commands and no auto-formatting on insert. Mainly useful as a
starting point for someone building a radically different command surface.

## Enable

In `src/config.c`'s `config_init()`:

```c
plugin_load(&plugin_core, 1);
```
