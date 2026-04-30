# treesitter

Tree-sitter syntax highlighting. Grammars are `.so` files loaded
dynamically from `$HED_TS_PATH` (defaults to `~/.config/hed/ts/` and
the in-tree `ts-langs/`). Each buffer gets its own `TSState*` in
`buf->ts_internal`. Highlight queries come from
`queries/<lang>/highlights.scm`.

Filetype detection is in `buf_detect_filetype()` (`src/buf/buffer.c`).
The plugin auto-loads the matching grammar on buffer open.

## Commands

- `:ts on|off|auto` — toggle highlighting (auto = detect by extension)
- `:tslang <name>` — force a language for the current buffer
- `:tsi <lang>` — install a grammar from GitHub (clones
  `tree-sitter-<lang>`, builds `<lang>.so`, copies it to `ts-langs/`)

## How core sees it

Core code (`src/buf/buffer.c`, `src/terminal.c`) calls `ts_is_enabled`,
`ts_buffer_autoload`, `ts_buffer_reparse` through **weak symbols**.
When this plugin is built, the refs resolve to the real functions;
when it isn't (e.g., `make PLUGINS_DIR=...` pointing at a set without
treesitter), the refs are NULL and core skips the calls — no crash,
no missing-symbol link errors.

## Library dependency

`libtree-sitter` is currently linked unconditionally by the Makefile.
If you build without this plugin, the dependency is unused — annoying
but harmless. Making the link conditional on the plugin's presence
would need a Makefile tweak.

## Enable

In `src/config.c`'s `config_init()`:

```c
plugin_load(&plugin_treesitter, 1);
```
