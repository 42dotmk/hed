#ifndef HED_INPUT_H
#define HED_INPUT_H

/*
 * Block on `fd` until one logical keypress is available, then return it as
 * an int with KEY_META/KEY_CTRL/KEY_SHIFT flags OR'd onto the base key
 * (KEY_* constants live in editor.h).
 *
 * Decodes one of:
 *   - plain byte / ASCII control char
 *   - bare ESC ('\x1b')
 *   - ESC + non-CSI byte         -> KEY_META | byte  (Alt/Meta + key)
 *   - SS3:  ESC O P/Q/R/S/H/F    -> F1-F4, Home, End  (xterm)
 *   - CSI:  ESC [ A/B/C/D/H/F    -> arrows, Home, End
 *   - CSI numeric: ESC [ <n> ~   -> Delete, PageUp/Down, F5-F12
 *   - CSI modified: ESC [1;<m><L> or ESC [<n>;<m>~ -- xterm modifier
 *     matrix (m=2..8) decoded into Shift/Meta/Ctrl flag combinations.
 * Truncated or unrecognized escapes degrade to bare ESC rather than
 * blocking for more bytes.
 *
 * Pure parser: no global state, no macro queue, no recording. The
 * production caller (`ed_read_key`) layers those concerns on top.
 */
int ed_parse_key_from_fd(int fd);


#endif
