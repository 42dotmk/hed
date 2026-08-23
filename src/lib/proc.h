#ifndef PROC_H
#define PROC_H

#include <sys/types.h>

/* Async child processes on pipes — the spawn half every plugin used
 * to hand-roll (lsp, copilot, translate, viewmd). Spawn only: callers
 * register from_fd with the select loop themselves. Stateless; no
 * editor dependencies beyond the log fd. */

typedef struct Proc {
    pid_t pid;
    int to_fd;   /* write end of the child's stdin; -1 unless PROC_STDIN */
    int from_fd; /* read end of the child's stdout; O_NONBLOCK unless
                    PROC_BLOCK_READ */
} Proc;

enum {
    PROC_STDIN = 1 << 0,       /* pipe to child stdin (else /dev/null) */
    PROC_STDERR_NULL = 1 << 1, /* stderr -> /dev/null (else editor log) */
    PROC_BLOCK_READ = 1 << 2,  /* leave from_fd blocking */
};

/* Fork + execvp argv (NULL-terminated). stdout lands on from_fd;
 * stderr goes to the editor log so child noise never repaints the
 * terminal. A child that dies within ~50ms (execvp failure — binary
 * not installed) is detected, reaped, and reported as -1 instead of
 * failing silently on the first write. Returns 0 and fills *out on
 * success. */
int proc_spawn(const char *const argv[], unsigned flags, Proc *out);

/* Close both fds (if open), send `sig` (0 = none), and reap without
 * blocking. Unregister from_fd from the select loop first. */
void proc_close(Proc *p, int sig);

#endif /* PROC_H */
