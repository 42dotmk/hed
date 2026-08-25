# completion

VSCode-style autocompletion menu. A non-focus-stealing popup that
tracks the caret — directly below it (above when there's no room),
never covering the line being typed — while normal insert-mode
editing continues in the text window, re-filtering live as you type.

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

## Built-in sources

Two editor-native sources ship inside the plugin
(`completion_sources.c`):

- **words** — identifiers harvested from every open non-readonly
  buffer that share the typed prefix (case-insensitive; kicks in from
  the second character; 3+ character words). The fallback that makes
  completion useful in any buffer, LSP or not.
- **path** — filesystem entries, triggered by `/`. The token before
  the cursor is resolved as absolute, `~/`, or relative to the
  buffer's directory; directories get a trailing `/` and sort first;
  hidden entries appear only when the basename starts with `.`. A
  bare `/` only completes at line start or after a quote/`<` (so
  `a / b` division stays quiet).

Items from different sources with the same insert text are deduped —
the richer one wins (an LSP function beats the same name as a plain
buffer word).

## Custom sources

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
generation guards it. A source may call `completion_provide`
synchronously from `request()`, but must not touch `Buffer` pointers
afterwards (the call can reallocate `E.buffers`). `plugins/lsp` is
the stock external source.

## Interactions

- **copilot**: `HOOK_KEYPRESS` fires before keybinds, so while the
  menu is open Tab accepts the menu item; when it's closed Tab accepts
  copilot ghost text as before.
- **auto_pair**: typing a bracket while the menu is open inserts the
  pair normally and dismisses the menu (non-identifier character).
