#include "viewmd.h"
#include "hed.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- module state ---- */

static pid_t viewmd_pid = -1;
/* sun_path is 108 bytes on Linux; keep socket path within that limit */
static char viewmd_socket[108] = {0};
/* Index of the buffer being previewed; -1 = none */
static int viewmd_buf_idx = -1;

/* ---- internal helpers ---- */

static int viewmd_is_running(void) {
    if (viewmd_pid < 0)
        return 0;
    /* WNOHANG: returns 0 if still running, >0 if exited */
    if (waitpid(viewmd_pid, NULL, WNOHANG) > 0) {
        viewmd_pid = -1;
        viewmd_socket[0] = '\0';
        viewmd_buf_idx = -1;
        return 0;
    }
    return 1;
}

static void viewmd_stop(void) {
    if (viewmd_pid > 0) {
        kill(viewmd_pid, SIGTERM);
        waitpid(viewmd_pid, NULL, 0);
    }
    viewmd_pid = -1;
    viewmd_socket[0] = '\0';
    viewmd_buf_idx = -1;
}

/* Push all rows of buf to the viewmd socket. */
static void viewmd_push(Buffer *buf) {
    if (!buf || !viewmd_socket[0] || !viewmd_is_running())
        return;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
        return;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", viewmd_socket);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return;
    }

    /* Send cursor line so viewmd can scroll to the right position. */
    char cursor_hdr[32];
    int hdr_len = snprintf(cursor_hdr, sizeof(cursor_hdr), "CURSOR:%d\n",
                           buf->cursor.y);
    write(sock, cursor_hdr, (size_t)hdr_len);

    for (int i = 0; i < buf->num_rows; i++) {
        const char *text = buf->rows[i].chars.data;
        size_t len = buf->rows[i].chars.len;
        if (text && len > 0)
            write(sock, text, len);
        write(sock, "\n", 1);
    }

    close(sock); /* viewmd re-renders on connection close */
}

/* Spawn viewmd with no initial file (we push content immediately after). */
static int viewmd_launch(void) {
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return 0;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return 0;
    }

    if (pid == 0) {
        /* child: redirect stdout to pipe, stderr to /dev/null, exec viewmd */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execlp("viewmd", "viewmd", "--socket", NULL);
        _exit(1);
    }

    /* parent */
    close(pipefd[1]);
    viewmd_pid = pid;

    /* Read the first line: "VIEWMD_SOCKET=/tmp/viewmd-<pid>.sock" */
    char line[512] = {0};
    int n = 0;
    char c;
    while (n < (int)sizeof(line) - 1) {
        ssize_t r = read(pipefd[0], &c, 1);
        if (r <= 0)
            break;
        if (c == '\n')
            break;
        line[n++] = c;
    }
    close(pipefd[0]);

    const char *prefix = "VIEWMD_SOCKET=";
    size_t plen = strlen(prefix);
    if (strncmp(line, prefix, plen) == 0) {
        strncpy(viewmd_socket, line + plen, sizeof(viewmd_socket) - 1);
        viewmd_socket[sizeof(viewmd_socket) - 1] = '\0';
        return 1;
    }

    /* viewmd didn't print the expected line */
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    viewmd_pid = -1;
    return 0;
}

/* ---- hook callbacks ---- */

static void on_char_insert(const HookCharEvent *e) {
    if (!viewmd_is_running())
        return;
    if (E.current_buffer != viewmd_buf_idx)
        return;
    viewmd_push(e->buf);
}

static void on_line_change(const HookLineEvent *e) {
    if (!viewmd_is_running())
        return;
    if (E.current_buffer != viewmd_buf_idx)
        return;
    viewmd_push(e->buf);
}

static void on_buffer_save(const HookBufferEvent *e) {
    if (!viewmd_is_running())
        return;
    if (E.current_buffer != viewmd_buf_idx)
        return;
    viewmd_push(e->buf);
}

/* ---- public API ---- */

void viewmd_init(void) {
    /* Live typing hooks (insert mode, any filetype) */
    hook_register_char(HOOK_CHAR_INSERT, MODE_INSERT, "*", on_char_insert);
    hook_register_char(HOOK_CHAR_DELETE, MODE_INSERT, "*", on_char_insert);

    /* Line-level changes (normal and insert modes) */
    hook_register_line(HOOK_LINE_INSERT, MODE_NORMAL, "*", on_line_change);
    hook_register_line(HOOK_LINE_DELETE, MODE_NORMAL, "*", on_line_change);
    hook_register_line(HOOK_LINE_INSERT, MODE_INSERT, "*", on_line_change);
    hook_register_line(HOOK_LINE_DELETE, MODE_INSERT, "*", on_line_change);

    /* Always push on save */
    hook_register_buffer(HOOK_BUFFER_SAVE, MODE_NORMAL, "*", on_buffer_save);
}

void cmd_viewmd_preview(const char *args) {
    (void)args;

    /* Toggle off if already previewing this buffer */
    if (viewmd_is_running() && E.current_buffer == viewmd_buf_idx) {
        viewmd_stop();
        ed_set_status_message("viewmd: stopped");
        return;
    }

    /* Stop any previous preview before starting a new one */
    if (viewmd_is_running())
        viewmd_stop();

    if (!viewmd_launch()) {
        ed_set_status_message("viewmd: failed to launch (is viewmd installed?)");
        return;
    }

    viewmd_buf_idx = E.current_buffer;
    Buffer *buf = buf_cur();
    viewmd_push(buf);

    const char *name = (buf && buf->filename) ? buf->filename : "[No Name]";
    ed_set_status_message("viewmd: previewing %s (socket: %s)", name,
                          viewmd_socket);
}
