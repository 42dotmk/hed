#ifndef HED_SELECT_LOOP_H
#define HED_SELECT_LOOP_H

#include <sys/select.h>

/*
 * Generic readable-fd registry for the main select() loop.
 *
 * Any subsystem (core or plugin) that needs to react to bytes arriving on
 * an fd registers `(fd, on_readable, ud)`. The main loop selects across
 * every registered fd and dispatches the callback for whichever fired.
 *
 * stdin is itself just a registered entry — its callback drives
 * ed_process_keypress.  Plugins (LSP, future debugger / file-watcher /
 * runner-stdout) register their own fds the same way.
 *
 * Names are descriptive only (logging / debugging); callers are
 * responsible for unregistering before close().
 */
typedef void (*ed_fd_cb)(int fd, void *ud);

void ed_loop_init(void);
int  ed_loop_register(const char *name, int fd, ed_fd_cb on_readable, void *ud);
void ed_loop_unregister(int fd);

/* Used by main.c. `select_one` blocks on the registered set, then invokes
 * the callback for every fd that became readable. Returns 0 on success,
 * -1 if select() failed with something other than EINTR. */
int ed_loop_select_once(void);

#endif
