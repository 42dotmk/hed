# viewmd

Activates the markdown live-preview feature. The implementation lives in
`src/utils/viewmd.c` (still part of core today because it owns its own
sub-process management). This plugin is the activation shell: it wires up
the char/line/buffer hooks that drive incremental updates and registers
the `:viewmd` command.

## Commands

- `:viewmd` — toggle the markdown live preview for the current buffer.

## Enable

In `src/config.c`'s `load_plugins()`:

```c
plugin_enable("viewmd");
```
