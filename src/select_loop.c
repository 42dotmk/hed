#include "select_loop.h"
#include "terminal.h"
#include "lib/log.h"
#include "stb_ds.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    int         fd;
    ed_fd_cb    cb;
    void       *ud;
} Watch;

static Watch *g_watches = NULL;

void ed_loop_init(void) {
    arrfree(g_watches);
    g_watches = NULL;
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

int ed_loop_select_once(void) {
    fd_set rfds;
    FD_ZERO(&rfds);
    int maxfd = -1;
    for (ptrdiff_t i = 0; i < arrlen(g_watches); i++) {
        int fd = g_watches[i].fd;
        FD_SET(fd, &rfds);
        if (fd > maxfd) maxfd = fd;
    }
    if (maxfd < 0) return 0;

    int rc = select(maxfd + 1, &rfds, NULL, NULL, NULL);
    if (rc == -1) {
        if (errno == EINTR) return 0;
        return -1;
    }

    /* Snapshot before dispatching so a callback that registers/unregisters
     * during its run cannot invalidate our iteration. */
    ptrdiff_t n = arrlen(g_watches);
    Watch *snap = malloc((size_t)n * sizeof(Watch));
    if (!snap) return 0;
    memcpy(snap, g_watches, (size_t)n * sizeof(Watch));

    for (ptrdiff_t i = 0; i < n; i++) {
        if (FD_ISSET(snap[i].fd, &rfds)) {
            snap[i].cb(snap[i].fd, snap[i].ud);
        }
    }
    free(snap);
    return 0;
}
