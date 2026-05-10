← [hed](../../readme.md)

# copilot

GitHub Copilot ghost-text suggestions via the official
`@github/copilot-language-server` JSON-RPC server.

## Install

The plugin spawns the language server as a child process. Either:

```bash
npm install -g @github/copilot-language-server
```

— in which case the `copilot-language-server` shim ends up on `$PATH`
and the plugin uses it automatically — or install it locally inside
the project (`./node_modules/@github/copilot-language-server/`), which
is also auto-detected. To override, set `HED_COPILOT_LSP` to either a
binary or a `.js` file path.

## Sign in

```
:copilot login              -> prints a userCode and a verification URL
:copilot login <userCode>   -> confirm after entering the code in the browser
:copilot status             -> show signed-in user and enable state
:copilot logout
```

GitHub stores the OAuth token in `~/.config/github-copilot/`; future
sessions reconnect without re-logging-in.

## How it behaves

- Once signed in, every char insert in INSERT mode at end-of-line
  schedules a debounced (250 ms) `getCompletions` request.
- The first returned completion renders as ghost text on the current
  row using the `copilot` virtual-text namespace.
- `<Tab>` in INSERT mode accepts the suggestion. With no live
  suggestion, it falls back to inserting a literal tab (or spaces, if
  `expand_tab`).
- Cursor move off the suggestion's anchor row, or leaving INSERT
  mode, dismisses it (and notifies the server with `notifyRejected`).
- Suggestions only render at end-of-line in v1; mid-line ghost text
  needs the inline placement form of virtual_text (currently EOL-only).

## Alternatives pane

```
:copilot pane    -> open a horizontal split listing all alternatives
:copilot next    -> show the next alternative as the active ghost
:copilot prev    -> show the previous alternative
```

The `[copilot]` buffer in the bottom pane refreshes on every new
response. The active alternative is marked with `*` at the start of
its line.

## Other commands

```
:copilot enable | disable    -> toggle ghost-text fetching
:copilot restart             -> kill the server and respawn
```

## Implementation notes

- The plugin reuses the bundled `cJSON` from `plugins/lsp/cjson/`.
  Framing (`Content-Length: …` LSP-style headers) mirrors
  `plugins/lsp/lsp_impl.c`.
- The `copilot` virtual-text namespace opts out of edit-time auto
  clearing (see `vtext_ns_set_auto_clear`); the plugin manages its
  own clears so ghost text survives the keystroke that triggered the
  request.
- Document positions are sent as byte offsets (matching the editor's
  cursor.x). Strict UTF-16 code-unit math is a v2 concern; ASCII files
  are the common case.
- v1 sends a fresh `doc.source` with every `getCompletions` and a
  `textDocument/didOpen` per buffer at sign-in time; full incremental
  `didChange` is deferred.
