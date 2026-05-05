#ifndef HED_H
#define HED_H

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

/* Library helpers - must come first for type definitions */
#include "lib/ansi.h"
#include "lib/errors.h"
#include "lib/file_helpers.h"
#include "lib/log.h"
#include "lib/safe_string.h"
#include "lib/sizedstr.h"
#include "lib/strutil.h"
#include "lib/theme.h"
#include "stb_ds.h"

/* Core modules */
#include "commands.h"
#include "commands/cmd_builtins.h"
#include "commands/cmd_util.h"
#include "editor.h"
#include "hook_builtins.h"
#include "hooks.h"
#include "keybinds.h"
#include "keybinds_builtins.h"
#include "plugin.h"
#include "registers.h"
#include "terminal.h"
#include "utils/undo.h"

/* Buffer subsystem */
#include "buf/buf_helpers.h"
#include "buf/buffer.h"
#include "buf/row.h"
#include "buf/textobj.h"

/* Utilities */
#include "utils/bottom_ui.h"
#include "utils/fzf.h"
#include "utils/history.h"
#include "utils/jump_list.h"
#include "utils/quickfix.h"
#include "utils/recent_files.h"
#include "utils/term_cmd.h"

/* UI */
#include "ui/abuf.h"
#include "ui/window.h"
#include "ui/winmodal.h"
#include "ui/wlayout.h"

#endif
