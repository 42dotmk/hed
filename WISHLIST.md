
## Renderer abstraction

Today rendering is *partially* abstracted — good entry point and sink, but
hard-wired to ANSI/VT. A second renderer (headless test backend, GUI, GPU,
web) can't be added without first decoupling the surface.

### Current state

- **One render entry point.** `ed_render_frame()` in `src/terminal.c:837`
  is the only public render call (~15 call sites, all just invoke it).
- **One stdout write.** `terminal.c:970` is the single `write(STDOUT_FILENO,
  ab.data, ab.len)` in the render path.
- **Append-buffer sink.** `src/ui/abuf.{c,h}` — growable `char*`. All
  drawing routines take an `Abuf *`.
- **`hl_line_fn` hook.** `buf->hl_line_fn` (buffer.h:48) lets plugins inject
  highlighting per-buffer. Tree-sitter is reached via weak symbols so the
  renderer doesn't require it. This pattern is the right shape — the rest
  of the renderer should look more like it.

### Where the abstraction leaks

1. **Renderer is hard-wired to ANSI/VT.** `ed_draw_rows_win` calls
   `ansi_move`, `ansi_invert_on`, `ansi_sgr_reset`, `ansi_clear_eol`
   directly. No `Renderer` vtable. A new backend would have to either
   parse the ANSI byte stream or reimplement `ed_draw_rows_win`.
2. **No cell/grid model.** Output is a byte stream of escapes interleaved
   with text. Diff-based redraws, screenshot tests, and non-ANSI backends
   all need an intermediate cell grid (cf. notcurses, ratatui, Helix
   `Surface`).
3. **Theme = ANSI string literals.** `COLOR_COMMENT` etc. in
   `src/lib/theme.h` are SGR strings (`"\x1b[38;5;Nm"`), not semantic
   tokens. Tree-sitter and markdown highlighters emit raw escapes into
   the line buffer. A non-ANSI renderer can't consume those without a
   parser.
4. **Side channels bypass the `Abuf`:**
   - `src/editor.c:30–39` — cursor-shape escapes on mode change
   - `src/terminal.c:35–99` — clear-on-die, bracketed paste toggle,
     cursor-position query, fallback window sizing
5. **`E.render_x` lives on the global** and is set during scroll, read
   during cursor placement — fine for one renderer, ties cursor math to
   terminal column semantics.

### Target shape

```c
typedef struct Attr { int fg, bg, flags; } Attr;

struct Renderer {
    void (*begin_frame)(void*);
    void (*end_frame)(void*);
    void (*move)(void*, int row, int col);
    void (*put)(void*, const char *utf8, int len, Attr a);
    void (*clear_eol)(void*);
    void (*set_cursor)(void*, int row, int col, CursorStyle);
    void (*get_size)(void*, int *r, int *c);
};
```

Plus: themes become `Attr` instead of SGR strings; `hl_line_fn` emits
attributed runs instead of ANSI bytes; the stray direct `write()` calls
route through the renderer. ANSI maps attrs → SGR centrally in one
backend.

### Effort estimate

| Phase | Effort | What ships |
|---|---|---|
| 1. Renderer vtable wrapping ANSI | 1–2 days | Mechanical port of the 11 `ansi_*` helpers + 9 stray `write(STDOUT_FILENO,…)` sites into methods; thread `Renderer*` through `ed_draw_rows_win` / `draw_status_bar` / `wlayout_draw_decorations`. Output byte-identical. Isolates the surface but doesn't unlock a second backend on its own. |
| 2. Semantic attrs + cell model | 1–2 weeks | `lib/theme.h` becomes `Attr`. `hl_line_fn` contract changes from "emit bytes" to "emit attributed runs" — touches `plugins/treesitter/ts_impl.c`, `plugins/markdown/markdown_highlight.c`, `plugins/hed_themes/themes.c`. Raw-escape spots in `commands/commands_buffer.c`, `editor.h`, `buf/buffer.h`, `plugins/mail/mail_impl.c` fold into the same model. Risk: subtle highlight regressions. |
| 3. Second backend | varies | Headless/test backend: ~1 day on top of Phase 2 (record cells into a grid, dump for golden tests). GUI/GPU backend: weeks, dominated by font/glyph/input plumbing — not the abstraction. |

### Recommendation

If the goal is "swap in a real second renderer," commit to Phases 1+2 —
Phase 1 alone is a false summit. If the goal is "be ready to add one
later and get testability now," Phase 1 + a minimal headless backend is
the cheapest concrete win and forces discovery of the leaks the vtable
doesn't catch.

**Main tradeoff:** Phase 2 is a flag day for highlight plugins. Doing it
before more highlighters land is much cheaper than after.

