# smart_indent

Carries indentation onto new lines, adds a level after filetype-
specific openers, and dedents when a closer starts a line.

## Behavior

- **Carry-over**: pressing `<Enter>` (or opening a line with `o`/`O`)
  copies the previous line's leading whitespace verbatim — a
  tab-indented line yields a tab-indented continuation, a
  space-indented one keeps its spaces.
- **Bracket balance**: the carried indent shifts by the previous
  line's net bracket count, clamped to one level. `x = [`<Enter>
  goes one level deeper; `    2]`<Enter> comes one level back;
  `foo(x);`<Enter> (balanced) carries unchanged. A trailing `:`
  counts as an opener in python and yaml. A closer that *starts* the
  line is not counted (a standalone `}` already sits at the dedented
  level), and `"…"`/`` `…` `` string contents are skipped.
- **Electric pair**: `<Enter>` with the cursor between a pair
  (`{|}`, as auto_pair leaves it) puts the closer on its own line at
  the base indent and leaves the cursor on the indented middle line.
- **Dedent on closers**: typing `}`, `)`, or `]` as the first
  non-whitespace char on a line removes one indent level, vim
  cindent-style.

One level is a tab, or `E.tab_size` spaces when `E.expand_tab` is
set. Bracketed paste never triggers any of this — pasted text lands
verbatim.

## Per-filetype rules

Rules are `(filetype, indent_after, dedent_of)` with `"*"` as the
fallback; registering a filetype again replaces its rule
(last-write-wins). Defaults:

| filetype | indent after | dedent on |
|----------|--------------|-----------|
| `*`      | `{ ( [`      | `} ) ]`   |
| `python` | `{ ( [ :`    | `} ) ]`   |
| `yaml`   | `{ ( [ :`    | `} ) ]`   |

Add your own from `~/.config/hed/config.c`:

```c
#include "smart_indent/smart_indent.h"

void config_user_init(void) {
    smart_indent_register("lua", "{(", "})");
}
```

## Disable

Set `plugin_load(&plugin_smart_indent, …)` to `0` in `src/config.h`
and `:reload`.
