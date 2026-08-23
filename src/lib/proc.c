#include "lib/proc.h"
#include "lib/log.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int proc_spawn(const char *const argv[], unsigned flags, Proc *out) {
    if (!argv || !argv[0] || !out)
        return -1;
    out->pid = 0;
    out->to_fd = -1;
    out->from_fd = -1;

    int in_pipe[2] = {-1, -1};
    int out_pipe[2];
    if ((flags & PROC_STDIN) && pipe(in_pipe) != 0)
        return -1;
    if (pipe(out_pipe) != 0) {
        if (in_pipe[0] >= 0) {
            close(in_pipe[0]);
            close(in_pipe[1]);
        }
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (in_pipe[0] >= 0) {
            close(in_pipe[0]);
            close(in_pipe[1]);
        }
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* child */
        if (flags & PROC_STDIN) {
            dup2(in_pipe[0], STDIN_FILENO);
        } else {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }
        dup2(out_pipe[1], STDOUT_FILENO);

        int errfd = (flags & PROC_STDERR_NULL) ? -1 : log_fileno();
        if (errfd < 0)
            errfd = open("/dev/null", O_WRONLY);
        if (errfd >= 0)
            dup2(errfd, STDERR_FILENO);

        if (in_pipe[0] >= 0) {
            close(in_pipe[0]);
            close(in_pipe[1]);
        }
        close(out_pipe[0]);
        close(out_pipe[1]);

        execvp(argv[0], (char *const *)argv);
        /* execvp returned → not on $PATH or otherwise unrunnable. The
         * parent notices via the immediate-death check below; stderr
         * already points at the log. */
        fprintf(stderr, "proc: execvp(%s) failed: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }

    /* parent */
    if (in_pipe[0] >= 0)
        close(in_pipe[0]);
    close(out_pipe[1]);

    /* Immediate-death check: a missing binary otherwise fails silently
     * on the first write to the pipe. */
    struct timespec ts = {0, 50 * 1000 * 1000}; /* 50 ms */
    nanosleep(&ts, NULL);
    int wstatus = 0;
    if (waitpid(pid, &wstatus, WNOHANG) == pid) {
        if (in_pipe[1] >= 0)
            close(in_pipe[1]);
        close(out_pipe[0]);
        log_msg("proc: child '%s' exited immediately (status=%d) — binary "
                "missing?",
                argv[0], wstatus);
        return -1;
    }

    if (!(flags & PROC_BLOCK_READ)) {
        int fl = fcntl(out_pipe[0], F_GETFL, 0);
        if (fl >= 0)
            fcntl(out_pipe[0], F_SETFL, fl | O_NONBLOCK);
    }

    out->pid = pid;
    out->to_fd = in_pipe[1];
    out->from_fd = out_pipe[0];
    log_msg("proc: spawned %s (pid %d)", argv[0], (int)pid);
    return 0;
}

void proc_close(Proc *p, int sig) {
    if (!p)
        return;
    if (p->from_fd >= 0) {
        close(p->from_fd);
        p->from_fd = -1;
    }
    if (p->to_fd >= 0) {
        close(p->to_fd);
        p->to_fd = -1;
    }
    if (p->pid > 0) {
        if (sig)
            kill(p->pid, sig);
        waitpid(p->pid, NULL, WNOHANG);
        p->pid = 0;
    }
}
