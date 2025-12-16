#include "hed.h"
#include "command_mode.h"
#include <dirent.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* --- Command-line (:) file path completion --- */

void cmdcomp_clear(void) {
    if (E.cmd_complete.items) {
        for (int i = 0; i < E.cmd_complete.count; i++)
            free(E.cmd_complete.items[i]);
        free(E.cmd_complete.items);
    }
    E.cmd_complete.items = NULL;
    E.cmd_complete.count = 0;
    E.cmd_complete.index = 0;
    E.cmd_complete.base[0] = '\0';
    E.cmd_complete.prefix[0] = '\0';
    E.cmd_complete.active = 0;
}

static void cmdcomp_apply_token(const char *replacement) {
    int len = E.command_len;
    int start = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (E.command_buf[i] == ' ') {
            start = i + 1;
            break;
        }
    }
    int rlen = (int)strlen(replacement);
    if (start + rlen >= (int)sizeof(E.command_buf))
        rlen = (int)sizeof(E.command_buf) - 1 - start;
    memcpy(E.command_buf + start, replacement, (size_t)rlen);
    E.command_len = start + rlen;
    E.command_buf[E.command_len] = '\0';
}

static void cmdcomp_build(void) {
    cmdcomp_clear();
    const char *home = getenv("HOME");
    int len = E.command_len;
    int start = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (E.command_buf[i] == ' ') {
            start = i + 1;
            break;
        }
    }
    char token[PATH_MAX];
    int tlen = len - start;
    if (tlen < 0)
        tlen = 0;
    if (tlen > (int)sizeof(token) - 1)
        tlen = (int)sizeof(token) - 1;
    memcpy(token, E.command_buf + start, (size_t)tlen);
    token[tlen] = '\0';
    /* Only start completion if token begins with '.', '~', or '/' */
    if (tlen == 0)
        return;
    char first = token[0];
    if (!(first == '.' || first == '~' || first == '/'))
        return;
    char full[PATH_MAX];
    if (token[0] == '~' && home) {
        if (token[1] == '/' || token[1] == '\0')
            snprintf(full, sizeof(full), "%s/%s", home,
                     token[1] ? token + 2 - 1 : "");
        else
            snprintf(full, sizeof(full), "%s", token); /* unsupported ~user */
    } else {
        snprintf(full, sizeof(full), "%s", token);
    }
    const char *slash = strrchr(full, '/');
    char base[PATH_MAX];
    char pref[PATH_MAX];
    if (slash) {
        size_t blen = (size_t)(slash - full + 1);
        if (blen >= sizeof(base))
            blen = sizeof(base) - 1;
        memcpy(base, full, blen);
        base[blen] = '\0';
        snprintf(pref, sizeof(pref), "%s", slash + 1);
    } else {
        base[0] = '\0';
        snprintf(pref, sizeof(pref), "%s", full);
    }
    DIR *d = opendir(base[0] ? base : ".");
    if (!d)
        return;
    struct dirent *de;
    int cap = 0;
    int count = 0;
    char **items = NULL;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.' && pref[0] != '.')
            continue;
        if (strncmp(name, pref, strlen(pref)) != 0)
            continue;
        int isdir = 0;
#ifdef DT_DIR
        if (de->d_type == DT_DIR)
            isdir = 1;
        if (de->d_type == DT_UNKNOWN)
#endif
        {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s%s", base[0] ? base : "", name);
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
                isdir = 1;
        }
        char cand[PATH_MAX];
        /* Include base path so tokens like "src/" complete to "src/<entry>" */
        snprintf(cand, sizeof(cand), "%s%s%s", base[0] ? base : "", name,
                 isdir ? "/" : "");
        if (count + 1 > cap) {
            cap = cap ? cap * 2 : 16;
            char **new_items = realloc(items, (size_t)cap * sizeof(char *));
            if (!new_items) {
                /* OOM: cleanup and abort completion */
                for (int i = 0; i < count; i++)
                    free(items[i]);
                free(items);
                closedir(d);
                return;
            }
            items = new_items;
        }
        char *cand_copy = strdup(cand);
        if (!cand_copy) {
            /* OOM: cleanup and abort completion */
            for (int i = 0; i < count; i++)
                free(items[i]);
            free(items);
            closedir(d);
            return;
        }
        items[count++] = cand_copy;
    }
    closedir(d);
    if (count == 0) {
        free(items);
        return;
    }
    E.cmd_complete.items = items;
    E.cmd_complete.count = count;
    E.cmd_complete.index = 0;
    snprintf(E.cmd_complete.base, sizeof(E.cmd_complete.base), "%s", base);
    snprintf(E.cmd_complete.prefix, sizeof(E.cmd_complete.prefix), "%s", pref);
    E.cmd_complete.active = 1;
    cmdcomp_apply_token(items[0]);
    ed_set_status_message("%d matches", count);
}

void cmdcomp_next(void) {
    if (!E.cmd_complete.active || E.cmd_complete.count == 0) {
        cmdcomp_build();
        return;
    }
    E.cmd_complete.index = (E.cmd_complete.index + 1) % E.cmd_complete.count;
    cmdcomp_apply_token(E.cmd_complete.items[E.cmd_complete.index]);
}

void ed_process_command(void) {
    if (E.command_len == 0) {
        ed_set_mode(MODE_NORMAL);
        return;
    }

    E.command_buf[E.command_len] = '\0';

    /* Parse command name and arguments */
    char *space = strchr(E.command_buf, ' ');
    char *cmd_name = E.command_buf;
    char *cmd_args = NULL;

    if (space) {
        *space = '\0'; /* Split command name and args */
        cmd_args = space + 1;
    }

    /* Execute command */
    log_msg(":%s%s%s", cmd_name, cmd_args ? " " : "", cmd_args ? cmd_args : "");
    if (!command_execute(cmd_name, cmd_args)) {
        if (space)
            *space = ' ';
        ed_set_status_message("Unknown command: %s", E.command_buf);
    } else {
        regs_set_cmd(E.command_buf, strlen(E.command_buf));
        hist_add(&E.history, E.command_buf);
    }

    if (E.stay_in_command) {
        E.stay_in_command = 0; /* consume flag: remain in command mode */
        /* do not clear command buffer; command likely prefilled it */
        E.mode = MODE_COMMAND;
    } else {
        ed_set_mode(MODE_NORMAL);
        E.command_len = 0;
        hist_reset_browse(&E.history);
        tmux_history_reset_browse();
        cmdcomp_clear();
    }
}
