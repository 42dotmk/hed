/* Built-in completion sources.
 *
 * words: identifiers harvested from every open non-readonly buffer
 * that share the typed prefix (case-insensitive, min 2 chars typed,
 * min 3 char words). The editor-native fallback when no smarter
 * source applies; duplicates of richer items are deduped by the menu.
 *
 * path: filesystem entries, triggered by '/'. The token before the
 * cursor is parsed as a path — absolute, ~/, or relative to the
 * buffer's directory (cwd for unnamed buffers). Directories get a
 * trailing '/' and sort first. The replacement range covers the
 * basename being typed via CmpItem.edit_start.
 *
 * Both sources deliver synchronously from request(); per the
 * completion_provide contract, no Buffer pointers are touched after
 * the call. */

#include "completion/completion_sources.h"
#include "completion/completion.h"
#include "hed.h"
#include <dirent.h>
#include <sys/stat.h>

#define WORDS_MIN_PREFIX 2
#define WORDS_MIN_LEN 3
#define WORDS_MAX_LEN 64
#define WORDS_MAX 100
#define WORDS_ROW_BUDGET 50000

#define PATH_MAX_ITEMS 200

static int src_is_word_byte(unsigned char c) {
    return c == '_' || isalnum(c) || c >= 128;
}

/* ------------------------------------------------------ buffer words */

static int words_available(Buffer *buf) {
    (void)buf;
    return 1;
}

static void words_request(Buffer *buf, int line, int col, unsigned token) {
    if (line < 0 || line >= buf->num_rows) {
        completion_provide(token, NULL, 0);
        return;
    }
    const char *s = buf->rows[line].chars.data;
    int ws = col;
    while (ws > 0 && src_is_word_byte((unsigned char)s[ws - 1]))
        ws--;
    size_t plen = (size_t)(col - ws);
    if (plen < WORDS_MIN_PREFIX || plen >= WORDS_MAX_LEN) {
        completion_provide(token, NULL, 0);
        return;
    }
    char prefix[WORDS_MAX_LEN];
    memcpy(prefix, s + ws, plen);
    prefix[plen] = '\0';

    char *words[WORDS_MAX];
    int nwords = 0;
    int budget = WORDS_ROW_BUDGET;
    for (ptrdiff_t bi = 0; bi < arrlen(E.buffers) && nwords < WORDS_MAX; bi++) {
        Buffer *b = &E.buffers[bi];
        if (b->readonly)
            continue; /* display buffers (mail, dired, ...) are noise */
        for (int r = 0; r < b->num_rows && nwords < WORDS_MAX && budget-- > 0;
             r++) {
            const char *p = b->rows[r].chars.data;
            size_t len = b->rows[r].chars.len;
            size_t i = 0;
            while (i < len) {
                if (!src_is_word_byte((unsigned char)p[i])) {
                    i++;
                    continue;
                }
                size_t j = i;
                while (j < len && src_is_word_byte((unsigned char)p[j]))
                    j++;
                size_t wlen = j - i;
                /* identifier-shaped (not a bare number), long enough,
                 * shares the prefix, and isn't the prefix itself */
                if (wlen >= WORDS_MIN_LEN && wlen < WORDS_MAX_LEN &&
                    !isdigit((unsigned char)p[i]) &&
                    strncasecmp(p + i, prefix, plen) == 0 &&
                    !(wlen == plen && strncmp(p + i, prefix, plen) == 0)) {
                    int dup = 0;
                    for (int k = 0; k < nwords && !dup; k++)
                        dup = strlen(words[k]) == wlen &&
                              strncmp(words[k], p + i, wlen) == 0;
                    if (!dup) {
                        char *w = malloc(wlen + 1);
                        if (w) {
                            memcpy(w, p + i, wlen);
                            w[wlen] = '\0';
                            words[nwords++] = w;
                        }
                    }
                }
                i = j;
            }
        }
    }
    if (nwords == 0) {
        completion_provide(token, NULL, 0);
        return;
    }
    CmpItem *items = calloc((size_t)nwords, sizeof(CmpItem));
    if (!items) {
        for (int k = 0; k < nwords; k++)
            free(words[k]);
        return;
    }
    for (int k = 0; k < nwords; k++) {
        items[k].label = words[k];
        items[k].kind = CMP_KIND_TEXT;
        items[k].edit_start = -1;
        items[k].edit_end = -1;
    }
    completion_provide(token, items, nwords);
}

static const CompletionSource words_source = {
    .name = "words",
    .available = words_available,
    .request = words_request,
};

/* ------------------------------------------------- filesystem paths */

/* Bytes that end a path token when scanning left from the cursor. */
static int path_is_stop(unsigned char c) {
    return c <= ' ' || strchr("\"'`<>(){}[],;=", c) != NULL;
}

