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
elif command -v wget >/dev/null 2>&1; then
    DL="wget -qO"
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
# tree-sitter (libtree-sitter0) : syntax highlighting
check_dep() {
    # $1 = friendly name, $2 = check command, $3 = apt pkg, $4 = dnf pkg,
    # $5 = pacman pkg, $6 = brew pkg
    local name="$1" check="$2"
    if eval "$check" >/dev/null 2>&1; then
        return 0
    fi
    return 1
}

missing_names=()
missing_apt=()
missing_dnf=()
missing_pacman=()
missing_brew=()

add_missing() {
    missing_names+=("$1")
    missing_apt+=("$2")
    missing_dnf+=("$3")
    missing_pacman+=("$4")
    missing_brew+=("$5")
}

if ! command -v fzf >/dev/null 2>&1; then
    add_missing "fzf" "fzf" "fzf" "fzf" "fzf"
fi
if ! command -v rg >/dev/null 2>&1; then
    add_missing "ripgrep" "ripgrep" "ripgrep" "ripgrep" "ripgrep"
fi
# libtree-sitter: look for the shared library in common locations
if ! ldconfig -p 2>/dev/null | grep -q 'libtree-sitter\.so'; then
    add_missing "libtree-sitter" "libtree-sitter0" "tree-sitter" "tree-sitter" "tree-sitter"
fi

if [ "${#missing_names[@]}" -eq 0 ]; then
    echo "All optional dependencies are present."
else
    echo
    echo "Missing optional dependencies:"
    for n in "${missing_names[@]}"; do echo "  - $n"; done

    PM=""
    PM_INSTALL=""
    if command -v apt-get >/dev/null 2>&1; then
        PM="apt"; PM_INSTALL="sudo apt-get install -y"
        pkgs=("${missing_apt[@]}")
    elif command -v dnf >/dev/null 2>&1; then
        PM="dnf"; PM_INSTALL="sudo dnf install -y"
        pkgs=("${missing_dnf[@]}")
    elif command -v pacman >/dev/null 2>&1; then
        PM="pacman"; PM_INSTALL="sudo pacman -S --needed --noconfirm"
        pkgs=("${missing_pacman[@]}")
    elif command -v brew >/dev/null 2>&1; then
        PM="brew"; PM_INSTALL="brew install"
        pkgs=("${missing_brew[@]}")
    fi

    if [ -z "$PM" ]; then
        echo
        echo "No supported package manager found (apt/dnf/pacman/brew)."
        echo "Install the above manually if you want full functionality."
    else
        echo
        echo "Detected package manager: $PM"
        echo "Would install: ${pkgs[*]}"
        echo "Command: $PM_INSTALL ${pkgs[*]}"
        echo
        printf "Install missing dependencies? [y/N/p(ick)] "
        read -r reply </dev/tty || reply="n"
        case "$reply" in
            y|Y|yes)
                $PM_INSTALL "${pkgs[@]}"
                ;;
            p|P|pick)
                chosen=()
                for i in "${!missing_names[@]}"; do
                    printf "  install %s (%s)? [y/N] " "${missing_names[$i]}" "${pkgs[$i]}"
                    read -r r </dev/tty || r="n"
                    case "$r" in y|Y|yes) chosen+=("${pkgs[$i]}") ;; esac
                done
                if [ "${#chosen[@]}" -gt 0 ]; then
                    $PM_INSTALL "${chosen[@]}"
                else
                    echo "Nothing selected, skipping."
                fi
                ;;
            *)
                echo "Skipping dependency install."
                ;;
        esac
    fi
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
