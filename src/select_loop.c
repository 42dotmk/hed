#include "select_loop.h"
#include "terminal.h"
#include "lib/log.h"
#include "stb_ds.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    const char *name;
    int         fd;
    ed_fd_cb    cb;
    void       *ud;
} Watch;

typedef struct {
    const char *name;     /* throttle key (caller-owned, must outlive timer) */
    long long   due_ms;   /* monotonic deadline in milliseconds */
    ed_timer_cb cb;
    void       *ud;
} Timer;

static Watch *g_watches = NULL;
static Timer *g_timers  = NULL;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

void ed_loop_init(void) {
    arrfree(g_watches);
    g_watches = NULL;
    arrfree(g_timers);
    g_timers = NULL;
}

int ed_loop_register(const char *name, int fd, ed_fd_cb on_readable, void *ud) {
    if (fd < 0 || !on_readable) return -1;
    for (ptrdiff_t i = 0; i < arrlen(g_watches); i++) {
        if (g_watches[i].fd == fd) {
            /* Replace in place — last-write-wins, mirrors keybind semantics. */
            g_watches[i].name = name;
            g_watches[i].cb   = on_readable;
            g_watches[i].ud   = ud;
            return 0;
        }
    }
    Watch w = { name, fd, on_readable, ud };
    arrput(g_watches, w);
    return 0;
}

void ed_loop_unregister(int fd) {
    for (ptrdiff_t i = 0; i < arrlen(g_watches); i++) {
        if (g_watches[i].fd == fd) {
            arrdel(g_watches, i);
            return;
        }
    }
}

int ed_loop_timer_after(const char *name, int delay_ms,
                        ed_timer_cb cb, void *ud) {
    if (!cb || !name) return -1;
    if (delay_ms < 0) delay_ms = 0;
    long long due = now_ms() + delay_ms;

    /* Replace any existing timer with the same name. */
    for (ptrdiff_t i = 0; i < arrlen(g_timers); i++) {
        if (g_timers[i].name && strcmp(g_timers[i].name, name) == 0) {
            g_timers[i].due_ms = due;
            g_timers[i].cb     = cb;
            g_timers[i].ud     = ud;
            return 0;
        }
    }
    Timer t = { name, due, cb, ud };
    arrput(g_timers, t);
    return 0;
}

void ed_loop_timer_cancel(const char *name) {
    if (!name) return;
    for (ptrdiff_t i = arrlen(g_timers) - 1; i >= 0; i--) {
        if (g_timers[i].name && strcmp(g_timers[i].name, name) == 0) {
            arrdel(g_timers, i);
        }
    }
}

/* Compute the smallest remaining time across all pending timers.
 * Writes the result into *out and returns 1 if a timer is pending,
 * 0 if none (caller should pass NULL to select). */
static int compute_select_timeout(struct timeval *out) {
    if (arrlen(g_timers) == 0) return 0;
    long long now = now_ms();
    long long best = -1;
    for (ptrdiff_t i = 0; i < arrlen(g_timers); i++) {
        long long rem = g_timers[i].due_ms - now;
        if (rem < 0) rem = 0;
        if (best < 0 || rem < best) best = rem;
    }
    out->tv_sec  = (long)(best / 1000);
    out->tv_usec = (long)((best % 1000) * 1000);
    return 1;
}

/* Dispatch any timers whose deadline has passed. Done in two passes so
 * a callback that schedules a new timer (even with the same name) can't
 * cause us to fire it twice in the same loop iteration. */
static void dispatch_expired_timers(void) {
    if (arrlen(g_timers) == 0) return;
    long long now = now_ms();

    /* Snapshot the entries to fire so we can remove them from the
     * registry before invoking — the callback is then free to register
     * a fresh timer with the same name without interference. */
    Timer *fire = NULL;
    for (ptrdiff_t i = arrlen(g_timers) - 1; i >= 0; i--) {
        if (g_timers[i].due_ms <= now) {
            arrput(fire, g_timers[i]);
            arrdel(g_timers, i);
        }
    }
    for (ptrdiff_t i = 0; i < arrlen(fire); i++) {
        fire[i].cb(fire[i].ud);
    }
    arrfree(fire);
}

int ed_loop_select_once(void) {
    fd_set rfds;
    FD_ZERO(&rfds);
    int maxfd = -1;
    for (ptrdiff_t i = 0; i < arrlen(g_watches); i++) {
        int fd = g_watches[i].fd;
        FD_SET(fd, &rfds);
        if (fd > maxfd) maxfd = fd;
    }

    struct timeval  tv;
    struct timeval *tvp = NULL;
    if (compute_select_timeout(&tv)) tvp = &tv;

    if (maxfd < 0 && !tvp) return 0; /* nothing to wait on */

    int rc = select(maxfd + 1, &rfds, NULL, NULL, tvp);
    if (rc == -1) {
        if (errno == EINTR) return 0;
        return -1;
    }

    /* Snapshot before dispatching so a callback that registers/unregisters
     * during its run cannot invalidate our iteration. */
    ptrdiff_t n = arrlen(g_watches);
    Watch *snap = NULL;
    if (n > 0) {
        snap = malloc((size_t)n * sizeof(Watch));
        if (!snap) return 0;
        memcpy(snap, g_watches, (size_t)n * sizeof(Watch));
    }

    for (ptrdiff_t i = 0; i < n; i++) {
        if (FD_ISSET(snap[i].fd, &rfds)) {
            snap[i].cb(snap[i].fd, snap[i].ud);
        }
    }
    free(snap);

    dispatch_expired_timers();
    return 0;
}
