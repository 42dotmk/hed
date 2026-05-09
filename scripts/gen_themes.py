#!/usr/bin/env python3
"""Generate plugins/hed_themes/themes.c from a curated palette catalogue.

Each theme is a mapping  hed-palette-role -> "#rrggbb"  for the keys hed's
highlight system understands (see plugins/treesitter/ts_impl.c
ts_seed_default_theme for the full list).

Operation:
  ./scripts/gen_themes.py            # write themes.c from baked-in palettes
  ./scripts/gen_themes.py --fetch    # try to refresh palettes from upstream
                                     # Lua sources first; fall back per theme
                                     # if the network or parser fails

Adding a theme: edit THEMES below. Optionally fill in UPSTREAM[<name>] with
a fetcher that downloads the theme's Lua palette and returns a {role: hex}
dict. Browse https://dotfyle.com/neovim/colorscheme/top for ideas.

The generated themes.c is committed to the repo so the build never depends
on this script having been run.
"""

from __future__ import annotations

import argparse
import re
import sys
import urllib.request
from pathlib import Path
from typing import Callable

# ---------------------------------------------------------------------------
# Curated palettes (offline default).
# These are hand-derived from each theme's documented palette and serve as
# the baseline emitted into themes.c. --fetch may override them at run time.
# Roles match the palette tokens seeded by ts_seed_default_theme.
# ---------------------------------------------------------------------------

THEMES: dict[str, dict[str, str]] = {
    "tokyo-night": {
        "string":     "#9ece6a",
        "comment":    "#565f89",
        "variable":   "#c0caf5",
        "constant":   "#e0af68",
        "number":     "#ff9e64",
        "keyword":    "#bb9af7",
        "type":       "#2ac3de",
        "function":   "#7aa2f7",
        "attribute":  "#bb9af7",
        "label":      "#e06c75",
        "operator":   "#9099ae",
        "punctuation": "#414868",
        "title":      "#c0caf5",
        "uri":        "#7aa2f7",
        "diag.error": "#f7768e",
        "diag.warn":  "#e0af68",
        "diag.note":  "#7aa2f7",
    },
    "gruvbox": {
        "string":     "#b8bb26",
        "comment":    "#928374",
        "variable":   "#ebdbb2",
        "constant":   "#d3869b",
        "number":     "#d3869b",
        "keyword":    "#fb4934",
        "type":       "#fabd2f",
        "function":   "#b8bb26",
        "attribute":  "#8ec07c",
        "label":      "#fe8019",
        "operator":   "#a89984",
        "punctuation": "#7c6f64",
        "title":      "#fb4934",
        "uri":        "#83a598",
        "diag.error": "#fb4934",
        "diag.warn":  "#fabd2f",
        "diag.note":  "#83a598",
    },
    "dracula": {
        "string":     "#f1fa8c",
        "comment":    "#6272a4",
        "variable":   "#f8f8f2",
        "constant":   "#bd93f9",
        "number":     "#bd93f9",
        "keyword":    "#ff79c6",
        "type":       "#8be9fd",
        "function":   "#50fa7b",
        "attribute":  "#ff79c6",
        "label":      "#ffb86c",
        "operator":   "#ff79c6",
        "punctuation": "#6272a4",
        "title":      "#f8f8f2",
        "uri":        "#8be9fd",
        "diag.error": "#ff5555",
        "diag.warn":  "#ffb86c",
        "diag.note":  "#8be9fd",
    },
    "catppuccin-mocha": {
        "string":     "#a6e3a1",
        "comment":    "#6c7086",
        "variable":   "#cdd6f4",
        "constant":   "#fab387",
        "number":     "#fab387",
        "keyword":    "#cba6f7",
        "type":       "#f9e2af",
        "function":   "#89b4fa",
        "attribute":  "#cba6f7",
        "label":      "#f38ba8",
        "operator":   "#94e2d5",
        "punctuation": "#6c7086",
        "title":      "#cdd6f4",
        "uri":        "#89b4fa",
        "diag.error": "#f38ba8",
        "diag.warn":  "#f9e2af",
        "diag.note":  "#89dceb",
    },
    "nord": {
        "string":     "#a3be8c",
        "comment":    "#4c566a",
        "variable":   "#d8dee9",
        "constant":   "#b48ead",
        "number":     "#b48ead",
        "keyword":    "#81a1c1",
        "type":       "#8fbcbb",
        "function":   "#88c0d0",
        "attribute":  "#81a1c1",
        "label":      "#bf616a",
        "operator":   "#d8dee9",
        "punctuation": "#4c566a",
        "title":      "#eceff4",
        "uri":        "#88c0d0",
        "diag.error": "#bf616a",
        "diag.warn":  "#ebcb8b",
        "diag.note":  "#88c0d0",
    },
    "rose-pine": {
        "string":     "#f6c177",
        "comment":    "#6e6a86",
        "variable":   "#e0def4",
        "constant":   "#ebbcba",
        "number":     "#ebbcba",
        "keyword":    "#31748f",
        "type":       "#c4a7e7",
        "function":   "#9ccfd8",
        "attribute":  "#c4a7e7",
        "label":      "#eb6f92",
        "operator":   "#908caa",
        "punctuation": "#524f67",
        "title":      "#e0def4",
        "uri":        "#9ccfd8",
        "diag.error": "#eb6f92",
        "diag.warn":  "#f6c177",
        "diag.note":  "#9ccfd8",
    },
}

