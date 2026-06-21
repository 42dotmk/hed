# emacs_keybinds

Emacs-flavored keymap. Modeless — the editor stays in INSERT
permanently; `<Esc>` is a no-op. NORMAL mode is unreachable while
this keymap is active.

Not loaded by default. Switch to it at runtime with `:keymap emacs`,
or pre-load it in `src/config.c` by setting `plugin_load(&plugin_emacs_keybinds, 1);`.

## Motion

| Key | Action |
|---|---|
| `C-a` `C-e` | Beginning / end of line |
| `C-b` `C-f` | Backward / forward char |
| `C-n` `C-p` | Next / previous line |
| `M-b` `M-f` | Backward / forward word |
| `M-<` `M->` | Buffer top / bottom |
| Arrow keys | Cursor motion (works everywhere) |

## Editing

| Key | Action |
|---|---|
| `C-d` | Delete char forward |
| `<BS>` | Delete char backward |
| `C-k` | Kill to end of line |
| `M-d` | Kill word forward |
| `M-w` | Copy region (no kill) |
| `C-w` | Kill region |
| `C-y` | Yank (paste from kill ring / register) |
| `C-/` `C-_` | Undo |

## Selection

| Key | Action |
|---|---|
| `S-Up/Down/Left/Right` | Extend selection |
| `C-S-Left` `C-S-Right` | Extend by word |
| `S-Home` `S-End` | Extend to beginning / end of line |
| `C-Space` | Set mark (start visual selection) |

## C-x cluster (file & buffer ops)

| Key | Action |
|---|---|
| `C-x C-s` | Save (`:w`) |
| `C-x C-c` | Quit (`:q`) |
| `C-x C-f` | Open file picker (`:fzf`) |
| `C-x b` | Switch buffer |
| `C-x k` | Kill buffer |
| `C-x 0` | Close current window |
| `C-x 2` | Horizontal split |
| `C-x 3` | Vertical split |
| `C-x o` | Other window (focus next) |
| `C-x u` | Undo |

## Command palette

`M-x` — opens hed's command picker (same as `:c`).

## Notes

The plugin sets `ed_set_modeless(1)` on init so every `:` command
that would normally drop you into NORMAL mode keeps you in INSERT.

If you want a hybrid (Emacs keys but mode-aware), don't set modeless:
edit `src/config.c` to call `ed_set_modeless(0)` after the plugin
loads.