static int path_available(Buffer *buf) {
    (void)buf;
    return 1;
}

static int path_is_trigger(Buffer *buf, int c) {
    (void)buf;
    return c == '/';
}

static void path_request(Buffer *buf, int line, int col, unsigned token) {
    if (line < 0 || line >= buf->num_rows) {
        completion_provide(token, NULL, 0);
        return;
    }
    const char *s = buf->rows[line].chars.data;
    int ts = col;
    while (ts > 0 && !path_is_stop((unsigned char)s[ts - 1]))
        ts--;
    int last_slash = -1;
    for (int i = ts; i < col; i++)
        if (s[i] == '/')
            last_slash = i;
    if (last_slash < 0) {
        completion_provide(token, NULL, 0);
        return;
    }
    /* A bare "/" is almost always division; only treat it as the
     * filesystem root at line start or after a quote-ish opener. */
    if (col - ts == 1 &&
        !(ts == 0 || strchr("\"'<", (unsigned char)s[ts - 1]))) {
        completion_provide(token, NULL, 0);
        return;
    }

    const char *base = s + last_slash + 1;
    size_t blen = (size_t)(col - last_slash - 1);

    /* Resolve the directory part [ts, last_slash]. Truncation of an
     * over-long path is harmless — opendir just fails and no items
     * are offered. */
    char dir[2048];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    size_t dplen = (size_t)(last_slash - ts + 1); /* incl. trailing '/' */
    if (s[ts] == '~') {
        const char *home = getenv("HOME");
        if (!home || dplen < 2) {
            completion_provide(token, NULL, 0);
            return;
        }
        snprintf(dir, sizeof(dir), "%s%.*s", home, (int)(dplen - 1),
                 s + ts + 1);
    } else if (s[ts] == '/') {
        snprintf(dir, sizeof(dir), "%.*s", (int)dplen, s + ts);
    } else {
        /* Relative to the buffer's own directory, like :e does from a
         * file's point of view; unnamed buffers fall back to cwd. */
        char basedir[1024];
        if (buf->filename && strchr(buf->filename, '/')) {
            const char *sl = strrchr(buf->filename, '/');
            snprintf(basedir, sizeof(basedir), "%.*s",
                     (int)(sl - buf->filename), buf->filename);
        } else {
            snprintf(basedir, sizeof(basedir), "%s", E.cwd);
        }
        snprintf(dir, sizeof(dir), "%s/%.*s", basedir, (int)dplen, s + ts);
    }
#pragma GCC diagnostic pop

    DIR *d = opendir(dir);
    if (!d) {
        completion_provide(token, NULL, 0);
        return;
    }
    CmpItem *items = NULL;
    int n = 0;
    struct dirent *de;
    while (n < PATH_MAX_ITEMS && (de = readdir(d))) {
        const char *name = de->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        if (name[0] == '.' && (blen == 0 || base[0] != '.'))
            continue; /* hidden unless explicitly asked for */
        if (blen > 0 && strncasecmp(name, base, blen) != 0)
            continue;
        int is_dir = 0;
        if (de->d_type == DT_DIR) {
            is_dir = 1;
        } else if (de->d_type == DT_UNKNOWN || de->d_type == DT_LNK) {
            char full[2304];
            struct stat st;
            /* Truncation harmless: stat on a clipped path fails and the
             * entry is just treated as a file. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            snprintf(full, sizeof(full), "%s/%s", dir, name);
#pragma GCC diagnostic pop
            is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        }
        CmpItem it = {0};
        size_t nl = strlen(name);
        it.label = malloc(nl + 2);
        it.sort_text = malloc(nl + 2);
        if (!it.label || !it.sort_text) {
            free(it.label);
            free(it.sort_text);
            break;
        }
        memcpy(it.label, name, nl);
        it.label[nl] = is_dir ? '/' : '\0';
        it.label[nl + 1] = '\0';
        it.sort_text[0] = is_dir ? '0' : '1'; /* directories first */
        memcpy(it.sort_text + 1, name, nl + 1);
        it.kind = is_dir ? CMP_KIND_DIR : CMP_KIND_FILE;
        it.edit_start = last_slash + 1; /* replace the basename */
        it.edit_end = -1;
        if (!items)
            items = calloc(PATH_MAX_ITEMS, sizeof(CmpItem));
        if (!items) {
            free(it.label);
            free(it.sort_text);
            break;
        }
        items[n++] = it;
    }
    closedir(d);
    completion_provide(token, items, n);
}

static const CompletionSource path_source = {
    .name = "path",
    .available = path_available,
    .is_trigger_char = path_is_trigger,
    .request = path_request,
};

void completion_sources_register(void) {
    completion_source_register(&words_source);
    completion_source_register(&path_source);
}
