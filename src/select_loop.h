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
typedef void (*ed_timer_cb)(void *ud);

void ed_loop_init(void);
int  ed_loop_register(const char *name, int fd, ed_fd_cb on_readable, void *ud);
void ed_loop_unregister(int fd);

/* Schedule a one-shot timer to fire roughly `delay_ms` from now. The
 * loop dispatches it from inside ed_loop_select_once() after select()
 * returns (either because an fd fired or the timeout elapsed).
 *
 * `name` is used as a throttle key — registering a new timer with the
 * same name replaces the prior one. Callers that just want
 * "(re)schedule a wakeup in N ms, cancel any pending one" pass the
 * same name every time and never bother with cancellation tokens.
 *
 * Names are owned by the caller and must outlive the timer (string
 * literal is the typical choice). Returns 0 on success. */
int  ed_loop_timer_after(const char *name, int delay_ms,
                         ed_timer_cb cb, void *ud);

/* Cancel the timer with the given name, if any. */
void ed_loop_timer_cancel(const char *name);

/* Used by main.c. `select_one` blocks on the registered set (with a
 * timeout derived from any pending timers), invokes the callback for
 * every fd that became readable, and dispatches expired timers.
 * Returns 0 on success, -1 if select() failed with something other
 * than EINTR. */
int ed_loop_select_once(void);

#endif
