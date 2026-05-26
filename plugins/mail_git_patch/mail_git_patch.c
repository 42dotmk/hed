/* mail_git_patch: turn the current repo's HEAD commit (or a custom
 * range) into a git-format-patch email and drop it into a compose
 * buffer so :mail-send can ship it.
 *
 * Runs `git format-patch <range> --stdout` from hed's cwd, strips the
 * mbox "From <sha> Mon Sep …" separator each patch starts with,
 * overrides the patch's From: line with the configured mail
 * from_addr (when set), inserts blank `To:` and `Cc:` headers, and
 * hands the resulting line array to mail_compose_with_lines. The
 * commit message + diffstat + diff become the compose body, so the
 * recipient receives something git-am can apply directly. */

#include "mail_git_patch.h"
#include "hed.h"
#include "mail/mail.h"
#include "input/command_mode.h"
#include "input/prompt.h"
#include "utils/term_cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Strip a single trailing space character from `s` (in place). git
 * format-patch indents commit-body lines with a leading space which
 * we want to keep — only trim CR/LF. */
static void rstrip_eol(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}

static void cmd_mail_git_patch(const char *args) {
    /* Verify we're inside a git work tree. `git rev-parse` prints
     * "true" / "false" and exits non-zero outside one. */
    if (term_cmd_system("git rev-parse --is-inside-work-tree "
                        ">/dev/null 2>&1") != 0) {
        ed_set_status_message("mail-git-patch: not a git repository");
        return;
    }

    /* Default: the last commit. Anything the user passes is forwarded
     * verbatim — `-3`, `origin/main..HEAD`, a single SHA, etc. */
    const char *range = (args && *args) ? args : "-1 HEAD";

    char cmd[512];
    int n = snprintf(cmd, sizeof(cmd),
                     "git format-patch --stdout %s 2>/dev/null", range);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        ed_set_status_message("mail-git-patch: range argument too long");
        return;
    }

    char **raw = NULL;
    int    raw_count = 0;
    term_cmd_capture(cmd, &raw, &raw_count);
    if (raw_count == 0) {
        ed_set_status_message(
            "mail-git-patch: git produced no output (bad range '%s'?)",
            range);
        term_cmd_free(raw, raw_count);
        return;
    }

    /* Skip the mbox "From <sha> Mon Sep 17 00:00:00 2001" separator
     * line that begins every format-patch message. If multiple
     * patches were produced, truncate at the next separator and warn —
     * a single compose buffer can't really represent a patch series,
     * and `git send-email` is the right tool for that case. */
    int start = 0;
    if (raw[0] && strncmp(raw[0], "From ", 5) == 0 &&
        strstr(raw[0], "@") == NULL) {
        start = 1;
    }

    int end = raw_count;
    int extra_patches = 0;
    for (int i = start + 1; i < raw_count; i++) {
        const char *l = raw[i] ? raw[i] : "";
        /* mbox separator: "From <40-hex> <weekday> ...". @ never
         * appears, distinguishing it from a `From:` header. */
        if (strncmp(l, "From ", 5) == 0 && strchr(l, '@') == NULL &&
            strlen(l) > 50) {
            end = i;
            extra_patches = 1;
            break;
        }
    }

    /* Build the compose lines. Headers first, then a blank line, then
     * the body (commit message + diffstat + diff). We rewrite From:,
     * inject To:/Cc:, and otherwise preserve format-patch output. */
    int cap = (end - start) + 6;
    char **out = calloc((size_t)cap, sizeof(*out));
    if (!out) {
        term_cmd_free(raw, raw_count);
        ed_set_status_message("mail-git-patch: out of memory");
        return;
    }

    const char *from = mail_get_from();
    int wrote_from = 0, wrote_to = 0, wrote_cc = 0;
    int in_header  = 1;
    int n_out      = 0;

    for (int i = start; i < end; i++) {
        const char *l = raw[i] ? raw[i] : "";
        char *line = strdup(l);
        if (!line) break;
        rstrip_eol(line);

        if (in_header) {
            if (line[0] == '\0') {
                /* End of header block — inject the headers we owe. */
                if (!wrote_from) {
                    char fl[512];
                    snprintf(fl, sizeof(fl), "From: %s",
                             from[0] ? from : "");
                    out[n_out++] = strdup(fl);
                }
                if (!wrote_to) out[n_out++] = strdup("To: ");
                if (!wrote_cc) out[n_out++] = strdup("Cc: ");
                out[n_out++] = line;     /* the blank separator */
                in_header = 0;
                continue;
            }
            if (strncasecmp(line, "From:", 5) == 0 && from[0]) {
                /* Replace git's From: with the configured mail
                 * sender so msmtp's account-by-from-header lookup
                 * works the same as for compose/reply/forward. */
                free(line);
                char fl[512];
                snprintf(fl, sizeof(fl), "From: %s", from);
                out[n_out++] = strdup(fl);
                wrote_from = 1;
                continue;
            }
            if (strncasecmp(line, "From:",    5) == 0) wrote_from = 1;
            if (strncasecmp(line, "To:",      3) == 0) wrote_to   = 1;
            if (strncasecmp(line, "Cc:",      3) == 0) wrote_cc   = 1;
        }
        out[n_out++] = line;
    }
    /* Defensive: if the patch had no body separator we still need a
     * landing spot for the cursor. */
    if (in_header) {
        if (!wrote_from) {
            char fl[512];
            snprintf(fl, sizeof(fl), "From: %s", from[0] ? from : "");
            out[n_out++] = strdup(fl);
        }
        if (!wrote_to) out[n_out++] = strdup("To: ");
        if (!wrote_cc) out[n_out++] = strdup("Cc: ");
        out[n_out++] = strdup("");
    }

    mail_compose_with_lines("Patch", out, n_out);

    for (int i = 0; i < n_out; i++) free(out[i]);
    free(out);
    term_cmd_free(raw, raw_count);

    if (extra_patches)
        ed_set_status_message(
            "mail-git-patch: range produced multiple patches — only the "
            "first is in the compose; use git send-email for a series");
    else
        ed_set_status_message(
            "mail-git-patch: patch loaded — fill in To:, :mail-send to ship");
}

