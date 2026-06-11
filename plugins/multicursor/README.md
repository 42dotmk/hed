← [hed](../../readme.md)

# Multicursor

Extra cursors with optional synchronized edits. Add extra cursors and
hop between them while they hold their positions — or turn **sync**
on, and every keystroke after that — motion, insert, operator, leader
chord, mode change — fires once per cursor.

## Two modes

**Sync off (default).** Extra cursors are passive markers. You move
and edit with the active cursor only; the others stay where you put
them (core auto-shifts them when edits land before them, so they stay
glued to their text). Use `:mc_jump_next` / `:mc_jump_prev`
(`<space>mj` / `<space>mk`) to make another cursor the active one.

**Sync on** (`:mc_sync` / `<space>ms`). Synchronized editing: every
keypress runs the full per-mode dispatch once per cursor (see
"How replay works" below).

## Per-window cursor sets

Cursor sets belong to a **(buffer, window) pair**. Two windows showing
the same buffer each have their own set; switching focus or buffers
parks one set and restores the other (handled by core,
`buf_cursors_bind_window`). A buffer that's no longer shown in any
window keeps the set of the last window that showed it, and the next
window to show the buffer adopts that set.

## Commands

| Command | Action |
|---|---|
| `:mc_add_below`  | Add an extra cursor on the line below |
| `:mc_add_above`  | Add an extra cursor on the line above |
| `:mc_add_here`   | Park a cursor at the current position and continue with the active one |
| `:mc_toggle_here` | Toggle: remove the parked cursor under the active one, or park one here |
| `:mc_next_match` | Add a cursor at the next match of the word under cursor (or visual selection) |
| `:mc_prev_match` | Add a cursor at the previous match |
| `:mc_match_all`  | Put a cursor at every match in the buffer |
| `:mc_jump_next`  | Make the next cursor (cyclic `(y, x)` order) the active one |
| `:mc_jump_prev`  | Make the previous cursor the active one |
| `:mc_sync [on\|off\|toggle]` | Enable/disable synchronized edits at all cursors |
| `:mc_skip`       | Drop the active cursor and move to the next in cyclic order |
| `:mc_clear`      | Drop the extras, keep only the active cursor |
| `:mc_count`      | Show how many cursors are active |
| `:mc_debug`      | Toggle a verbose log_msg trace of every dispatch |

## Default keybinds

Two mirrored clusters bind the same letters: `<space>m…` and `'…`.
The `'` cluster always works; the `<space>m` cluster is shadowed by
the tasks plugin inside markdown buffers (filetype-scoped bindings
win). Bare `'` is deliberately unbound — an exact match fires
immediately, which would make the two-key `'x` sequences unreachable;
toggle is the double-tap `''` instead.

| Key | Mode | Action |
|---|---|---|
| `<space>md` / `'d` | normal | `:mc_add_below` |
| `<space>mu` / `'u` | normal | `:mc_add_above` |
| `<space>ma` / `'a` | normal | `:mc_add_here` |
| `<space>mt` / `''` | normal | `:mc_toggle_here` |
| `<space>mc` / `'c` | normal | `:mc_clear` |
| `<space>mj` / `'j` | normal | `:mc_jump_next` |
| `<space>mk` / `'k` | normal | `:mc_jump_prev` |
| `<space>ms` / `'s` | normal | `:mc_sync` (toggle) |
| `<space>mn` / `'n` | normal | `:mc_next_match` |
| `<space>mp` / `'p` | normal | `:mc_prev_match` |
| `'*` / `<M-n>` | normal | `:mc_match_all` |
| `'n` / `<C-n>` | visual | `:mc_next_match` on the selection, exits visual |
| `'p`           | visual | `:mc_prev_match` on the selection, exits visual |
| `'*` / `<M-n>` | visual | `:mc_match_all` on the selection, exits visual |
| `<C-n>`     | normal | `:mc_next_match` |
| `Q`         | normal | `:mc_skip` |

`<C-n>` mirrors VSCode's `Ctrl-D` ("select next occurrence"). On the
first press it picks up the word under the cursor (or a single-line
visual selection), seeds `E.search_query` with it, and puts a new
cursor at the next match. Each subsequent press adds one more cursor
at the following match, wrapping past the end of the buffer.

`'*` / `<M-n>` mirrors VSCode's `Ctrl-Shift-L` ("select all
occurrences"): one press puts a cursor at **every** match of the word
under cursor (normal) or the single-line selection (visual) across
the whole buffer, replacing the current cursor set. The active cursor
stays on the occurrence the query came from. Matches are
non-overlapping, scanned left to right. Follow with `'s` to turn sync
on and edit them all at once, and `Q` to skip any match you don't
want.

`Q` is "skip this match": drop the active cursor and advance to the
next cursor in `(y, x)` order, cycling back to the first when there's
nothing after the current one.

## How replay works (sync on)

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

The hook bails out (no replay, doesn't consume the event) when sync
is off, or when a single global UI is active:

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
