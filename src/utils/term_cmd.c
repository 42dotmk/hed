#include "utils/term_cmd.h"
#include "lib/strbuf.h"
#include "lib/strutil.h"
#include "terminal.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Shared popen + line-capture backend. `toggle_raw` leaves raw mode
 * around the child for commands that may write to the terminal. */
static int capture_impl(const char *cmd, char ***out_lines, int *out_count,
                        int toggle_raw) {
    if (!cmd)
        return 0;
    if (out_lines)
        *out_lines = NULL;
    if (out_count)
        *out_count = 0;

    if (toggle_raw)
        disable_raw_mode();
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        if (toggle_raw)
            enable_raw_mode();
        return 0;
    }

    int capacity = 0, count = 0;
    char **lines = NULL;
    char buf[2048];
    int ok = 1;

    while (fgets(buf, sizeof(buf), fp)) {
        str_chomp(buf);
        if (count + 1 > capacity) {
            capacity = capacity ? capacity * 2 : 8;
            char **nl = realloc(lines, (size_t)capacity * sizeof(char *));
            if (!nl) {
                ok = 0;
                break;
            }
            lines = nl;
        }
        char *copy = strdup(buf);
        if (!copy) {
            ok = 0;
            break;
        }
        lines[count++] = copy;
    }

    pclose(fp);
    if (toggle_raw)
        enable_raw_mode();

    if (!ok) {
        term_cmd_free(lines, count);
        return 0;
    }
    if (out_lines)
        *out_lines = lines;
    else
        term_cmd_free(lines, count);
    if (out_count)
        *out_count = count;
    return 1;
}

int term_cmd_run(const char *cmd, char ***out_lines, int *out_count) {
    return capture_impl(cmd, out_lines, out_count, 1);
}

int term_cmd_capture(const char *cmd, char ***out_lines, int *out_count) {
    return capture_impl(cmd, out_lines, out_count, 0);
}

int term_cmd_system(const char *cmd) {
    if (!cmd || !*cmd)
        return -1;
    disable_raw_mode();
    int status = system(cmd);
    enable_raw_mode();
    return status;
}

int term_cmd_run_interactive(const char *cmd, bool acknowledge) {
    if (!cmd)
        return -1;

    disable_raw_mode();
    int status = system(cmd);
    if (acknowledge) {
        fprintf(stdout,
                "\n\n[command finished with status %d] "
                "Press Enter to return to hed...",
                status);
        fflush(stdout);
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) {
        }
    }

    enable_raw_mode();
    return status;
}

int term_cmd_filter(const char *cmd, const char *in, size_t in_len, char **out,
                    size_t *out_len) {
    if (out)
        *out = NULL;
    if (out_len)
        *out_len = 0;
    if (!cmd)
        return -1;

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0)
        return -1;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);

    /* Pump stdin and stdout together so neither pipe can fill up and
     * deadlock (the old write-everything-then-read pattern could).
     * EPIPE on a child that stopped reading must not kill the editor. */
    void (*old_pipe)(int) = signal(SIGPIPE, SIG_IGN);

    StrBuf acc = strbuf_new();
    int wfd = in_pipe[1], rfd = out_pipe[0];
    size_t off = 0;
    if (!in || in_len == 0) {
        close(wfd);
        wfd = -1;
    }

    while (rfd >= 0) {
        struct pollfd pfds[2];
        int n = 0, ri = -1, wi = -1;
        pfds[n] = (struct pollfd){.fd = rfd, .events = POLLIN};
        ri = n++;
        if (wfd >= 0) {
            pfds[n] = (struct pollfd){.fd = wfd, .events = POLLOUT};
            wi = n++;
        }
        if (poll(pfds, (nfds_t)n, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (wi >= 0 && (pfds[wi].revents & (POLLOUT | POLLERR | POLLHUP))) {
            ssize_t w = write(wfd, in + off, in_len - off);
            if (w > 0)
                off += (size_t)w;
            if (w <= 0 || off == in_len) {
                close(wfd);
                wfd = -1;
            }
        }
        if (pfds[ri].revents & (POLLIN | POLLHUP | POLLERR)) {
            char tmp[4096];
            ssize_t r = read(rfd, tmp, sizeof(tmp));
            if (r <= 0) {
                close(rfd);
                rfd = -1;
            } else {
                strbuf_append(&acc, tmp, (size_t)r);
            }
        }
    }
    if (wfd >= 0)
        close(wfd);

    signal(SIGPIPE, old_pipe);

    int status = 0;
    waitpid(pid, &status, 0);

    if (out)
        *out = strbuf_to_cstr(&acc);
    if (out_len)
        *out_len = acc.len;
    strbuf_free(&acc);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void term_cmd_free(char **lines, int count) {
    if (!lines)
        return;
    for (int i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
}
