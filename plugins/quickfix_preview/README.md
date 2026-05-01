# quickfix_preview

When the cursor moves inside a quickfix buffer, this plugin syncs the
preview window to the entry under the cursor — file, line, and a few
lines of surrounding context.

## Behavior

The plugin registers a `HOOK_CURSOR_MOVE` handler that fires only
when the focused buffer is the quickfix list (`[Quickfix]`). When the
quickfix line under the cursor changes, it:

1. Reads the file referenced by that entry.
2. Centers the preview on the target line.
3. Highlights the matched line.

If the preview window is closed (or no second window exists), the
plugin does nothing.

## Notes

No commands, no keybinds — pure passive hook. Quickfix itself is
provided by `core` (`:copen`, `:cclose`, `:ctoggle`, `:cnext`,
`:cprev`, `:cadd`, `:cclear`).

The quickfix buffer is created by `core` whenever a quickfix-
producing command finishes (e.g., `:rg <pattern>`). This plugin only
adds the live preview behavior.
