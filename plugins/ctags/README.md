← [hed](../../readme.md)

# ctags

Goto-definition via a [universal-ctags](https://ctags.io/) `tags`
file. Owns both the `:tag` command and the underlying tag lookup
(previously `src/utils/ctags.c`).

## Commands

| Command | Action |
|---|---|
| `:tag <name>` | Jump to the definition of `<name>` |
| `:tag` | Jump to the definition of the word under the cursor |

After the jump the screen is centered on the target line.

## Keybind

Bound to `gd` by default in `src/config.c`:

```c
cmapn("gd", "tag", "goto tag");
```

## How it works

The plugin shells out to `rg` against a `tags` file in the current
working directory (or any ancestor). The expected format is the
standard `TAG\tFILEPATH\tPATTERN` produced by:

```sh
ctags -R .
```

No tags file → `:tag` reports it and is a no-op. Regenerate
whenever your project layout changes.

## Disable

Set `plugin_load(&plugin_ctags, …)` to `0` in `src/config.c` and
`:reload`. The `gd` cmap will fall through to whatever else owns
the sequence (nothing, by default).
