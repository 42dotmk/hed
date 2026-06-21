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
cd packaging/arch && makepkg -si        # build + install locally
```

`arch/.SRCINFO` is the committed metadata. Publishing to the AUR is a push
to your AUR git remote (needs an AUR account with your SSH key registered):

```sh
# one-time: clone the AUR repo (empty until first push)
git clone ssh://aur@aur.archlinux.org/hed.git aur-hed
cp packaging/arch/PKGBUILD packaging/arch/.SRCINFO aur-hed/
cd aur-hed
# after any PKGBUILD edit, regenerate metadata on an Arch box:
#   makepkg --printsrcinfo > .SRCINFO
git add PKGBUILD .SRCINFO
git commit -m "hed 1.18.1"
git push                                 # authenticated as you, via SSH
```

## Void (`void/template`)

Copy to `srcpkgs/hed/template` in a `void-packages` checkout, fill the
checksum placeholders with `xgensum -i srcpkgs/hed/template`, then
`./xbps-src pkg hed`. It pulls tree-sitter as a second distfile (the GitHub
source tarball omits submodules).

## Hosted APT / YUM repo (GitHub Pages)

`.github/workflows/pages-repo.yml` publishes the release's `.deb`/`.rpm` into
an APT + YUM repo served from GitHub Pages, so users can `apt`/`dnf install
hed`. Setup, once:

1. **Settings → Pages → Source: GitHub Actions.**
2. Add repo secrets `GPG_PRIVATE_KEY` (ASCII-armored) and `GPG_PASSPHRASE`
   to sign the metadata. Without them the repo publishes **unsigned** (usable
   with `[trusted=yes]` / `gpgcheck=0`).

It then runs on every published release. Install instructions render at the
Pages URL (`packaging/repo-index.html`).

The repo is signed with a dedicated key (not a personal identity); its
public half is committed at `packaging/hed-signing-key.asc` and also served
from Pages as `hed-archive-keyring.asc`. Fingerprint:

```
405E 3235 4302 2A4A 91C6  8737 95C6 0021 B3C8 B4E1
```

The matching private key + passphrase live in the repo secrets
`GPG_PRIVATE_KEY` / `GPG_PASSPHRASE`.

## Homebrew tap

Lives in its own repo, [`42dotmk/homebrew-hed`](https://github.com/42dotmk/homebrew-hed):

```sh
brew tap 42dotmk/hed && brew install hed
```

Ships the prebuilt linux/x86_64 binary; bump `version` + the two `sha256`
values in `Formula/hed.rb` when a new tag ships.

## The tree-sitter submodule

Source builds need `vendor/tree-sitter` (pinned in `.gitmodules`; currently
tree-sitter `v0.26.3`). The Arch recipe uses git submodules; the Void
template fetches it as a separate distfile. Update both when bumping the
submodule.
