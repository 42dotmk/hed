# Packaging

Distro packaging recipes for `hed`. They all build on the FHS-aware
`install` target in the top-level `makefile`:

```sh
make PREFIX=/usr MAN_DIR=/usr/share/man
make install PREFIX=/usr MAN_DIR=/usr/share/man DESTDIR=/path/to/pkgroot
```

`PREFIX`/`DESTDIR` follow the usual GNU convention. `MAN_DIR` is baked into
the binary at build time so in-editor `:man` finds the pages the package
installs — keep it equal to `$(MANDIR)` (default `$(PREFIX)/share/man`).

What lands where:

| File | Path |
|------|------|
| `hed`, `tsi` binaries | `$(PREFIX)/bin/` |
| man pages (`hed.1`, `hed-*.1`) | `$(PREFIX)/share/man/man1/` |
| `LICENSE` | `$(PREFIX)/share/doc/hed/copyright` |

Only **libc** is a hard runtime dependency: the tree-sitter runtime is
statically linked, and every external tool (`fzf`, `ripgrep`, `tmux`,
`git`, `ctags`, formatters) is probed at runtime, so they belong in
`Recommends`/`optdepends`/`Suggests`, never hard deps.

## deb / rpm

Built in CI by [`nfpm`](https://nfpm.goreleaser.com) from `nfpm.yaml` (repo
root) and attached to each GitHub Release on tag push. To build locally:

```sh
make clean && make MAN_DIR=/usr/share/man
cp build/hed hed-linux-x86_64 && cp build/tsi tsi-linux-x86_64
VERSION=1.18.1 nfpm package -f nfpm.yaml -p deb -t .
VERSION=1.18.1 nfpm package -f nfpm.yaml -p rpm -t .
```

## Arch (`arch/PKGBUILD`)

Source package for the AUR. Builds from the tagged release and vendors the
tree-sitter submodule from a local clone. Bump by editing `pkgver`.

```sh
cd packaging/arch && makepkg -si
```

## Void (`void/template`)

Copy to `srcpkgs/hed/template` in a `void-packages` checkout, fill the
checksum placeholders with `xgensum -i srcpkgs/hed/template`, then
`./xbps-src pkg hed`. It pulls tree-sitter as a second distfile (the GitHub
source tarball omits submodules).

## The tree-sitter submodule

Source builds need `vendor/tree-sitter` (pinned in `.gitmodules`; currently
tree-sitter `v0.26.3`). The Arch recipe uses git submodules; the Void
template fetches it as a separate distfile. Update both when bumping the
submodule.
