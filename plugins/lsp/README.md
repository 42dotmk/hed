# lsp

A Language Server Protocol client. Spawns servers from a built-in
registry (clangd, rust-analyzer, pyright, gopls, typescript-language-
server, lua-language-server, zls, csharp-ls — see `lsp_servers.c`) and
integrates
hover, goto-definition, references, diagnostics, completion, code
actions, rename and formatting into hed.

**Opt-in:** nothing runs until you say so. `:lsp_start` spawns the
server for the current buffer's filetype; `:lsp_autostart on` makes
buffer opens spawn registry servers automatically (put
`lsp_set_autostart(1);` in `~/.config/hed/config.c` to make that the
default). Without a server, `gd` falls back to ctags and completion
to the built-in buffer-word/path sources.

## Commands

| Command | Action |
|---|---|
| `:lsp_start [lang]` | Spawn a server from the registry (default: current buffer's filetype). Root is auto-detected from the buffer path via the registry's root markers. |
| `:lsp_autostart [on\|off\|toggle]` | Spawn registry servers automatically on buffer open (default off) |
| `:lsp_connect <lang> tcp <host>:<port> [root_uri]` | Attach to a server listening on TCP |
| `:lsp_connect <lang> <to_pipe> <from_pipe> [root_uri]` | Attach via two named pipes (hed writes `to_pipe`, reads `from_pipe`) |
| `:lsp_disconnect <lang>` | shutdown/exit handshake, then tear down |
| `:lsp_toggle [lang]` | Stop the server for the language (default: current filetype) if running, else start it (`<space>cl`) |
| `:lsp_status` | Per-server state on the status line |
| `:lsp_hover` | Hover popup for the symbol under the cursor (`K`) |
| `:lsp_definition` | Jump to the definition under the cursor |
| `:lsp_diagnostics` | Dump stored diagnostics into the quickfix list |
| `:lsp_code_action [kind]` | Pick and run a code action for the cursor or visual selection (`<space>ca`). With a kind filter a lone match runs without asking |
| `:lsp_rename [new_name]` | Rename the symbol under the cursor across the workspace (`<space>cr`, `F2` in the vscode keymap); asks for the name when omitted |
| `:lsp_format` | Format the document, or the visual selection (range formatting), through the server |
| `:lsp_references` | Every reference to the symbol under the cursor (declaration included) into the quickfix list (`<space>cu`, `Shift+F12` in the vscode keymap) |

Servers are keyed by the buffer's `filetype`; one server per language,
up to 8 concurrently.

## Goto definition (`gd`)

`gd` (and F12/`C-t` in the vscode keymap) runs `:definition`, a
dispatcher owned by the ctags plugin: when a ready LSP server is
attached to the buffer it asks the server (`lsp_definition_try`, a
weak symbol so ctags builds without lsp); otherwise it falls back to
`:tag`. Jumps push the jump list — `<space>jb` goes back — including
same-file jumps, and center the target line.

### Decompiled sources (csharp-ls)

A definition that lives in a referenced assembly (the BCL, a NuGet
package) comes back from csharp-ls as a `csharp:/…` URI rather than a
file. The plugin then asks the server for the decompiled source
(`csharp/metadata`, ILSpy under the hood) and shows it in a read-only
`csharp` buffer titled `[Assembly] Full.Type.Name`, cursor on the
symbol. The buffer keeps the `csharp:` URI as its filename, so `gd`
and `K` from inside decompiled code keep working (and re-jumping to
the same type reuses the buffer). Needs csharp-ls started with
`--features metadata-uris`, which the registry entry does.

## Code actions, rename, formatting

`:lsp_code_action` sends `textDocument/codeAction` for the cursor (or
the visual selection, which it leaves) with the diagnostics the server
published for that range as context, and offers the results through
`picker_list` as `N: title  [kind]`. The picked action's `edit` is
applied first, then its `command` goes out as
`workspace/executeCommand` — whose effects arrive as a
`workspace/applyEdit` server request, applied and answered
`{applied: true}`. Actions that arrive without an edit or command
(rust-analyzer's lazy ones) are completed with `codeAction/resolve`
first; bare `Command` results are executed directly. A kind filter
(`:lsp_code_action source.organizeImports`) becomes `context.only`,
and a single filtered result is applied without a picker — handy for
keybinds.

`:lsp_rename` and `:lsp_format` produce a `WorkspaceEdit` and a
`TextEdit[]` respectively; `:lsp_format` sends `rangeFormatting` when
a visual selection is active, with `tabSize`/`insertSpaces` from the
editor settings. Each command checks the matching `*Provider`
capability from the initialize result and says so when the server
lacks it.

All edits go through `lsp_edit.c`: a `WorkspaceEdit` (both the
`changes` map and the `documentChanges` array; resource operations are
not declared and are skipped if sent) is applied per document, and a
document's edits are applied bottom-up so earlier ranges stay valid,
with UTF-16 columns converted against the live row at apply time.
Each buffer's batch is one undo group. Files not open in a buffer are
loaded (hidden, `didOpen` goes out) and left dirty for you to save;
the status line reports `N edit(s) in M file(s)`.

## Completion

Completion is served through `plugins/completion`: this plugin
registers a `CompletionSource` whose trigger characters come from the
server's `completionProvider.triggerCharacters`. The menu, filtering,
keys and the accept edit all live in the completion plugin; this
plugin only converts `textDocument/completion` responses (labels,
kinds, `textEdit` ranges with UTF-16→byte conversion, snippet
placeholder stripping) into menu items.

## Document sync

Full-text `didOpen` on buffer open, `didClose`/`didSave` on close and
save. `didChange` is sent lazily: before every request (hover,
definition, completion) and on leaving insert mode, but only when the
buffer actually changed since the last sync — so the server always
sees live text without per-keystroke traffic.

All positions on the wire are UTF-16 code units, converted at the
boundary from hed's byte columns.

## Hover display

Hover responses render into a centered modal. `q`/`<Esc>` dismiss,
`j`/`k` scroll.

## Lifecycle

The fd of every server is registered on the editor's select loop
(`ed_loop_register`) — no polling, no core hooks. Server→client
requests (`workspace/configuration`, `client/registerCapability`,
`window/workDoneProgress/create`, …) are answered with empty results
so servers that block on them (rust-analyzer, lua-language-server)
never stall. On `:lsp_disconnect` and editor exit (plugin deinit +
`atexit`) each server gets a `shutdown` request and `exit`
notification before an escalating SIGTERM/SIGKILL reap.

## Architecture

- `lsp.c` / `lsp_plugin.h` — plugin entry, command registration,
  completion-source registration, atexit shutdown.
- `lsp_impl.c` — protocol state machine: servers, pending-request
  table, document sync, response handlers, the completion source,
  diagnostics store, code action / rename / formatting / references
  requests.
- `lsp_edit.c` — UTF-16 conversion, URI → buffer lookup, and the
  `TextEdit[]` / `WorkspaceEdit` applier (undo-grouped, bottom-up).
- `cmd_lsp.c` — the user-facing `:lsp_*` command thunks.
- `lsp_hooks.c` — buffer open/close/save + mode-change sync hooks and
  hover-popup keys.
- `lsp_servers.c` — the spawn registry (argv + root markers per lang).
- `json_helpers.c` + `cjson/` — JSON decode helpers (vendored cJSON).
- JSON-RPC framing/envelopes are shared: `plugins/jsonrpc/`.
