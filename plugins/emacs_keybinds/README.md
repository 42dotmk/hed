# emacs_keybinds

Emacs-flavored keymap for hed. Provides standard Emacs muscle memory:
`C-a`/`C-e`/`C-n`/`C-p`/`C-b`/`C-f`/`C-d`/`C-k`/`C-y`/`C-s`, the `C-x`
prefix cluster, and Meta bindings (`M-x`/`M-f`/`M-b`/`M-w`/`M-d`/`M-<`/`M->`).

## Effectively modeless

hed is modal at its core, but this plugin installs a `HOOK_MODE_CHANGE`
that bounces NORMAL → INSERT immediately. The user spends all their time
in INSERT mode (where typing is natural), with COMMAND mode entered
explicitly via `M-x`. VISUAL mode still works for region selection.

## Meta/Alt support

The input layer (`src/editor.c:ed_read_key`) was extended to recognize
`ESC` followed by a printable byte as a Meta-prefixed key. The keybind
subsystem renders these as `<M-x>`, `<M-f>`, etc. so plugins and config
can bind them naturally.

Caveats:

- `M-[` collides with the CSI escape prefix, so terminals never deliver
  it as a Meta key.
- Bare `<Esc>` (no follow-up byte within ~100ms) still works for users
  who want to escape — but with the always-insert hook this is rarely
  useful.

## Conflicts with vim_keybinds

Do not enable both simultaneously. Both want `<C-d>`, `<C-n>`, `<C-p>`,
`<C-u>`, `<C-v>`, `<C-o>` in normal mode and would clobber each other
(last-write-wins).

## Bindings

### Insert / global

| Binding | Action |
|---|---|
| `C-a` / `C-e` | beginning / end of line |
| `C-b` / `C-f` | backward / forward char |
| `C-n` / `C-p` | next / previous line |
| `C-d` | delete char forward |
| `C-k` | kill to end of line |
| `C-y` | yank (paste) |
| `C-s` / `C-r` | search forward / backward |
| `C-g` | cancel |
| `M-f` / `M-b` | forward / backward word (approx — paragraph) |
| `M-<` / `M->` | beginning / end of buffer |
| `M-d` | kill word forward (approx) |
| `M-w` | copy region (when visual is active) |
| `M-x` | enter command mode |

### `C-x` prefix (multi-key)

| Binding | Action |
|---|---|
| `C-x C-s` | save |
| `C-x C-c` | quit |
| `C-x C-f` | find file (fzf) |
| `C-x b` | switch buffer (fzf) |
| `C-x k` | kill buffer |
| `C-x 0` | delete current window |
| `C-x 2` | split below |
| `C-x 3` | split right |
| `C-x o` | other window |
| `C-x u` | undo |

### Visual

| Binding | Action |
|---|---|
| `C-w` | kill region (cut) |
| `C-g` | cancel selection |

## Enable

In `src/config.c`'s `load_plugins()`:

```c
plugin_enable("emacs_keybinds");
// remove or comment out plugin_enable("vim_keybinds")
```