/* ----------------------------------------------------------------- */
/* `git am` — apply a patch email back into the cwd repo               */
/* ----------------------------------------------------------------- */

/* True if `s` looks like a flag (starts with '-'), false if it looks
 * like a path or freeform text. */
static int looks_like_flag(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s[0] == '-';
}

/* :mail-git-am [args]
 *
 * No args, in a mail-message buffer → pipe the current notmuch thread
 *   as mbox into `git am` (the natural way to apply a patch email or
 *   patch-series email you're reading).
 * Args starting with `-` (e.g. `--3way`, `--abort`, `--continue`,
 *   `--skip`) → flags are forwarded to git am; if the buffer is a
 *   mail-message we still feed the notmuch mbox on stdin, otherwise
 *   git am runs standalone (for `--continue` / `--abort` after a
 *   conflict).
 * Anything else → treated as a path / extra git-am positional arg
 *   (`:mail-git-am /tmp/foo.patch`). */
static void cmd_mail_git_am(const char *args) {
    if (term_cmd_system("git rev-parse --is-inside-work-tree "
                        ">/dev/null 2>&1") != 0) {
        ed_set_status_message("mail-git-am: not a git repository");
        return;
    }

    const char *arg = args ? args : "";
    while (*arg == ' ' || *arg == '\t') arg++;

    int use_buffer_mbox = 0;
    const char *tid = NULL;

    if (!*arg || looks_like_flag(arg)) {
        Buffer *buf = buf_cur();
        if (buf && buf->filename && buf->filetype &&
            strcmp(buf->filetype, "mail-message") == 0 &&
            strncmp(buf->filename, "mail://", 7) == 0) {
            tid             = buf->filename + 7;
            use_buffer_mbox = 1;
        } else if (!*arg) {
            ed_set_status_message(
                "mail-git-am: open a patch message, pass a file, "
                "or run with --continue/--abort after a conflict");
            return;
        }
        /* If we got here with flags but no mail-message buffer, fall
         * through to a standalone `git am <flags>` invocation —
         * supports `:mail-git-am --continue` from anywhere. */
    }

    char cmd[1024];
    int  n;
    if (use_buffer_mbox) {
        /* Thread ids from notmuch are alnum + ':' — safe to single-
         * quote without further escaping. */
        n = snprintf(cmd, sizeof(cmd),
                     "notmuch show --format=mbox -- '%s' | git am%s%s",
                     tid, *arg ? " " : "", arg);
    } else {
        n = snprintf(cmd, sizeof(cmd), "git am%s%s",
                     *arg ? " " : "", arg);
    }
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        ed_set_status_message("mail-git-am: command too long");
        return;
    }

    /* Run interactive so the user sees git am's progress / conflict
     * output and can resolve in place. `acknowledge=true` waits for
     * Enter before returning to hed so the transcript isn't blown
     * away by the next render. */
    int rc = term_cmd_run_interactive(cmd, true);
    if (rc == 0) {
        ed_set_status_message("mail-git-am: applied");
    } else if (rc == -1) {
        ed_set_status_message("mail-git-am: failed to launch git am");
    } else {
        ed_set_status_message(
            "mail-git-am: git am exited %d "
            "(resolve and `:mail-git-am --continue`, or `--abort`)",
            rc);
    }
    ed_render_frame();
}

void kb_mail_git_patch_prompt(void) {
    /* Open a fresh : prompt and seed it with the command + a trailing
     * space so the user lands ready to type extra git-format-patch
     * args (e.g. `-3`, `--cover-letter origin/main..HEAD`). Pressing
     * Enter immediately runs the default `-1 HEAD`. */
    cmd_prompt_open();
    Prompt *p = prompt_current();
    if (!p) return;
    static const char pref[] = "mail-git-patch ";
    prompt_set_text(p, pref, (int)(sizeof(pref) - 1));
    ed_set_status_message(":%s", p->buf);
}

static int mail_git_patch_init(void) {
    cmd("mail-git-patch", cmd_mail_git_patch,
        "compose a git format-patch email of the current repo "
        "(default range: -1 HEAD)");
    cmd("mail-git-am",    cmd_mail_git_am,
        "apply the current patch email (or a file) via `git am`");
    return 0;
}

const Plugin plugin_mail_git_patch = {
    .name   = "mail_git_patch",
    .desc   = "compose an email from `git format-patch` output of the cwd repo",
    .init   = mail_git_patch_init,
    .deinit = NULL,
};
