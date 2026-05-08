#include "input.h"
#include "editor.h"
#include "terminal.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

int ed_parse_key_from_fd(int fd) {
    int nread;
    char c;
    while ((nread = read(fd, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    int key = 0;

    if (c == '\x1b') {
        char seq[3];

        if (read(fd, &seq[0], 1) != 1) {
            /* Bare ESC. */
            key = '\x1b';
        } else if (seq[0] == 'O') {
            /* SS3 sequence: ESC O <letter> for F1-F4 (xterm) and some
             * Home/End forms. */
            char letter;
            if (read(fd, &letter, 1) != 1) {
                key = KEY_META | 'O';
            } else {
                switch (letter) {
                case 'P': key = KEY_F1; break;
                case 'Q': key = KEY_F2; break;
                case 'R': key = KEY_F3; break;
                case 'S': key = KEY_F4; break;
                case 'H': key = KEY_HOME; break;
                case 'F': key = KEY_END; break;
                default:  key = '\x1b'; break;
                }
            }
        } else if (seq[0] != '[') {
            /* ESC followed by any non-CSI byte = Meta/Alt + that key.
             * Terminals encode M-x as the two bytes ESC, 'x'. */
            key = KEY_META | (unsigned char)seq[0];
        } else if (read(fd, &seq[1], 1) != 1) {
            /* CSI prefix but no follow-up — degrade to bare ESC. */
            key = '\x1b';
        } else {
            /* seq[0] == '[': CSI escape sequence. */
            if (seq[1] >= '0' && seq[1] <= '9') {
                /* Read full digit run, then ';' (modifier) or '~' (terminator). */
                char digits[8] = { seq[1] };
                int dlen = 1;
                char tail = '\0';
                int parse_ok = 1;
                while (dlen < (int)sizeof(digits) - 1) {
                    char c2;
                    if (read(fd, &c2, 1) != 1) { parse_ok = 0; break; }
                    if (c2 >= '0' && c2 <= '9') {
                        digits[dlen++] = c2;
                        continue;
                    }
                    tail = c2;
                    break;
                }
                digits[dlen] = '\0';
                int n = atoi(digits);

                /* Map a CSI-numeric "~" code to a special key. */
                int base = 0;
                switch (n) {
                case 3:  base = KEY_DELETE;    break;
                case 5:  base = KEY_PAGE_UP;   break;
                case 6:  base = KEY_PAGE_DOWN; break;
                case 11: case 12: case 13: case 14:
                    base = KEY_F1 + (n - 11); break;
                case 15: base = KEY_F5;        break;
                case 17: case 18: case 19: case 20: case 21:
                    base = KEY_F6 + (n - 17); break;
                case 23: case 24:
                    base = KEY_F11 + (n - 23); break;
                case 200: base = KEY_PASTE_START; break;
                case 201: base = KEY_PASTE_END;   break;
                case 1:  /* used only with ';<mod><letter>' suffix */
                    break;
                }

                if (!parse_ok) {
                    key = '\x1b';
                } else if (tail == '~') {
                    key = base ? base : '\x1b';
                } else if (tail == ';') {
                    /* Modifier follows: <mod><letter|~>.
                     * For n==1, letter encodes the key (A/B/C/D/H/F).
                     * For function keys, terminator is '~' and base
                     * comes from `n`. */
                    char mod_b, term;
                    if (read(fd, &mod_b, 1) != 1 ||
                        read(fd, &term, 1) != 1) {
                        key = '\x1b';
                    } else {
                        int b = 0;
                        if (n == 1) {
                            switch (term) {
                            case 'A': b = KEY_ARROW_UP;    break;
                            case 'B': b = KEY_ARROW_DOWN;  break;
                            case 'C': b = KEY_ARROW_RIGHT; break;
                            case 'D': b = KEY_ARROW_LEFT;  break;
                            case 'H': b = KEY_HOME;        break;
                            case 'F': b = KEY_END;         break;
                            }
                        } else if (term == '~') {
                            b = base;
                        }
                        if (b) {
                            int flags = 0;
                            switch (mod_b) {
                            case '2': flags = KEY_SHIFT; break;
                            case '3': flags = KEY_META; break;
                            case '4': flags = KEY_SHIFT | KEY_META; break;
                            case '5': flags = KEY_CTRL; break;
                            case '6': flags = KEY_SHIFT | KEY_CTRL; break;
                            case '7': flags = KEY_META | KEY_CTRL; break;
                            case '8': flags = KEY_SHIFT | KEY_META | KEY_CTRL; break;
                            }
                            key = flags | b;
                        } else {
                            key = '\x1b';
                        }
                    }
                } else {
                    key = '\x1b';
                }
            } else {
                switch (seq[1]) {
                case 'A': key = KEY_ARROW_UP;    break;
                case 'B': key = KEY_ARROW_DOWN;  break;
                case 'C': key = KEY_ARROW_RIGHT; break;
                case 'D': key = KEY_ARROW_LEFT;  break;
                case 'H': key = KEY_HOME;        break;
                case 'F': key = KEY_END;         break;
                default:  key = '\x1b';          break;
                }
            }
        }
    } else {
        key = c;
    }

    return key;
}
