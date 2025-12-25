#include "hed.h"

#ifndef CMD_HISTORY_MAX
#define CMD_HISTORY_MAX 1000
#endif

static const char *history_filename = ".hed_history"; /* $HOME fallback */

static char *hist_path(void) {
    const char *home = getenv("HOME");
    if (home && *home) {
        size_t len = strlen(home) + 1 + strlen(history_filename) + 1;
        char *p = malloc(len);
        if (!p)
            return NULL;
        snprintf(p, len, "%s/%s", home, history_filename);
        return p;
    }
    return strdup(history_filename);
}

static void hist_clear_items(CmdHistory *h) {
    if (!h)
        return;
    for (size_t i = 0; i < h->items.len; i++)
        free(h->items.data[i]);
    free(h->items.data);
    h->items.data = NULL;
    h->items.len = 0;
    h->items.cap = 0;
}

static void hist_insert_front(CmdHistory *h, const char *line) {
    if (!line || !*line)
        return;

    char *line_copy = strdup(line);
    vec_push_start_typed(&h->items, char *, line_copy);

    /* Enforce max limit */
    if (h->items.len > CMD_HISTORY_MAX) {
        char *oldest = vec_pop_typed(&h->items, char *);
        free(oldest);
    }
}

static void hist_append(CmdHistory *h, const char *line) {
    if (!line || !*line)
        return;
    if ((int)h->items.len >= CMD_HISTORY_MAX)
        return;

    char *line_copy = strdup(line);
    vec_push_typed(&h->items, char *, line_copy);
}

static void hist_prepend_to_file(const char *line) {
    if (!line || !*line)
        return;
    char *path = hist_path();
    if (!path)
        return;

    FILE *fp = fopen(path, "r");
    char *old = NULL;
    size_t oldsz = 0;
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        if (sz > 0) {
            fseek(fp, 0, SEEK_SET);
            old = malloc((size_t)sz + 1);
            if (old) {
                fread(old, 1, (size_t)sz, fp);
                old[sz] = '\0';
                oldsz = (size_t)sz;
            }
        }
        fclose(fp);
    }

    size_t llen = strlen(line);
    size_t keep_bytes = 0;
    if (old && oldsz > 0) {
        int lines_kept = 0;
        const char *p = old;
        const char *end = old + oldsz;
        while (p < end && lines_kept < CMD_HISTORY_MAX - 1) {
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            if (!nl) {
                keep_bytes = oldsz;
                lines_kept++;
                break;
            }
            keep_bytes = (size_t)((nl - old) + 1);
            p = nl + 1;
            lines_kept++;
        }
    }
    size_t newsz = llen + 1 + keep_bytes;
    char *buf = malloc(newsz + 1);
    if (!buf) {
        free(old);
        free(path);
        return;
    }
    memcpy(buf, line, llen);
    buf[llen] = '\n';
    if (keep_bytes)
        memcpy(buf + llen + 1, old, keep_bytes);
    buf[newsz] = '\0';

    char tmppath[4096];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);
    FILE *tfp = fopen(tmppath, "w");
    if (tfp) {
        fwrite(buf, 1, newsz, tfp);
        fclose(tfp);
        rename(tmppath, path);
    } else {
        tfp = fopen(path, "w");
        if (tfp) {
            fwrite(buf, 1, newsz, tfp);
            fclose(tfp);
        }
    }
    free(buf);
    free(old);
    free(path);
}

void hist_init(CmdHistory *h) {
    if (!h)
        return;
    h->items.data = NULL;
    h->items.len = 0;
    h->items.cap = 0;
    h->idx = -1;
    h->saved_len = 0;
    h->prefix_len = 0;
    h->saved_line[0] = '\0';
    h->prefix[0] = '\0';

    char *path = hist_path();
    if (!path)
        return;
    FILE *fp = fopen(path, "r");
    if (!fp) {
        free(path);
        return;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t r;
    while ((r = getline(&line, &cap, fp)) != -1) {
        while (r > 0 && (line[r - 1] == '\n' || line[r - 1] == '\r'))
            r--;
        line[r] = '\0';
        hist_append(h, line);
        if ((int)h->items.len >= CMD_HISTORY_MAX)
            break;
    }
    free(line);
    fclose(fp);
    free(path);
}

void hist_free(CmdHistory *h) { hist_clear_items(h); }

void hist_add(CmdHistory *h, const char *line) {
    if (!h)
        return;
    hist_insert_front(h, line);
    hist_prepend_to_file(line);
}

void hist_reset_browse(CmdHistory *h) {
    if (!h)
        return;
    h->idx = -1;
    h->saved_len = 0;
    h->saved_line[0] = '\0';
    h->prefix_len = 0;
    h->prefix[0] = '\0';
}

static int hist_prefix_match(const char *entry, const char *pre, int plen) {
    if (!entry)
        return 0;
    for (int i = 0; i < plen; i++) {
        if (!entry[i] || entry[i] != pre[i])
            return 0;
    }
    return 1;
}

int hist_browse_up(CmdHistory *h, const char *current_input, int current_len,
                   char *out, int out_cap) {
    if (!h || h->items.len == 0 || out_cap <= 0)
        return 0;
    if (h->idx == -1) {
        h->saved_len = current_len;
        if (h->saved_len > (int)sizeof(h->saved_line) - 1)
            h->saved_len = (int)sizeof(h->saved_line) - 1;
        memcpy(h->saved_line, current_input, h->saved_len);
        h->saved_line[h->saved_len] = '\0';
        h->prefix_len = current_len;
        if (h->prefix_len > (int)sizeof(h->prefix) - 1)
            h->prefix_len = (int)sizeof(h->prefix) - 1;
        memcpy(h->prefix, current_input, h->prefix_len);
        h->prefix[h->prefix_len] = '\0';
    }
    int start = (h->idx == -1) ? 0 : h->idx + 1;
    for (int i = start; i < (int)h->items.len; i++) {
        const char *s = h->items.data[i];
        if (hist_prefix_match(s, h->prefix, h->prefix_len)) {
            h->idx = i;
            int n = (int)strlen(s);
            if (n > out_cap - 1)
                n = out_cap - 1;
            memcpy(out, s, n);
            out[n] = '\0';
            return 1;
        }
    }
    return 0;
}

int hist_browse_down(CmdHistory *h, char *out, int out_cap, int *restored) {
    if (restored)
        *restored = 0;
    if (!h || h->items.len == 0 || out_cap <= 0)
        return 0;
    if (h->idx == -1)
        return 0;
    for (int i = h->idx - 1; i >= 0; i--) {
        const char *s = h->items.data[i];
        if (hist_prefix_match(s, h->prefix, h->prefix_len)) {
            h->idx = i;
            int n = (int)strlen(s);
            if (n > out_cap - 1)
                n = out_cap - 1;
            memcpy(out, s, n);
            out[n] = '\0';
            return 1;
        }
    }
    /* Restore saved input and exit browsing */
    h->idx = -1;
    int n = h->saved_len;
    if (n > out_cap - 1)
        n = out_cap - 1;
    memcpy(out, h->saved_line, n);
    out[n] = '\0';
    if (restored)
        *restored = 1;
    return 1;
}

int hist_len(const CmdHistory *h) { return h ? (int)h->items.len : 0; }
const char *hist_get(const CmdHistory *h, int idx) {
    if (!h || idx < 0 || idx >= (int)h->items.len)
        return NULL;
    return h->items.data[idx];
}
