← [hed](../../readme.md)

# multicursor

Synchronized edits at multiple cursors. Add an extra cursor; every
keystroke after that — motion, insert, operator, leader chord, mode
change — fires once per cursor.

## Commands

| Command | Action |
|---|---|
| `:mc_add_below` | Add an extra cursor on the line below |
| `:mc_add_above` | Add an extra cursor on the line above |
| `:mc_clear` | Drop the extras, keep only the active cursor |
| `:mc_count` | Show how many cursors are active |

## Default keybinds

| Key | Mode | Action |
|---|---|---|
| `<space>md` | normal | `:mc_add_below` |
| `<space>mu` | normal | `:mc_add_above` |
| `<space>mc` | normal | `:mc_clear` |

## How replay works

The plugin hooks `HOOK_KEYPRESS` and, when more than one cursor
exists, replays `ed_dispatch_key(c)` at every cursor — active first,
then the rest in descending `(y, x)` order so earlier positions
don't shift under later edits.

Before each replay, the pre-keypress mode and key-sequence buffer
are restored, so:

- `i` enters INSERT at every cursor instead of typing `i` literally
  at the second one.
- Multi-key sequences (`dd`, `ciw`, `ya"`) replay correctly — the
  first key arms the operator at every cursor, and the next key
  completes it at every cursor.
- Leader bindings work the same way.

A re-entrancy guard prevents the dispatch from re-triggering the
keypress hook on its own replays.

## Limitations

- Visual-block mode interactions are minimal — the plugin treats
  block selection as a single primary cursor with the column span
  driven by the renderer, not by per-cursor objects.
- Operations that read/modify a single global selection (yank
  registers, search, fzf) still happen once per replay rather than
  being merged. Useful in practice; surprising in edge cases.
- `deinit` is a no-op (the editor doesn't yet support unregistering
  hooks), so toggling this plugin off requires a `:reload`.
