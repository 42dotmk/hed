#!/bin/sh
# gen-man.sh — generate roff man pages from the project's markdown docs.
#
#   readme.md                 -> man/man1/hed.1
#   plugins/<name>/README.md  -> man/man1/hed-<name>.1
#
# Requires pandoc. Each page gets a synthetic NAME section (so `man`,
# `whatis` and `apropos` work), the inter-doc nav line is stripped, and
# headings are promoted one level so the markdown `##` sections become
# top-level `.SH` man sections.
#
# Usage: scripts/gen-man.sh [output_dir]   (default: man/man1)

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT_DIR=${1:-"$ROOT/man/man1"}
SECTION=1
DATE=$(date +%Y-%m-%d 2>/dev/null || echo "")
VERSION=$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo dev)

if ! command -v pandoc >/dev/null 2>&1; then
    echo "gen-man: pandoc not found (install pandoc to generate man pages)" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# Pull the first paragraph after the H1 as the page description (one line).
extract_desc() {
    awk '
        /^```/        { in_code = !in_code; next }
        in_code       { next }
        /readme\.md\)/ { next }                 # skip the nav line
        /^#/ && !seen_h1 { seen_h1 = 1; next }  # consume the H1
        !seen_h1      { next }
        /^[[:space:]]*$/ { if (started) exit; else next }
        { started = 1; printf "%s ", $0 }
    ' "$1" |
    sed -E 's/\[([^]]*)\]\([^)]*\)/\1/g; s/[*`_]//g; s/[[:space:]]+/ /g; s/^ //; s/ $//'
}

# Strip nav line + H1, promote every heading one level (## -> #), leaving
# fenced code blocks untouched.
transform_body() {
    awk '
        /^```/        { in_code = !in_code; print; next }
        in_code       { print; next }
        /readme\.md\)/ { next }                 # nav line
        /^#/ && !seen_h1 { seen_h1 = 1; next }  # drop the original H1
        /^#/          { sub(/^#/, ""); print; next }  # promote heading
        { print }
    ' "$1"
}

gen_one() {
    src=$1
    name=$2
    out="$OUT_DIR/$name.$SECTION"
    desc=$(extract_desc "$src")
    [ -n "$desc" ] || desc="part of the hed editor"
    upper=$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]')

    tmp=$(mktemp)
    {
        printf '# NAME\n\n%s - %s\n\n' "$name" "$desc"
        transform_body "$src"
    } > "$tmp"

    pandoc "$tmp" -f markdown -s -t man \
        -V title="$upper" \
        -V section="$SECTION" \
        -V date="$DATE" \
        -V header="hed manual" \
        -V footer="hed $VERSION" \
        -o "$out"
    rm -f "$tmp"
    echo "  $out"
}

echo "Generating man pages into $OUT_DIR ..."
gen_one "$ROOT/readme.md" "hed"

for readme in "$ROOT"/plugins/*/README.md; do
    [ -f "$readme" ] || continue
    plugin=$(basename "$(dirname "$readme")")
    gen_one "$readme" "hed-$plugin"
done

echo "Done. Preview with:  man $OUT_DIR/hed.1"
