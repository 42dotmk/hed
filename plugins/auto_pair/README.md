# auto_pair

Auto-inserts the closing half of a pair when you type the opening
half — `(`, `[`, `{`, `"`, `'`, `` ` `` — and steps the cursor over
the close character if you're already sitting on it.

## Behavior

| You type | Result |
|---|---|
| `(` `[` `{` | Inserts pair, cursor between |
| `"` `'` `` ` `` (and you're not on a matching close) | Inserts pair, cursor between |
| `"` `'` `` ` `` (and the next char already matches) | Cursor steps over the existing close |
| `<BS>` on an opener whose close is to the right | Deletes both halves |

Active in INSERT mode only. Policy-free: no special handling for
strings inside comments or escaped characters — if you don't want a
pair, just press Backspace.

## Disable

Set `plugin_load(&plugin_auto_pair, …)` to `0` in `src/config.c` and
`:reload`.
