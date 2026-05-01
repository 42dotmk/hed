# viewmd

Activates the markdown live-preview feature. The implementation lives in
`src/utils/viewmd.c` (still part of core today because it owns its own
sub-process management). This plugin is the activation shell: it wires up
the char/line/buffer hooks that drive incremental updates and registers
the `:viewmd` command.

> ⚠️ **A patched `viewmd` binary is required.** The plugin spawns
> `viewmd --socket` and streams buffer contents over a UNIX socket
> (see `viewmd_impl.c`). Upstream `viewmd` does not implement that
> interface — install the patched fork before using this plugin, or
> `:viewmd` will fail to launch. _TODO: add fork URL._

## Commands

- `:viewmd` — toggle the markdown live preview for the current buffer.

## Enable

In `src/config.c`'s `load_plugins()`:

```c
plugin_enable("viewmd");
```
