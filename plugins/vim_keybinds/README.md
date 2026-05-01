# vim_keybinds

The default Vim-style modal keymap. Loaded with `enabled=1` in the
default `src/config.c`, so this is what you get on a fresh install.

## Modes

`NORMAL`, `INSERT`, `VISUAL`, `VISUAL_LINE`, `VISUAL_BLOCK`, `COMMAND`.
`<Esc>` exits to NORMAL from anywhere.

## Motions (NORMAL / VISUAL)

| Key | Motion |
|---|---|
| `h` `j` `k` `l` | Left / down / up / right |
| Arrow keys | Same as `hjkl` |
| `w` `b` `e` | Word forward / backward / end |
| `0` `^` `$` | Line start / first non-blank / line end |
| `gg` `G` | Buffer top / bottom |
| `{` `}` | Previous / next paragraph |
| `<C-u>` `<C-d>` | Half-page up / down |
| `<C-b>` `<C-f>` | Page up / down |
| `%` | Match bracket |
| `n` `N` | Next / previous search match |
| `*` `<C-*>` | Search word under cursor |
| `42G` | Jump to line 42 (numeric prefix + `G`) |
| `5j` | Move down 5 lines (numeric prefix + motion) |

## Operators (NORMAL / VISUAL)

`d` (delete), `c` (change), `y` (yank), `>` `<` (indent / outdent).

Operator + motion or text object:

```
diw      delete inner word
ci(      change inside parens
ya"      yank around double-quoted string
d$       delete to end of line
y2j      yank current + 2 lines below
```

Text objects: `iw` `aw` `i(` `a(` `i[` `a[` `i{` `a{` `i"` `a"` `i'`
`a'` `` i` `` `` a` ``.

## Modes — entering / leaving

| Key | Action |
|---|---|
| `i` `a` `I` `A` | Enter INSERT (at cursor / after / line start / line end) |
| `o` `O` | Open new line below / above |
| `v` | VISUAL |
| `V` | VISUAL_LINE |
| `<C-v>` | VISUAL_BLOCK |
| `:` | COMMAND |
| `/` `?` | Search forward / backward |
| `<Esc>` | Back to NORMAL |

## Editing

| Key | Action |
|---|---|
| `x` `X` | Delete char under / before cursor |
| `r<char>` | Replace one character |
| `D` | Delete to end of line |
| `C` | Change to end of line |
| `Y` | Yank line |
| `p` `P` | Paste after / before |
| `u` `<C-r>` | Undo / redo |
| `.` | Repeat last change |

## Macros & marks

`q<reg>` start recording, `q` to stop, `@<reg>` play, `@@` replay.

## Folds

`za` toggle, `zo` open, `zc` close, `zR` open all, `zM` close all.

## Leader cluster

Space is the leader. The full default leader map lives in
`src/config.c`; highlights:

| Key | Action |
|---|---|
| `<space><space>` | `:fzf` |
| `<space>ff` | `:fzf` |
| `<space>fr` | `:recent` |
| `<space>fc` `<space>fc` | `:c` (command picker) |
| `<space>sd` `<space>sa` `<space>ss` | `:rg` / `:rgword` / `:ssearch` |
| `<space>tt` `<space>tT` | `:tmux_toggle` / `:tmux_kill` |
| `<space>ts` | `:tmux_send_line` |
| `<space>tq` | `:ctoggle` |
| `<space>tk` | `:keymap-toggle` |
| `<space>ws` `<space>wv` | `:split` / `:vsplit` |
| `<space>ww/h/j/k/l` | Focus next / left / down / up / right |
| `<space>jb` `<space>jf` | `:jb` (jump back) / `:jf` (jump forward) |
| `<space>fh` `<space>fj` | `:hfzf` / `:jfzf` |
| `<space>z` | `:scratch` |
| `<space>rr` | `:reload` |

## Notes

- Numeric prefixes work with motions and `G`. `42G` is the canonical
  "jump to line 42."
- `:keybinds` lists every binding currently registered for the
  active mode — useful to see what your config and plugins have done
  to the table.
