# quickfix_preview

Live-previews the quickfix entry under the cursor. When the quickfix buffer
is focused and you move the cursor with `j`/`k`, this plugin updates
`E.qf.sel` and triggers `qf_preview_selected()` so the target window scrolls
to the matching file:line — without leaving the quickfix pane.

## How it works

Registers a single `HOOK_CURSOR_MOVE` hook scoped to the `quickfix` filetype.
On every cursor move it clamps the row to the quickfix items range, updates
the selection, and re-renders the preview.

## Enable

In `src/config.c`'s `user_hooks_init()`:

```c
plugin_enable("quickfix_preview");
```
