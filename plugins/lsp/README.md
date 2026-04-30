# lsp

Activates LSP client integration. The implementation lives in `src/lsp.c`,
`src/lsp_hooks.c`, and `src/commands/cmd_lsp.c` (still core today —
LSP is a partial / WIP feature). This plugin owns the activation layer:
lifecycle hooks (`HOOK_BUFFER_OPEN`, `HOOK_BUFFER_CLOSE`, `HOOK_BUFFER_SAVE`,
`HOOK_MODE_CHANGE`, `HOOK_KEYPRESS`) plus the `:lsp_*` command surface.

## Commands

- `:lsp_connect` — connect to a running LSP server
- `:lsp_disconnect` — disconnect
- `:lsp_status` — show connection state
- `:lsp_hover` — hover info at cursor
- `:lsp_definition` — goto definition
- `:lsp_completion` — completion at cursor

## Enable

In `src/config.c`'s `load_plugins()`:

```c
plugin_enable("lsp");
```
