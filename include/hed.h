#ifndef HED_H
#define HED_H
#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>

/* Include all module headers */
#include "sizedstr.h"
#include "buffer.h"
#include "editor.h"
#include "terminal.h"
#include "hooks.h"
#include "keybinds.h"
#include "buf_helpers.h"
#include "commands.h"
#include "history.h"
#include "registers.h"
#include "undo.h"
#include "quickfix.h"
#include "log.h"
#include "window.h"
#include "ts.h"

#endif
