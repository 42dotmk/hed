← [hed](../../readme.md)

# lsp

A minimal Language Server Protocol client. Speaks LSP over a TCP
socket to a running server, integrates hover and goto-definition
into hed.

> **Status:** work in progress. Hover and goto-definition work for
> servers that speak LSP-on-TCP. Diagnostics, completion, formatting,
> and rename are stubs. The plugin works alongside the rest of the
> editor, not as a critical-path dependency.

## Commands

| Command | Action |
|---|---|
| `:lsp_connect <host> <port>` | Connect to an LSP server listening on TCP |
| `:lsp_disconnect` | Tear down the connection |
| `:lsp_status` | Print connection state to the status line |
| `:lsp_hover` | Hover info for the symbol under the cursor |
| `:lsp_definition` | Jump to the definition of the symbol under the cursor |
| `:lsp_completion` | Trigger completion at the cursor (placeholder) |

## Default keybinds

The leader cluster maps `K` (NORMAL) to `:lsp_hover` and `gd` to
`:tag` (which `core` ships) — wire `gd` to `:lsp_definition` instead
in your `src/config.c` if you prefer LSP over ctags.

## Connecting

Most servers default to stdio; this plugin talks TCP. Either run a
TCP-mode server directly:

```bash
clangd --tcp-port=12345
```

Or wrap a stdio server in `socat`:

```bash
socat TCP-LISTEN:12345,reuseaddr,fork EXEC:'pyright-langserver --stdio'
```

Then inside hed:

```
:lsp_connect 127.0.0.1 12345
:lsp_hover
```

## Hover display

Hover responses render into a centered modal window. Press
`q` or `<Esc>` to dismiss; `j`/`k` to scroll.

## Architecture

- `lsp.c` / `lsp_plugin.h` — plugin entry, command registration.
- `lsp_impl.c` — JSON-RPC framing, request/response correlation, the
  socket pump (drained from `main.c` via the weak `lsp_fill_fdset` /
  `lsp_handle_readable` hooks).
- `cmd_lsp.c` — the user-facing `:lsp_*` commands.
- `lsp_hooks.c` — buffer/cursor hook handlers (notify the server of
  open/change/close, sync cursor position).
- `json_helpers.c` + `cjson/` — JSON encode/decode (vendored cJSON).

## Notes

- TCP only by design — keeps the implementation small and lets you
  run a server outside the editor process. Stdio servers can be
  fronted with `socat`.
- The plugin uses weak references from the editor's `select()` loop,
  so building hed without the plugin (e.g., a custom `PLUGINS_DIR`)
  is fine — the editor compiles and runs without the LSP fd-pump.
