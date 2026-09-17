# treesitter

Syntax highlighting via tree-sitter. Grammars are `.so` files loaded
on demand with `dlopen` — the editor binary doesn't bundle any
particular language; you pick what you want.

## Commands

| Command | Action |
|---|---|
| `:ts on` `:ts off` `:ts auto` | Enable / disable / auto-detect highlighting per buffer |
| `:tslang [name]` | Force a language for the current buffer; with no arg, pick from the installed grammars (bound to `<space>ft`) |
| `:tsi <name>` | Install a tree-sitter grammar |

Auto-detection uses the buffer's filetype (file extension or
shebang). When it fires, the plugin tries to `dlopen` a grammar
called `tree_sitter_<name>` from the configured grammar directory.

## Where grammars live

```
~/.config/hed/ts/<lang>.so
```

This is what `tsi` writes to. The library is the compiled grammar
for `tree-sitter-<lang>` (e.g., `tree-sitter-c`,
`tree-sitter-python`).

## Installing a grammar

The bundled `tsi` helper clones the upstream `tree-sitter-<lang>`
repository, builds the parser as a shared library, and drops the
result into `~/.config/hed/ts/`.

From the shell:

```bash
tsi python
tsi c
tsi rust
```

Or from inside hed:

```
:tsi python
```

After `:tsi`, run `:tslang python` (or just open a Python file with
`:ts auto`) to start highlighting.

`tsi` needs `git`, a C compiler (`cc`), and network access. The
shared library is built with the same C runtime as hed, so the
grammar runs against the statically linked tree-sitter runtime
shipped in the editor binary.

## Highlight queries

Colour decisions come from a `highlights.scm` per language. Lookup
order, first hit wins:

1. `~/.config/hed/ts/queries.local/<lang>/` — your handwritten override
2. a query registered by a plugin (`ts_register_query`, e.g. markdown)
3. [`queries/<lang>/`](../../queries/) in this repository — enhanced
   defaults shipped in-tree (c-sharp so far)
4. `~/.config/hed/ts/queries/<lang>/` — the upstream grammar's own
   queries, copied by `tsi`
5. `./queries/<lang>/` relative to the cwd (development)

Queries are written nvim-treesitter style: on the same node a later
pattern wins, an inner node wins over the outer one enclosing it, and
the `#eq?` / `#not-eq?` / `#match?` / `#not-match?` / `#any-of?`
predicates are evaluated (`#match?` regexes are translated from Rust
syntax to POSIX ERE — `\d`, `\w`, `\s` are rewritten).

## Notes

- The vendored tree-sitter runtime is statically linked into hed —
  no `libtree-sitter` system package needed.
- A grammar `.so` only works against a tree-sitter runtime version
  it was compiled for. If `:ts auto` reports a load failure after a
  `tsi`, the grammar's tree-sitter version drifted from the one
  bundled in this build — `:tsi <lang>` again to recompile against
  the current runtime.
- Build hed without highlighting at all: `make WITH_TREESITTER=0`.
  The editor compiles cleanly without the plugin or the runtime.
