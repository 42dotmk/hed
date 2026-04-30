#!/usr/bin/env bash
set -euo pipefail

REPO="42dotmk/hed"
INSTALL_DIR="${HED_INSTALL_DIR:-$HOME/.local/bin}"

OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Linux) ;;
    *) echo "Unsupported OS: $OS (only Linux prebuilt binaries are published)" >&2; exit 1 ;;
esac

case "$ARCH" in
    x86_64|amd64) ARCH="x86_64" ;;
    *) echo "Unsupported architecture: $ARCH" >&2; exit 1 ;;
esac

if command -v curl >/dev/null 2>&1; then
    DL="curl -fsSL -o"
    DL_STDOUT="curl -fsSL"
elif command -v wget >/dev/null 2>&1; then
    DL="wget -qO"
    DL_STDOUT="wget -qO -"
else
    echo "Need curl or wget installed." >&2
    exit 1
fi

mkdir -p "$INSTALL_DIR"

base="https://github.com/${REPO}/releases/latest/download"

for bin in hed tsi; do
    url="${base}/${bin}-linux-${ARCH}"
    dest="${INSTALL_DIR}/${bin}"
    echo "Downloading ${bin} -> ${dest}"
    $DL "${dest}.tmp" "$url"
    chmod +x "${dest}.tmp"
    mv "${dest}.tmp" "${dest}"
done

echo
echo "Installed hed and tsi to ${INSTALL_DIR}"

# --- Optional runtime dependencies ----------------------------------------
# fzf       : fuzzy pickers (:fzf, :recent, :c, history fzf)
# ripgrep   : :rg, :ssearch, :rgword
# (tree-sitter is statically linked into the binary; no system pkg needed.)
#
# We download portable static binaries from upstream releases into
# $INSTALL_DIR — no sudo, no package manager, no version pinning to your
# distro's repos.

# Resolve the latest release tag for a GitHub repo via the public API.
latest_tag() {
    # $1 = owner/repo. Echoes "vX.Y.Z" on success.
    $DL_STDOUT "https://api.github.com/repos/$1/releases/latest" \
        | grep -m1 '"tag_name"' \
        | sed -E 's/.*"tag_name"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/'
}

install_fzf() {
    local tag ver tmp
    tag=$(latest_tag junegunn/fzf) || { echo "Could not resolve fzf release tag." >&2; return 1; }
    ver="${tag#v}"
    tmp=$(mktemp -d)
    trap "rm -rf '$tmp'" RETURN
    echo "Downloading fzf ${tag}"
    $DL "$tmp/fzf.tgz" \
        "https://github.com/junegunn/fzf/releases/download/${tag}/fzf-${ver}-linux_amd64.tar.gz"
    tar -xzf "$tmp/fzf.tgz" -C "$tmp" fzf
    chmod +x "$tmp/fzf"
    mv "$tmp/fzf" "$INSTALL_DIR/fzf"
    echo "  -> $INSTALL_DIR/fzf"
}

install_ripgrep() {
    local tag tmp inner
    tag=$(latest_tag BurntSushi/ripgrep) || { echo "Could not resolve ripgrep release tag." >&2; return 1; }
    tmp=$(mktemp -d)
    trap "rm -rf '$tmp'" RETURN
    inner="ripgrep-${tag}-x86_64-unknown-linux-musl"
    echo "Downloading ripgrep ${tag}"
    $DL "$tmp/rg.tgz" \
        "https://github.com/BurntSushi/ripgrep/releases/download/${tag}/${inner}.tar.gz"
    tar -xzf "$tmp/rg.tgz" -C "$tmp"
    chmod +x "$tmp/${inner}/rg"
    mv "$tmp/${inner}/rg" "$INSTALL_DIR/rg"
    echo "  -> $INSTALL_DIR/rg"
}

missing_names=()
missing_installers=()

if ! command -v fzf >/dev/null 2>&1; then
    missing_names+=("fzf")
    missing_installers+=("install_fzf")
fi
if ! command -v rg >/dev/null 2>&1; then
    missing_names+=("ripgrep")
    missing_installers+=("install_ripgrep")
fi

if [ "${#missing_names[@]}" -eq 0 ]; then
    echo "All optional dependencies are present."
else
    echo
    echo "Missing optional dependencies (will be downloaded as portable static binaries to $INSTALL_DIR):"
    for n in "${missing_names[@]}"; do echo "  - $n"; done
    echo
    printf "Install them? [Y/n/p(ick)] "
    read -r reply </dev/tty || reply="y"
    case "$reply" in
        n|N|no)
            echo "Skipping dependency install."
            ;;
        p|P|pick)
            for i in "${!missing_names[@]}"; do
                printf "  install %s? [y/N] " "${missing_names[$i]}"
                read -r r </dev/tty || r="n"
                case "$r" in y|Y|yes) "${missing_installers[$i]}" ;; esac
            done
            ;;
        *)
            for fn in "${missing_installers[@]}"; do
                "$fn"
            done
            ;;
    esac
fi

# --- Tree-sitter grammars (optional) --------------------------------------
# tsi clones tree-sitter-<lang>, builds <lang>.so, and installs it to
# ~/.config/hed/ts/. Needs git + cc on the host.

LANG_CHOICES=(c cpp python javascript typescript rust go html css json bash lua ruby java markdown)

if command -v git >/dev/null 2>&1 && command -v cc >/dev/null 2>&1; then
    echo
    echo "Install tree-sitter grammars now? (used for syntax highlighting)"
    echo "Available:"
    for i in "${!LANG_CHOICES[@]}"; do
        printf "  %2d) %s\n" "$((i+1))" "${LANG_CHOICES[$i]}"
    done
    echo
    echo "Enter numbers separated by spaces (e.g. '1 3 7'), 'a' for all,"
    echo "or just press Enter to skip. You can always run 'tsi <lang>' later."
    printf "> "
    read -r selection </dev/tty || selection=""

    chosen_langs=()
    case "$selection" in
        ""|n|N|no|skip)
            ;;
        a|A|all)
            chosen_langs=("${LANG_CHOICES[@]}")
            ;;
        *)
            for n in $selection; do
                if [[ "$n" =~ ^[0-9]+$ ]] && [ "$n" -ge 1 ] && [ "$n" -le "${#LANG_CHOICES[@]}" ]; then
                    chosen_langs+=("${LANG_CHOICES[$((n-1))]}")
                else
                    echo "  ignoring invalid choice: $n"
                fi
            done
            ;;
    esac

    if [ "${#chosen_langs[@]}" -gt 0 ]; then
        echo "Installing grammars: ${chosen_langs[*]}"
        ts_workdir=$(mktemp -d)
        # tsi builds in $cwd/ts/build/<lang>, then installs to ~/.config/hed/ts.
        ( cd "$ts_workdir" && for lang in "${chosen_langs[@]}"; do
              echo
              echo "=== $lang ==="
              "$INSTALL_DIR/tsi" "$lang" || echo "  (failed to install $lang, continuing)"
          done )
        rm -rf "$ts_workdir"
    fi
else
    echo
    echo "Note: 'git' and/or 'cc' not found — skipping grammar install offer."
    echo "Install them and run 'tsi <lang>' later (e.g. 'tsi python')."
fi

case ":$PATH:" in
    *":${INSTALL_DIR}:"*) ;;
    *)
        echo
        echo "Note: ${INSTALL_DIR} is not on your PATH."
        echo "Add this to your shell rc (~/.bashrc, ~/.zshrc):"
        echo "    export PATH=\"${INSTALL_DIR}:\$PATH\""
        ;;
esac
