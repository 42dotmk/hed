← [hed](../../readme.md)

# pickers

fzf-driven views over editor state. Pure UI on top of
`src/utils/history.c` and `src/utils/jump_list.c` — no core editing
primitives live here.

## Commands

| Command | Action |
|---|---|
| `:hfzf` | Fuzzy-pick a line from the `:` command history; selection prefills the prompt and stays open for editing |
| `:jfzf` | Fuzzy-pick a destination from the jump list; selection opens the file and positions the cursor |

`:jfzf` previews entries with `bat` if available, falling back to
`awk` for a 30-line window around the target.

## Keybinds

Default leader bindings in `src/config.c`:

```c
cmapn(" fh", "hfzf", "history fzf");
cmapn(" fj", "jfzf", "jump-list fzf");
```

## Requirements

`fzf` on `$PATH`. `bat` is optional and only affects the `:jfzf`
preview.

## Disable

Set `plugin_load(&plugin_pickers, …)` to `0` in `src/config.c` and
`:reload`.