# ---------------------------------------------------------------------------
# Optional upstream fetchers. Each takes the raw Lua source of the upstream
# palette file and returns a {role: hex} dict. Roles missing from the
# returned dict fall back to the curated baseline above.
#
# All fetchers use the same loose `key = "#hex"` regex extractor; they
# differ only in (URL, lua-key -> hed-role mapping). Most themes use
# semantic colour names (`green`, `magenta`, …); we map those to the role
# we expect them to fill, mirroring conventional syntax-highlight choices.
# ---------------------------------------------------------------------------

_HEX_RE = re.compile(r'(\w+)\s*=\s*"(#[0-9a-fA-F]{6,8})"')


def _extract(lua_text: str, key_to_role: dict[str, str]) -> dict[str, str]:
    """Pull `key = "#hex"` pairs from lua_text and remap into hed roles.

    Strips alpha bytes from 8-char hex (some palettes carry RRGGBBAA).
    """
    out: dict[str, str] = {}
    for m in _HEX_RE.finditer(lua_text):
        key = m.group(1)
        if key not in key_to_role:
            continue
        hex_val = m.group(2)[:7]  # drop alpha
        out[key_to_role[key]] = hex_val
    return out


def _http_get(url: str, timeout: float = 10.0) -> str:
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read().decode("utf-8", errors="replace")


def _fetch_tokyo_night() -> dict[str, str]:
    url = ("https://raw.githubusercontent.com/folke/tokyonight.nvim/"
           "main/lua/tokyonight/colors/storm.lua")
    return _extract(_http_get(url), {
        "green":   "string",
        "comment": "comment",
        "fg":      "variable",
        "yellow":  "constant",
        "orange":  "number",
        "magenta": "keyword",
        "blue1":   "type",
        "blue":    "function",
        "red":     "label",
    })


def _fetch_gruvbox() -> dict[str, str]:
    url = ("https://raw.githubusercontent.com/ellisonleao/gruvbox.nvim/"
           "main/lua/gruvbox.lua")
    return _extract(_http_get(url), {
        "bright_green":  "string",
        "gray":          "comment",
        "fg1":           "variable",
        "bright_purple": "constant",
        "bright_red":    "keyword",
        "bright_yellow": "type",
        "bright_aqua":   "attribute",
        "bright_orange": "label",
    })


def _fetch_dracula() -> dict[str, str]:
    url = ("https://raw.githubusercontent.com/Mofiqul/dracula.nvim/"
           "main/lua/dracula/init.lua")
    return _extract(_http_get(url), {
        "yellow":  "string",
        "comment": "comment",
        "fg":      "variable",
        "purple":  "constant",
        "pink":    "keyword",
        "cyan":    "type",
        "green":   "function",
        "orange":  "label",
    })


def _fetch_catppuccin_mocha() -> dict[str, str]:
    url = ("https://raw.githubusercontent.com/catppuccin/nvim/"
           "main/lua/catppuccin/palettes/mocha.lua")
    return _extract(_http_get(url), {
        "green":     "string",
        "overlay0":  "comment",
        "text":      "variable",
        "peach":     "constant",
        "mauve":     "keyword",
        "yellow":    "type",
        "blue":      "function",
        "red":       "label",
        "teal":      "operator",
    })


def _fetch_nord() -> dict[str, str]:
    url = ("https://raw.githubusercontent.com/shaunsingh/nord.nvim/"
           "master/lua/nord/colors.lua")
    return _extract(_http_get(url), {
        "nord14_gui":  "string",
        "nord3_gui":   "comment",
        "nord4_gui":   "variable",
        "nord15_gui":  "constant",
        "nord9_gui":   "keyword",
        "nord7_gui":   "type",
        "nord8_gui":   "function",
        "nord11_gui":  "label",
    })


