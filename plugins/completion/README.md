# completion

VSCode-style autocompletion menu. A non-focus-stealing popup anchored
under the word being typed: normal insert-mode editing continues in
the text window while the menu floats above it, re-filtering live as
you type.

## Keys

| Key | Action |
|---|---|
| (typing) | auto-triggers after a short idle (150 ms) on identifier or source trigger characters; keeps filtering while open |
| `Ctrl-Space` / `:complete` | open the menu immediately (toggles it closed) |
| `Down` / `C-n` | next item |
| `Up` / `C-p` / `Shift-Tab` | previous item |
| `Tab` / `Enter` | accept the selected item |
| `Esc` | dismiss — stays in insert mode |

Anything else falls through to normal editing. Moving off the anchor
line, deleting past the word start, switching buffers or leaving
insert mode dismisses the menu.

Filtering ranks prefix matches over case-insensitive prefixes over
subsequence matches, then sorts by the source's `sort_text`. The
accept edit replaces the item's range (`textEdit`-style if the source
provided one, else the typed word) through the undo-recorded edit
path, so the whole insert session — typed text plus completion — is
one undo step.

## Appearance

Rows are `label  kind  detail` columns; the selected row gets a
background bar. Colors default to built-in SGRs and can be themed via
the runtime palette keys `menu.sel`, `menu.kind`, `menu.detail`
(`theme_palette_set`, weak — works only with the treesitter theme
registry present).

## Sources

The menu is source-agnostic. A provider registers a
`CompletionSource` (static lifetime) and delivers items
asynchronously:

```c
#include "completion/completion.h"

static int  my_available(Buffer *buf) { return 1; }
static void my_request(Buffer *buf, int line, int col, unsigned token) {
    CmpItem *items = calloc(1, sizeof(CmpItem));
    items[0].label = strdup("hello");
    items[0].kind = CMP_KIND_TEXT;
    items[0].edit_start = items[0].edit_end = -1;
    completion_provide(token, items, 1); /* menu takes ownership */
}
static const CompletionSource my_source = {
    .name = "mine", .available = my_available, .request = my_request,
};
/* in your plugin's init: */
completion_source_register(&my_source);
```

A response arriving after the menu was dismissed (or after further
typing invalidated the request) is dropped and freed — the `token`
generation guards it. `plugins/lsp` is the stock source.

## Interactions

- **copilot**: `HOOK_KEYPRESS` fires before keybinds, so while the
  menu is open Tab accepts the menu item; when it's closed Tab accepts
  copilot ghost text as before.
- **auto_pair**: typing a bracket while the menu is open inserts the
  pair normally and dismisses the menu (non-identifier character).
