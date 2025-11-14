#ifndef HED_H
#define HED_H
#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/types.h>

/* Library helpers - must come first for type definitions */
#include "sizedstr.h"
#include "log.h"
#include "ansi.h"
#include "strutil.h"
#include "safe_string.h"
#include "errors.h"

/* Core modules */
#include "editor.h"
#include "terminal.h"
#include "hooks.h"
#include "keybinds.h"
#include "registers.h"
#include "undo.h"
#include "commands.h"

/* Buffer subsystem */
#include "row.h"
#include "buffer.h"
#include "buf_helpers.h"

/* Utilities */
#include "history.h"
#include "recent_files.h"
#include "jump_list.h"
#include "ts.h"
#include "quickfix.h"
#include "term_cmd.h"
#include "fzf.h"
#include "bottom_ui.h"
#include "term_pane.h"
#include "visual_mode.h"

/* UI */
#include "abuf.h"
#include "window.h"
#include "wlayout.h"

#endif
