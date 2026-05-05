# whichkey

When you start a multi-key chord and pause, whichkey lists the
candidate completions in the message bar — a small read-only popup
that goes away the moment you finish the sequence or wait long
enough for the dispatcher to time out.

It's pure discoverability: type `<space>` and you can see every
leader binding without leaving the editor.

## What you see

```
<space>
 ff         fzf                | fr         recent files     | fc         command picker
 ws         split              | wv         vsplit            | ww         focus next
 sd         rg                 | sa         rgword            | ss         ssearch
 ts         send paragraph     | tt         tmux toggle       | tH         whichkey toggle
 ...
```

- The first line shows the active sequence (and any numeric prefix —
  e.g. `42 d` while typing `42d…`).
- Each cell shows the **tail** (what more you'd type) plus the
  binding's description.
- Entries are sorted alphabetically by tail. ASCII puts `' '` first,
  so any nested-leader binding floats to the top.
- 1–4 columns based on terminal width (`MIN_COL_W = 28`, `" | "`
  separators, picks the largest N that fits).
- Capped at 18 rows; overflow is marked `...`.

## How it works

Driven entirely by two hooks added in v1.3.0:

- `HOOK_KEYBIND_FEED` fires after every keypress that goes through
  the dispatcher. The plugin reads the snapshot (active sequence,
  pending count, full match list) and renders to the message bar
  when `partial == true`.
- `HOOK_KEYBIND_INVOKE` fires just before a binding's callback runs.
  The plugin clears its display so the popup disappears the instant
  a sequence resolves.

No polling, no extra render plumbing, no timers — the message bar
already auto-grows for newlines.

The dispatcher's sequence timeout (`SEQUENCE_TIMEOUT_MS` in
`src/keybinds.c`, default 5 s) decides how long whichkey stays up if
you walk away mid-chord.

## Toggle

```
:whichkey [on|off|toggle]
```

Toggling `off` calls `hook_unregister` for both hooks, so the
dispatcher does literally zero work for whichkey while it's
disabled. Toggling back on re-subscribes.

`<space>th` is bound to `:whichkey toggle` by default.

## Layout

| Terminal width | Columns |
|---|---:|
| ≥ 121 cols | 4 |
| 90–120 | 3 |
| 59–89 | 2 |
| < 59 | 1 |

The cell width is `term_cols / ncols` minus the separator. Tail
column is data-driven (capped at ⅓ of cell). Long descriptions
are truncated, so a verbose binding can't blow out the column
alignment.

## Limitations

- Uses `E.status_msg` (4 KB), so very large keymaps may overflow at
  the highest column counts. The display caps at `MAX_LINES *
  ncols` cells.
- Not a real buffer — no scrolling, no syntax highlighting, no
  selection. If a generic "bottom panel" plugin extension point is
  ever added, whichkey is the obvious candidate to migrate.
- Renders immediately on every keystroke; no configurable delay.
  The chord timeout (5 s) is the only knob.
