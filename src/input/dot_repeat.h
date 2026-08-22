#ifndef DOT_REPEAT_H
#define DOT_REPEAT_H

/* Vim-style dot repeat.
 *
 * Records the raw key run of the last buffer-modifying command — an
 * operator with its arguments (diw, dfx), a simple edit (x, J, p), or
 * a whole insert session (ihello<Esc>) — into the '.' register, which
 * `:repeat` (bound to `.`) replays through the macro queue.
 *
 * ed_read_key() feeds every real keypress in via dot_record_key(), so
 * argument keys read inside callbacks are captured too; replayed keys
 * (macro queue, multicursor replay) are not. ed_process_keypress()
 * calls dot_boundary() after each dispatched key: when the editor is
 * back in normal mode with no pending sequence or count, the run is
 * complete — it is saved if it changed the buffer, discarded
 * otherwise. */

void dot_record_key(int key);
void dot_boundary(void);

#endif /* DOT_REPEAT_H */
