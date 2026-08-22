#include "input/dot_repeat.h"
#include "editor.h"
#include "input/keybinds.h"
#include "input/macros.h"
#include "input/registers.h"
#include "utils/undo.h"
#include <string.h>

#define DOT_RUN_MAX 4096

static char run[DOT_RUN_MAX];
static size_t run_len = 0;
static int run_overflow = 0;

/* Baseline to detect "this run changed the buffer". */
static unsigned long base_gen = 0;
static int base_buffer = -1;

void dot_record_key(int key) {
    if (run_overflow)
        return;
    /* First key of a run: snapshot the change-detection baseline
     * before the command executes. */
    if (run_len == 0) {
        base_gen = undo_mod_generation();
        base_buffer = E.current_buffer;
    }
    char s[32];
    macro_key_to_string(key, s, sizeof(s));
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

static void run_reset(void) {
    run_len = 0;
    run_overflow = 0;
    /* Baseline is re-snapshotted when the next run's first key is
     * recorded (dot_record_key). */
}

void dot_boundary(void) {
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
    run_reset();
}
