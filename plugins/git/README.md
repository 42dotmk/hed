← [hed](../../readme.md)

# git

Launches [lazygit](https://github.com/jesseduffield/lazygit) as a
full-screen TUI from inside hed. The editor temporarily leaves raw
mode (the same dance fzf uses), hands the terminal over to lazygit,
and repaints when it exits.

## Commands

| Command | Action |
|---|---|
| `:git` | Run lazygit in the current working directory |

## Keybind

Bound to `<space>gg` by default in `src/config.c`:

```c
cmapn(" gg", "git", "lazygit");
```

## Requirements

`lazygit` on `$PATH`. If it isn't installed, `:git` reports
`failed to run lazygit` and leaves the editor untouched.

## Disable

Set `plugin_load(&plugin_git, …)` to `0` in `src/config.c` and
`:reload`.
