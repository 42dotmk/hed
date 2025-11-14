#include "hed.h"
#include "term_pane.h"

#ifdef USE_LIBVTERM

#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

struct TermPane {
    int open;
    int height;
    int rows;
    int cols;
    int pty_fd;
    pid_t pid;
    VTerm *vt;
    VTermScreen *screen;
};

static TermPane G_term;

static void term_pane_reset(void) {
    TermPane *t = &G_term;
    t->open = 0;
    t->height = 0;
    t->rows = 0;
    t->cols = 0;
    if (t->pty_fd >= 0) {
        close(t->pty_fd);
        t->pty_fd = -1;
    }
    if (t->pid > 0) {
        int status;
        waitpid(t->pid, &status, WNOHANG);
        t->pid = -1;
    }
    if (t->screen) {
        vterm_screen_free(t->screen);
        t->screen = NULL;
    }
    if (t->vt) {
        vterm_free(t->vt);
        t->vt = NULL;
    }
}

TermPane *term_pane_get(void) {
    return &G_term;
}

int term_pane_open(const char *cmd, int height) {
    if (!cmd || !*cmd) return 0;
    TermPane *t = &G_term;
    if (t->open) {
        term_pane_reset();
    } else {
        t->pty_fd = -1;
        t->pid = -1;
        t->vt = NULL;
        t->screen = NULL;
    }

    t->height = height > 0 ? height : 10;

    int master_fd = -1;
    pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
    if (pid < 0) {
        ed_set_status_message("term: forkpty failed");
        term_pane_reset();
        return 0;
    }
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    t->pty_fd = master_fd;
    t->pid = pid;
    fcntl(t->pty_fd, F_SETFL, O_NONBLOCK);
    t->open = 1;

    t->rows = 0;
    t->cols = 0;
    t->vt = vterm_new(24, 80);
    if (!t->vt) {
        ed_set_status_message("term: vterm_new failed");
        term_pane_reset();
        return 0;
    }
    t->screen = vterm_obtain_screen(t->vt);
    vterm_screen_reset(t->screen, 1);

    return 1;
}

void term_pane_close(void) {
    term_pane_reset();
}

int term_pane_fd(void) {
    TermPane *t = &G_term;
    if (!t->open) return -1;
    return t->pty_fd;
}

void term_pane_resize(int rows, int cols) {
    TermPane *t = &G_term;
    if (!t->open || !t->vt || t->pty_fd < 0) return;
    if (rows <= 0 || cols <= 0) return;
    if (rows == t->rows && cols == t->cols) return;

    t->rows = rows;
    t->cols = cols;
    vterm_set_size(t->vt, rows, cols);

    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    ioctl(t->pty_fd, TIOCSWINSZ, &ws);
}

void term_pane_poll(void) {
    TermPane *t = &G_term;
    if (!t->open || t->pty_fd < 0 || !t->vt || !t->screen) return;

    char buf[4096];
    while (1) {
        ssize_t n = read(t->pty_fd, buf, sizeof(buf));
        if (n <= 0) break;
        vterm_input_write(t->vt, buf, (size_t)n);
        vterm_screen_flush_damage(t->screen);
    }

    if (t->pid > 0) {
        int status = 0;
        pid_t r = waitpid(t->pid, &status, WNOHANG);
        if (r == t->pid) {
            t->pid = -1;
        }
    }
}

void term_pane_draw(const Window *win, Abuf *ab) {
    TermPane *t = &G_term;
    if (!t->open || !t->screen || !win || !ab) return;

    int rows = win->height;
    int cols = win->width;
    if (rows <= 0 || cols <= 0) return;

    term_pane_resize(rows, cols);

    for (int y = 0; y < rows; y++) {
        ansi_move(ab, win->top + y, win->left);
        for (int x = 0; x < cols; x++) {
            VTermScreenCell cell;
            vterm_screen_get_cell(t->screen, y, x, &cell);
            char ch = cell.chars[0] ? cell.chars[0] : ' ';
            ab_append(ab, &ch, 1);
        }
        ansi_clear_eol(ab);
    }
}

int term_pane_handle_key(int key) {
    TermPane *t = &G_term;
    if (!t->open || t->pty_fd < 0) return 0;

    char seq[8];
    int len = 0;

    switch (key) {
        case '\r':
            seq[0] = '\r';
            len = 1;
            break;
        case '\x7f':
            seq[0] = '\x7f';
            len = 1;
            break;
        case KEY_ARROW_UP:
            seq[0] = '\x1b'; seq[1] = '['; seq[2] = 'A';
            len = 3;
            break;
        case KEY_ARROW_DOWN:
            seq[0] = '\x1b'; seq[1] = '['; seq[2] = 'B';
            len = 3;
            break;
        case KEY_ARROW_RIGHT:
            seq[0] = '\x1b'; seq[1] = '['; seq[2] = 'C';
            len = 3;
            break;
        case KEY_ARROW_LEFT:
            seq[0] = '\x1b'; seq[1] = '['; seq[2] = 'D';
            len = 3;
            break;
        default:
            if (!iscntrl(key) && key >= 0 && key < 128) {
                seq[0] = (char)key;
                len = 1;
            } else {
                return 0;
            }
    }
    if (len > 0) {
        write(t->pty_fd, seq, (size_t)len);
        return 1;
    }
    return 0;
}

#else /* !USE_LIBVTERM */

struct TermPane {
    int dummy;
};

static TermPane G_term;

TermPane *term_pane_get(void) {
    return &G_term;
}

int term_pane_open(const char *cmd, int height) {
    (void)cmd;
    (void)height;
    ed_set_status_message("term: libvterm not available (rebuild with -DUSE_LIBVTERM and link libvterm)");
    return 0;
}

void term_pane_poll(void) {
}

void term_pane_close(void) {
}

void term_pane_resize(int rows, int cols) {
    (void)rows;
    (void)cols;
}

int term_pane_fd(void) {
    return -1;
}

void term_pane_draw(const Window *win, Abuf *ab) {
    (void)win;
    (void)ab;
}

int term_pane_handle_key(int key) {
    (void)key;
    return 0;
}

#endif /* USE_LIBVTERM */

