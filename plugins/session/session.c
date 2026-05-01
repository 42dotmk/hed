#include "hed.h"
#include "session.h"

EdError session_save(const char *path) {
    if (!path || !*path) return ED_ERR_INVALID_INDEX;

    FILE *f = fopen(path, "w");
    if (!f) return ED_ERR_INVALID_INDEX;

    for (size_t i = 0; i < E.buffers.len; i++) {
        const Buffer *b = &E.buffers.data[i];
        if (!b->filename || !*b->filename) continue;
        const char *prefix = ((int)i == E.current_buffer) ? "* " : "  ";
        fprintf(f, "%s%s\n", prefix, b->filename);
    }
    fclose(f);
    return ED_OK;
}

EdError session_restore(const char *path) {
    if (!path || !*path) return ED_ERR_INVALID_INDEX;

    FILE *f = fopen(path, "r");
    if (!f) return ED_ERR_INVALID_INDEX;

    int target = -1;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        if (n < 2) continue;

        int is_current = (line[0] == '*');
        const char *file = line + 2;  /* skip "* " or "  " */
        if (!*file) continue;

        buf_open_or_switch(file, false);
        if (is_current) target = E.current_buffer;
    }
    fclose(f);

    /* Close any empty, unnamed placeholder buffer left from startup.
     * Closing shifts higher indices down, so adjust target. */
    for (size_t i = 0; i < E.buffers.len; ) {
        Buffer *b = &E.buffers.data[i];
        if ((!b->filename || !*b->filename) && b->num_rows == 0 && !b->dirty) {
            if ((int)i < target) target--;
            else if ((int)i == target) target = -1;
            buf_close((int)i);
            continue;
        }
        i++;
    }

    if (target >= 0 && target < (int)E.buffers.len) {
        buf_switch(target);
    }
    return ED_OK;
}