def _fetch_rose_pine() -> dict[str, str]:
    url = ("https://raw.githubusercontent.com/rose-pine/neovim/"
           "main/lua/rose-pine/palette.lua")
    return _extract(_http_get(url), {
        "gold":   "string",
        "muted":  "comment",
        "text":   "variable",
        "rose":   "constant",
        "pine":   "keyword",
        "iris":   "type",
        "foam":   "function",
        "love":   "label",
    })


UPSTREAM: dict[str, Callable[[], dict[str, str]]] = {
    "tokyo-night":      _fetch_tokyo_night,
    "gruvbox":          _fetch_gruvbox,
    "dracula":          _fetch_dracula,
    "catppuccin-mocha": _fetch_catppuccin_mocha,
    "nord":             _fetch_nord,
    "rose-pine":        _fetch_rose_pine,
}

# ---------------------------------------------------------------------------
# C emission
# ---------------------------------------------------------------------------

# Roles we emit always-bold for; anything else is plain fg.
BOLD_ROLES = {"title"}
UNDERLINE_ROLES = {"uri"}


def hex_to_sgr(hex_str: str, *, bold: bool = False, underline: bool = False) -> str:
    """Return the C string literal body for an ANSI 24-bit colour SGR."""
    h = hex_str.lstrip("#")
    if len(h) < 6:
        raise ValueError(f"bad hex {hex_str!r}")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    parts: list[str] = []
    if bold:
        parts.append("1")
    if underline:
        parts.append("4")
    parts += ["38", "2", str(r), str(g), str(b)]
    return f"\\x1b[{';'.join(parts)}m"


def c_symbol(name: str) -> str:
    return name.replace("-", "_").replace(".", "_")


def emit_apply_fn(name: str, palette: dict[str, str]) -> str:
    sym = c_symbol(name)
    lines = [f"static void apply_{sym}(void) {{"]
    # Stable role order for diff-friendliness.
    role_order = [
        "string", "comment", "variable", "constant", "number",
        "keyword", "type", "function", "attribute", "label",
        "operator", "punctuation", "title", "uri",
        "diag.error", "diag.warn", "diag.note",
    ]
    for role in role_order:
        if role not in palette:
            continue
        sgr = hex_to_sgr(palette[role],
                         bold=role in BOLD_ROLES,
                         underline=role in UNDERLINE_ROLES)
        lines.append(f'    theme_palette_set("{role}", "{sgr}");')
    lines.append("}")
    return "\n".join(lines)


def emit_themes_c(palettes: dict[str, dict[str, str]]) -> str:
    parts = [
        "/* Generated by scripts/gen_themes.py — do not edit by hand.",
        " *",
        " * To add or refresh themes, edit scripts/gen_themes.py and re-run it.",
        " * Browse https://dotfyle.com/neovim/colorscheme/top for ideas. */",
        "",
        '#include "treesitter/theme.h"',
        "",
    ]
    for name, palette in palettes.items():
        parts.append(emit_apply_fn(name, palette))
        parts.append("")
    parts.append("void hed_themes_register_all(void) {")
    parts.append("    if (!&theme_register) return;")
    for name in palettes:
        sym = c_symbol(name)
        parts.append(f'    theme_register("{name}", apply_{sym});')
    parts.append("}")
    return "\n".join(parts) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fetch", action="store_true",
                    help="try to refresh palettes from upstream Lua sources")
    ap.add_argument("--out", type=Path,
                    default=Path(__file__).resolve().parent.parent
                    / "plugins" / "hed_themes" / "themes.c",
                    help="output path (default: %(default)s)")
    args = ap.parse_args()

    palettes: dict[str, dict[str, str]] = {n: dict(p) for n, p in THEMES.items()}

    if args.fetch:
        for name, fetch in UPSTREAM.items():
            print(f"fetch {name} ...", end=" ", file=sys.stderr, flush=True)
            try:
                fresh = fetch()
            except Exception as e:  # noqa: BLE001
                print(f"failed ({e}); keeping curated", file=sys.stderr)
                continue
            if not fresh:
                print("empty; keeping curated", file=sys.stderr)
                continue
            # Layer over curated so missing roles keep their baseline.
            palettes[name].update(fresh)
            print(f"got {len(fresh)} role(s)", file=sys.stderr)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(emit_themes_c(palettes))
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
