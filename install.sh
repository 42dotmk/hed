#!/usr/bin/env bash
#
# hed installer
#
# Two install modes:
#   1. binary  — download prebuilt hed/tsi from the latest GitHub release
#   2. source  — clone the repo and build with make; symlink into ~/.local/bin
#
# Optional follow-ups:
#   - portable static fzf and ripgrep into the same bin dir
#   - tree-sitter grammars via the bundled tsi tool
#
# Everything lands under ~/.local/. No sudo. No package manager.

set -euo pipefail

REPO="42dotmk/hed"
INSTALL_DIR="${HED_INSTALL_DIR:-$HOME/.local/bin}"
SOURCE_DIR="${HED_SOURCE_DIR:-$HOME/.local/share/hed}"

OS_RAW="$(uname -s)"
ARCH_RAW="$(uname -m)"

case "$OS_RAW" in
    Linux)  OS_TAG="linux"  ;;
    Darwin) OS_TAG="darwin" ;;
    *) echo "Unsupported OS: $OS_RAW (Linux and macOS supported)" >&2; exit 1 ;;
esac

# Normalise the architecture for the various asset URLs further down.
# - HED_ARCH       : tag used by the hed release assets (x86_64 / arm64)
# - FZF_ARCH       : fzf's tarball convention (amd64 / arm64)
# - RG_TRIPLE_ARCH : ripgrep's target-triple prefix (x86_64 / aarch64)
case "$ARCH_RAW" in
    x86_64|amd64)
        HED_ARCH="x86_64"; FZF_ARCH="amd64"; RG_TRIPLE_ARCH="x86_64" ;;
    arm64|aarch64)
        HED_ARCH="arm64";  FZF_ARCH="arm64"; RG_TRIPLE_ARCH="aarch64" ;;
    *) echo "Unsupported architecture: $ARCH_RAW" >&2; exit 1 ;;
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

# --- Pick install mode ----------------------------------------------------

MODE="${HED_INSTALL_MODE:-}"
if [ -z "$MODE" ]; then
    # Default to source on platforms where prebuilt binaries may not
    # be published yet. Linux/x86_64 has been the canonical release
    # target; everything else falls back to source.
    if [ "$OS_TAG" = "linux" ] && [ "$HED_ARCH" = "x86_64" ]; then
        DEFAULT_MODE="binary"; DEFAULT_CHOICE=1
    else
        DEFAULT_MODE="source"; DEFAULT_CHOICE=2
    fi
    echo
    echo "Install hed from:"
    echo "  1) binary  — prebuilt ${OS_TAG}-${HED_ARCH} release (fastest, if published)"
    echo "  2) source  — clone repo to $SOURCE_DIR, build with make"
    echo
    printf "Choice [1/2] (default: %d): " "$DEFAULT_CHOICE"
    read -r reply </dev/tty || reply=""
    case "$reply" in
        1|b|bin|binary) MODE="binary" ;;
        2|s|src|source) MODE="source" ;;
        *)              MODE="$DEFAULT_MODE" ;;
    esac
fi

# --- Binary install --------------------------------------------------------

install_binary() {
    local base="https://github.com/${REPO}/releases/latest/download"
    for bin in hed tsi; do
        local url="${base}/${bin}-${OS_TAG}-${HED_ARCH}"
        local dest="${INSTALL_DIR}/${bin}"
        echo "Downloading ${bin} -> ${dest}"
        if ! $DL "${dest}.tmp" "$url"; then
            echo "Failed to download ${bin} for ${OS_TAG}-${HED_ARCH}." >&2
            echo "Release may not publish a binary for this platform yet —" >&2
            echo "rerun and pick (2) source, or set HED_INSTALL_MODE=source." >&2
            rm -f "${dest}.tmp"
            exit 1
        fi
        chmod +x "${dest}.tmp"
        mv "${dest}.tmp" "${dest}"
    done
    echo
    echo "Installed hed and tsi to ${INSTALL_DIR}"
}

# --- Source install --------------------------------------------------------

install_source() {
    if ! command -v git >/dev/null 2>&1; then
        echo "Source install requires git on PATH." >&2
        exit 1
    fi
    if ! command -v make >/dev/null 2>&1; then
        echo "Source install requires make on PATH." >&2
        exit 1
    fi
    if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1; then
        echo "Source install requires a C compiler (cc or gcc)." >&2
        exit 1
    fi

    if [ -d "$SOURCE_DIR/.git" ]; then
        echo "Updating existing checkout at $SOURCE_DIR"
        git -C "$SOURCE_DIR" fetch --tags --recurse-submodules
        git -C "$SOURCE_DIR" pull --recurse-submodules --ff-only || {
            echo "Could not fast-forward $SOURCE_DIR — please update manually." >&2
            exit 1
        }
    else
        echo "Cloning $REPO into $SOURCE_DIR"
        mkdir -p "$(dirname "$SOURCE_DIR")"
        git clone --recursive "https://github.com/${REPO}.git" "$SOURCE_DIR"
    fi

    echo "Building..."
    make -C "$SOURCE_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

    echo "Symlinking binaries into $INSTALL_DIR"
    ln -sf "$SOURCE_DIR/build/hed" "$INSTALL_DIR/hed"
    ln -sf "$SOURCE_DIR/build/tsi" "$INSTALL_DIR/tsi"

    echo
    echo "Installed hed (source) to $SOURCE_DIR, symlinked into $INSTALL_DIR"
    echo "Update later with: cd $SOURCE_DIR && git pull --recurse-submodules && make"
}

case "$MODE" in
    binary) install_binary ;;
    source) install_source ;;
esac

# --- Optional runtime dependencies ----------------------------------------
# fzf       : fuzzy pickers (:fzf, :recent, :c, history fzf)
# ripgrep   : :rg, :ssearch, :rgword
# (tree-sitter is statically linked into the binary; no system pkg needed.)

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
        "https://github.com/junegunn/fzf/releases/download/${tag}/fzf-${ver}-${OS_TAG}_${FZF_ARCH}.tar.gz"
    tar -xzf "$tmp/fzf.tgz" -C "$tmp" fzf
    chmod +x "$tmp/fzf"
    mv "$tmp/fzf" "$INSTALL_DIR/fzf"
    echo "  -> $INSTALL_DIR/fzf"
}

install_ripgrep() {
    local tag tmp inner triple
    tag=$(latest_tag BurntSushi/ripgrep) || { echo "Could not resolve ripgrep release tag." >&2; return 1; }
    tmp=$(mktemp -d)
    trap "rm -rf '$tmp'" RETURN
    case "$OS_TAG" in
        linux)  triple="${RG_TRIPLE_ARCH}-unknown-linux-musl" ;;
        darwin) triple="${RG_TRIPLE_ARCH}-apple-darwin" ;;
    esac
    inner="ripgrep-${tag}-${triple}"
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

# --- PATH check ------------------------------------------------------------
case ":$PATH:" in
    *":${INSTALL_DIR}:"*) ;;
    *)
        echo
        echo "Note: ${INSTALL_DIR} is not on your PATH."
        echo "Add this to your shell rc (~/.bashrc, ~/.zshrc):"
        echo "    export PATH=\"${INSTALL_DIR}:\$PATH\""
        ;;
esac
