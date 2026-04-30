#ifndef HED_H
#define HED_H
#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define cmd(name, cb, desc) command_register(name, cb, desc)
#define mapn(x, y, d) keybind_register(MODE_NORMAL, x, y, d)
#define mapv(x, y, d) keybind_register(MODE_VISUAL, x, y, d)
#define mapi(x, y, d) keybind_register(MODE_INSERT, x, y, d)
#define mapvb(x, y, d) keybind_register(MODE_VISUAL_BLOCK, x, y, d)
#define mapvl(x, y, d) keybind_register(MODE_VISUAL_LINE, x, y, d)
#define cmapn(x, y)           keybind_register_command(MODE_NORMAL, x, y)
#define cmapv(x, y)           keybind_register_command(MODE_VISUAL, x, y)
#define cmapi(x, y)           keybind_register_command(MODE_INSERT, x, y)

/* Filetype-specific keybind macros — only fire when buffer filetype matches */
#define mapn_ft(ft, x, y, d)  keybind_register_ft(MODE_NORMAL, x, ft, y, d)
#define mapi_ft(ft, x, y, d)  keybind_register_ft(MODE_INSERT, x, ft, y, d)
#define mapv_ft(ft, x, y, d)  keybind_register_ft(MODE_VISUAL, x, ft, y, d)
#define cmapn_ft(ft, x, y)    keybind_register_command_ft(MODE_NORMAL, x, ft, y)
#define cmapi_ft(ft, x, y)    keybind_register_command_ft(MODE_INSERT, x, ft, y)
#define cmapv_ft(ft, x, y)    keybind_register_command_ft(MODE_VISUAL, x, ft, y)

/* Library helpers - must come first for type definitions */
#include "ansi.h"
#include "errors.h"
#include "log.h"
#include "safe_string.h"
#include "sizedstr.h"
#include "strutil.h"
#include "theme.h"

/* Core modules */
#include "commands.h"
#include "editor.h"
#include "hooks.h"
#include "keybinds.h"
#include "registers.h"
#include "terminal.h"
#include "undo.h"

/* Buffer subsystem */
#include "buf_helpers.h"
#include "buffer.h"
#include "row.h"
#include "textobj.h"

/* Utilities */
#include "bottom_ui.h"
#include "fzf.h"
#include "history.h"
#include "jump_list.h"
#include "quickfix.h"
#include "recent_files.h"
#include "term_cmd.h"

/* UI */
#include "abuf.h"
#include "window.h"
#include "winmodal.h"
#include "wlayout.h"

#endif
