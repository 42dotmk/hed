#include "hed.h"
#include "session.h"

EdError session_save(const char *path) {
    if (!path || !*path) return ED_ERR_INVALID_INDEX;

    /* Compose the whole session into one buffer, then write atomically. */
    size_t cap = 0, len = 0;
    char  *txt = NULL;
    for (ptrdiff_t i = 0; i < arrlen(E.buffers); i++) {
        const Buffer *b = &E.buffers[i];
        if (!b->filename || !*b->filename) continue;
        size_t need = 2 + strlen(b->filename) + 2;
        if (len + need > cap) {
            cap = cap ? cap * 2 : 1024;
            while (len + need > cap) cap *= 2;
            char *n = realloc(txt, cap);
            if (!n) { free(txt); return ED_ERR_NOMEM; }
            txt = n;
        }
        const char *prefix = ((int)i == E.current_buffer) ? "* " : "  ";
        int w = snprintf(txt + len, cap - len, "%s%s\n", prefix, b->filename);
        if (w < 0) { free(txt); return ED_ERR_FILE_WRITE; }
        len += (size_t)w;
    }

    EdError err = fs_file_write_atomic(path, txt ? txt : "", len);
    free(txt);
    return err == ED_OK ? ED_OK : ED_ERR_INVALID_INDEX;
}

EdError session_restore(const char *path) {
    if (!path || !*path) return ED_ERR_INVALID_INDEX;

    FsLines *r = NULL;
    if (fs_lines_open(&r, path) != ED_OK) return ED_ERR_INVALID_INDEX;

    int target = -1;
    const char *line;
    size_t      n;
    while (fs_lines_next(r, &line, &n)) {
        if (n < 2) continue;
        int is_current = (line[0] == '*');
        const char *file = line + 2;  /* skip "* " or "  " */
        if (!*file) continue;

        buf_open_or_switch(file, false);
        if (is_current) target = E.current_buffer;
    }
    fs_lines_close(r);

    /* Close any empty, unnamed placeholder buffer left from startup.
     * Closing shifts higher indices down, so adjust target. */
    for (ptrdiff_t i = 0; i < arrlen(E.buffers); ) {
        Buffer *b = &E.buffers[i];
        if ((!b->filename || !*b->filename) && b->num_rows == 0 && !b->dirty) {
            if ((int)i < target) target--;
            else if ((int)i == target) target = -1;
            buf_close((int)i);
            continue;
        }
        i++;
    }

    if (target >= 0 && target < (int)arrlen(E.buffers)) {
        buf_switch(target);
    }
    return ED_OK;
}
