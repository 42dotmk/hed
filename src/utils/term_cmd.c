#include "hed.h"
#include "term_cmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper: trim trailing newline/carriage return */
static void trim_eol(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

int term_cmd_run(const char *cmd, char ***out_lines, int *out_count) {
    if (!cmd) return 0;
    if (out_lines) *out_lines = NULL;
    if (out_count) *out_count = 0;

    /* Disable raw mode for command execution */
    disable_raw_mode();

    /* Open pipe to command */
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        enable_raw_mode();
        return 0;
    }

    /* Capture output lines */
    int capacity = 0;
    int count = 0;
    char **lines = NULL;
    char line_buf[2048];

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        trim_eol(line_buf);

        /* Grow array if needed */
        if (count + 1 > capacity) {
            capacity = capacity ? capacity * 2 : 8;
            char **new_lines = realloc(lines, (size_t)capacity * sizeof(char*));
            if (!new_lines) {
                /* OOM: cleanup and fail */
                for (int i = 0; i < count; i++) free(lines[i]);
                free(lines);
                pclose(fp);
                enable_raw_mode();
                return 0;
            }
            lines = new_lines;
        }

        /* Duplicate line */
        char *line_copy = strdup(line_buf);
        if (!line_copy) {
            /* OOM: cleanup and fail */
            for (int i = 0; i < count; i++) free(lines[i]);
            free(lines);
            pclose(fp);
            enable_raw_mode();
            return 0;
        }

        lines[count++] = line_copy;
    }

    pclose(fp);
    enable_raw_mode();

    /* Return results */
    if (out_lines) {
        *out_lines = lines;
    } else {
        /* Caller doesn't want output, free it */
        for (int i = 0; i < count; i++) free(lines[i]);
        free(lines);
    }

    if (out_count) *out_count = count;
    return 1;
}

int term_cmd_run_interactive(const char *cmd) {
    if (!cmd) return -1;

    /* Disable raw mode for interactive command */
    disable_raw_mode();

    /* Run command via system() - user can interact */
    int status = system(cmd);

    /* Wait for user to acknowledge before returning to editor UI */
    fprintf(stdout,
            "\n\n[command finished with status %d] "
            "Press Enter to return to hed...", status);
    fflush(stdout);
    int ch;
    /* Consume until newline or EOF */
    while ((ch = getchar()) != '\n' && ch != EOF) {
        /* discard */
    }

    enable_raw_mode();
    return status;
}

void term_cmd_free(char **lines, int count) {
    if (!lines) return;
    for (int i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
}
