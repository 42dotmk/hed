← [hed](../../readme.md)

# Multicursor

Synchronized edits at multiple cursors. Add an extra cursor; every
keystroke after that — motion, insert, operator, leader chord, mode
change — fires once per cursor.

## Commands

| Command | Action |
|---|---|
| `:mc_add_below`  | Add an extra cursor on the line below |
| `:mc_add_above`  | Add an extra cursor on the line above |
| `:mc_next_match` | Add a cursor at the next match of the word under cursor (or visual selection) |
| `:mc_skip`       | Drop the active cursor and move to the next in cyclic order |
| `:mc_clear`      | Drop the extras, keep only the active cursor |
| `:mc_count`      | Show how many cursors are active |
| `:mc_debug`      | Toggle a verbose log_msg trace of every dispatch |

## Default keybinds

| Key | Mode | Action |
|---|---|---|
| `<space>md` | normal | `:mc_add_below` |
| `<space>mu` | normal | `:mc_add_above` |
| `<space>mc` | normal | `:mc_clear` |
| `<C-n>`     | normal, visual | `:mc_next_match` |
| `Q`         | normal | `:mc_skip` |

`<C-n>` mirrors VSCode's `Ctrl-D` ("select next occurrence"). On the
first press it picks up the word under the cursor (or a single-line
visual selection), seeds `E.search_query` with it, and puts a new
cursor at the next match. Each subsequent press adds one more cursor
at the following match, wrapping past the end of the buffer.

`Q` is "skip this match": drop the active cursor and advance to the
next cursor in `(y, x)` order, cycling back to the first when there's
nothing after the current one.

## How replay works

Two phases.

**Phase 1** — dispatch the key once at the original active cursor
with `in_replay = 0`. Multicursor's own commands (`C-n`, `Q`,
`mc_add_below`, `mc_add_above`, `mc_clear`) actually run here: they
guard themselves with `if (in_replay) return;` so they only execute
on the active dispatch, never on a per-cursor replay. If this
dispatch changed the cursor count (a list operation happened), we
stop — exactly one dispatch is the right amount, and replaying it at
every extra would compound.

**Phase 2** — replay at every cursor except the original active,
sorted descending by `(y, x)` so an edit at a later position doesn't
shift the column of an unprocessed cursor. The pre-keypress mode and
key-sequence buffer are restored before each replay, so:

- `i` enters INSERT at every cursor instead of typing `i` literally
  at the second one.
- Multi-key sequences (`dd`, `ciw`, `ya"`) replay correctly — the
  first key arms the operator at every cursor, and the next key
  completes it at every cursor.
- Leader bindings work the same way.

A re-entrancy guard (`in_replay`) prevents Phase 2 dispatches from
re-firing the keypress hook on themselves.

### Skipped contexts

The hook bails out (no replay, doesn't consume the event) when a
single global UI is active:

- **Prompt input** (`:` / `/` / `?`): replaying would type each
  character N times into the same prompt buffer.
- **Modal windows** (dired confirms, LSP popups, selectlist):
  these route keys globally; per-cursor replay has no meaning there.

The same bail-out triggers inside Phase 2 if a Phase 1 dispatch
opens a prompt or modal mid-sequence (e.g. `:` from normal mode).

## Limitations

- Visual-block mode interactions are minimal — the plugin treats
  block selection as a single primary cursor with the column span
  driven by the renderer, not by per-cursor objects.
- `:mc_next_match` only supports single-line visual selections;
  multi-line selections report "multi-line selection not supported".
- Operations that read/modify a single global selection (yank
  registers, search, fzf) still happen once per replay rather than
  being merged. Useful in practice; surprising in edge cases.
- `deinit` is a no-op (the editor doesn't yet support unregistering
  hooks), so toggling this plugin off requires a `:reload`.
