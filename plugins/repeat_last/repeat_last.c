/* repeat_last plugin: Vim-style dot repeat.
 *
 * Records the raw key run of the last buffer-modifying command — an
 * operator with its arguments (diw, dfx), a simple edit (x, J, p), or
 * a whole insert session (ihello<Esc>) — into the '.' register, and
 * provides `:repeat` (the vim keymap binds `.` to it) to replay that
 * run through the macro queue.
 *
 * Wiring: HOOK_KEY_RAW feeds every real keypress in — including keys
 * read inside command callbacks (operator arguments, f/t targets),
 * which never reach HOOK_KEYPRESS; replayed keys (macro queue,
 * multicursor replay) don't fire it, so a repeat never re-records
 * itself. HOOK_DISPATCH_POST marks command boundaries: back in plain
 * normal mode with nothing pending, the run is complete — saved if it
 * changed the buffer (undo_mod_generation, since buf->dirty misses
 * in-row edits), discarded otherwise. Undo/redo, ex commands and `.`
 * itself are excluded. */

#include "hed.h"
#include "input/macros.h"

#define DOT_RUN_MAX 4096

static char run[DOT_RUN_MAX];
static size_t run_len = 0;
static int run_overflow = 0;

/* Baseline to detect "this run changed the buffer". */
static unsigned long base_gen = 0;
static int base_buffer = -1;

static void dot_record_key(HookKeyEvent *e) {
    if (!e || run_overflow)
        return;
    /* First key of a run: snapshot the change-detection baseline
     * before the command executes. */
    if (run_len == 0) {
        base_gen = undo_mod_generation();
        base_buffer = E.current_buffer;
    }
    char s[32];
    macro_key_to_string(e->key, s, sizeof(s));
    size_t sl = strlen(s);
    if (run_len + sl >= sizeof(run)) {
        run_overflow = 1; /* too long to repeat; drop the whole run */
        return;
    }
    memcpy(run + run_len, s, sl);
    run_len += sl;
}

/* Runs that change the buffer but must not become the repeat target:
 * undo/redo and dot itself (u, U, <C-r>, .), and ex commands. */
static int run_excluded(void) {
    if (run_len == 0)
        return 1;
    if (run[0] == ':')
        return 1;
    if (run_len == 1 && (run[0] == 'u' || run[0] == 'U' || run[0] == '.'))
        return 1;
    if (run_len == 5 && memcmp(run, "<C-r>", 5) == 0)
        return 1;
    return 0;
}

static void dot_boundary(HookKeyEvent *e) {
    (void)e;
    /* A command is still in flight while a multi-key prefix or count
     * is pending, or while a mode (insert/visual/command) is open. */
    if (E.mode != MODE_NORMAL)
        return;
    if (keybind_sequence_pending() || keybind_has_pending_count())
        return;

    int changed =
        E.current_buffer == base_buffer && undo_mod_generation() != base_gen;
    if (changed && run_len > 0 && !run_overflow && !run_excluded())
        regs_set_dot(run, run_len);
    run_len = 0;
    run_overflow = 0;
    /* Baseline is re-snapshotted when the next run's first key is
     * recorded (dot_record_key). */
}

/* :repeat — replay the '.' register through the macro queue. */
static void cmd_repeat(const char *args) {
    (void)args;
    const StrBuf *dot_reg = regs_get('.');
    if (!dot_reg || !dot_reg->data || dot_reg->len == 0) {
        ed_set_status_message("No previous command to repeat");
        return;
    }
    macro_replay_string(dot_reg->data, dot_reg->len);
}

static int repeat_last_init(void) {
    hook_register_key(HOOK_KEY_RAW, dot_record_key);
    hook_register_key(HOOK_DISPATCH_POST, dot_boundary);
    cmd("repeat", cmd_repeat, "repeat the last change");
    return 0;
}

static void repeat_last_deinit(void) {
    hook_unregister(HOOK_KEY_RAW, (HookFn)dot_record_key);
    hook_unregister(HOOK_DISPATCH_POST, (HookFn)dot_boundary);
}

const Plugin plugin_repeat_last = {
    .name = "repeat_last",
    .desc = "vim-style dot repeat (records the last change, :repeat "
            "replays it)",
    .init = repeat_last_init,
    .deinit = repeat_last_deinit,
};
