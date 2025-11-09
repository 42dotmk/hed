# Repository Guidelines

## Project Structure & Module Organization
- `src/` — C sources (one module per file, e.g., `buffer.c`, `editor.c`).
- `include/` — public headers matching sources (e.g., `buffer.h`).
- `include/hed.h` — umbrella header including all module headers.
- `build/` — build artifacts (created by `make`).
- `Makefile`, `compile_flags.txt`, `README.md` — top‑level config and docs.

Tips
- New module: add `src/<name>.c` and `include/<name>.h`, then include it from `include/hed.h` if broadly used. The Makefile auto-picks `src/*.c`.

## Build, Test, and Development Commands
- Build: `make` (uses `clang`, C23, `-Wall -Wextra -pedantic -O2`). Output: `build/hed`.
- Run: `make run` or `./build/hed [files...]`.
- Clean: `make clean` (removes `build/`).
- Flags: edit `compile_flags.txt` for compiler options (e.g., `-Iinclude`).

## Coding Style & Naming Conventions
- Language: C23; compiler: `clang`.
- Indentation: 4 spaces; no tabs; ≤ 100 cols.
- Braces: K&R/1TBS (opening brace on the same line).
- Naming: `lower_snake_case` for functions/vars, `UPPER_SNAKE_CASE` for macros, header guards like `MODULE_H`.
- Includes: system headers first, then project headers. Prefer `#include "hed.h"` in `.c` files.
- Keep modules focused; avoid global state except where already established (e.g., editor state).

## Testing Guidelines
- No formal test suite yet. Manually verify core flows:
  - Open/save (`:w`, `:wq`), navigation (`hjkl`, `gg`, `G`), search (`/`, `n`), buffers (`:e`, `:ls`, `:bn`).
- If adding tests, create `tests/` and a `make test` target; prefer a lightweight C framework (e.g., Unity/CMocka). Name files `test_<area>.c`.

## Commit & Pull Request Guidelines
- Commits: small, focused, imperative style (e.g., `buffer: fix off‑by‑one on delete`).
- Prefer Conventional Commits when helpful (`feat:`, `fix:`, `refactor:`).
- PRs: describe intent, key changes, and manual test steps; link issues; include before/after terminal screenshots when UI changes.

## Security & Configuration Tips
- Terminal safety: ensure raw mode is restored on exit paths.
- File IO: check errors and avoid data loss; write to temp then rename when appropriate.
- Keep dependencies minimal; avoid `system()` and non‑portable calls.
