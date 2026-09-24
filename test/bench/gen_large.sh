#!/bin/sh
# Generate large test files for performance work: big.c and big.md of
# roughly N lines each (default 100000), built by repeating hed's own
# sources so highlighting, folds and injections see realistic code.
#
#   test/bench/gen_large.sh [outdir] [lines]
#
# Then: HED_PROFILE=1 ./build/hed <outdir>/big.c and read the
# "profile:" lines in the log (:log, or ~/.cache/hed/...).
set -e
out=${1:-.}
n=${2:-100000}
here=$(cd "$(dirname "$0")/../.." && pwd)

gen() {
    : >"$2"
    while [ "$(wc -l <"$2")" -lt "$n" ]; do
        cat $1 >>"$2"
    done
    head -n "$n" "$2" >"$2.tmp" && mv "$2.tmp" "$2"
}

gen "$here/src/*.c $here/src/*/*.c" "$out/big.c"
gen "$here/*.md $here/plugins/*/README.md" "$out/big.md"
wc -l "$out/big.c" "$out/big.md"
