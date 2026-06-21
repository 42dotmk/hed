/* tasks plugin: a literate-programming task tracker living entirely in
 * markdown. The document IS the task tree — no database, no sidecar.
 *
 * Format
 * ------
 *   ## [IN-PROGRESS] Wire keybinds      <- status = [KEYWORD] after the #s
 *   deadline:: 2026-06-10               <- recognized fields (see FIELDS[])
 *   prio:: A
 *   owner:: costa
 *
 *   Prose under the heading is the literate description: narrative,
 *   code fences, sub-lists — read top to bottom.
 *
 *   - 2026-06-08 14:30 split the parser out      <- dated log (:task_note)
 *
 *   ## [DONE] OSC52 clipboard
 *   completed:: 2026-06-08              <- stamped automatically on close
 *
 * A heading with no [KEYWORD] is just a heading, not a task.
 *
 * Recognized fields (FIELDS[]) get meaning: date fields are validated and
 * accept relative input (`today`, `+3d`, `+2w`); `prio` takes A/B/C. The
 * highlighter colours the [KEYWORD] and recognized field values; the
 * agenda sorts open tasks by deadline (overdue first) then priority.
 *
 * Surface
 * -------
 *   :task_cycle           rotate status: (none)->TODO->IN-PROGRESS->
 *                         BLOCKED->DONE->(none).            <space>mc
 *   :task_status <name>   set status directly (incl. cancelled|none).
 *   :task_deadline [date] set/clear deadline:: (date arg or today).
 *   :task_schedule [date] set/clear schedule::.
 *   :task_prio <A|B|C>    set/clear prio::.
 *   :task_field k [v]     upsert any field k (empty v clears it).
 *   :task_note <text>     append a dated log bullet to the section.
 *   :task_agenda          open tasks across the tree -> quickfix,
 *                         sorted by deadline+prio, overdue flagged. <space>ma
 *
 * Known limitation: heading/field detection doesn't track fenced code
 * blocks, so `# [TODO]`/`key:: v` lines inside a ``` fence are also
 * recognized. The conventions make that rare. */

#include "hed.h"
#include "input/command_mode.h"  /* cmd_prompt_open */
#include "lib/path_limits.h"     /* PATH_MAX */
#include "markdown/markdown_fields.h"  /* MdFieldDef, md_parse_field, ... */
#include <time.h>

/* Row-array mutators live in buf/buffer.c with no public header — the
 * core forward-declares them locally (see buf_helpers.c). Do the same. */

/* --- status table ----------------------------------------------------- */

typedef struct {
    const char *name;
    const char *sgr;  /* render colour for the [KEYWORD] token */
    int         closed;
} Status;

static const Status STATUS[] = {
    {"TODO",        "\x1b[1;38;2;224;175;104m", 0}, /* amber  */
    {"IN-PROGRESS", "\x1b[1;38;2;122;162;247m", 0}, /* blue   */
    {"BLOCKED",     "\x1b[1;38;2;247;118;142m", 0}, /* red    */
    {"DONE",        "\x1b[1;38;2;158;206;106m", 1}, /* green  */
    {"CANCELLED",   "\x1b[9;38;2;86;95;137m",   1}, /* strike */
};
#define N_STATUS ((int)(sizeof(STATUS) / sizeof(STATUS[0])))

enum { IDX_TODO = 0, IDX_INPROG, IDX_BLOCKED, IDX_DONE, IDX_CANCELLED };

static const int CYCLE[] = {IDX_TODO, IDX_INPROG, IDX_BLOCKED, IDX_DONE};
#define N_CYCLE ((int)(sizeof(CYCLE) / sizeof(CYCLE[0])))

static int status_lookup(const char *s, int len) {
    for (int i = 0; i < N_STATUS; i++)
        if ((int)strlen(STATUS[i].name) == len &&
            strncmp(STATUS[i].name, s, (size_t)len) == 0)
            return i;
    return -1;
}

/* -1 = none/clear, -2 = unrecognised. */
static int status_parse(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || strcasecmp(s, "none") == 0 || strcasecmp(s, "clear") == 0)
        return -1;
    for (int i = 0; i < N_STATUS; i++)
        if (strcasecmp(STATUS[i].name, s) == 0)
            return i;
    return -2;
}

static int cycle_pos(int status) {
    for (int i = 0; i < N_CYCLE; i++)
        if (CYCLE[i] == status)
            return i;
    return -1;
}

/* --- recognized fields ------------------------------------------------ */
/* The field vocabulary and `key:: value` parser now live in the markdown
 * plugin (markdown_fields.{c,h}) so the markdown folder and this plugin
 * share one definition. We add task meaning (date validation, prio
 * levels, agenda) on top of MdFieldDef / md_field_lookup. */

/* Highlight palette for fields. */
#define SGR_FIELD_KEY "\x1b[38;2;125;207;255m"   /* cyan   */
#define SGR_DATE_OK   "\x1b[38;2;158;206;106m"   /* green  */
#define SGR_DATE_BAD  "\x1b[38;2;247;118;142m"   /* red    */
static const char *PRIO_SGR[3] = {
    "\x1b[1;38;2;247;118;142m", /* A red    */
    "\x1b[1;38;2;224;175;104m", /* B amber  */
    "\x1b[38;2;122;162;247m",   /* C blue   */
};

/* Priority level from a value: 0=A,1=B,2=C (accepts A/B/C or 1/2/3), -1 bad. */
static int prio_level(const char *s, int len) {
    while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
    if (len < 1) return -1;
    char c = (char)toupper((unsigned char)s[0]);
    if (c >= 'A' && c <= 'C') return c - 'A';
    if (s[0] >= '1' && s[0] <= '3') return s[0] - '1';
    return -1;
}

/* --- date math (proleptic Gregorian, days from 1970-01-01) ------------ */

static long ymd_to_days(int y, int m, int d) {
    if (m <= 2) { y -= 1; m += 12; }
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m - 3) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void days_to_ymd(long z, int *y, int *m, int *d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    long dd = doy - (153 * mp + 2) / 5 + 1;
    long mm = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(mm <= 2 ? yy + 1 : yy);
    *m = (int)mm;
    *d = (int)dd;
}

static long today_days(void) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    return ymd_to_days(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

static void days_to_str(long z, char *out, size_t cap) {
    int y, m, d;
    days_to_ymd(z, &y, &m, &d);
    snprintf(out, cap, "%04d-%02d-%02d", y, m, d);
}

/* Strict YYYY-MM-DD with a real calendar date (round-trip check). */
static int valid_date(const char *s, int len) {
    if (len != 10 || s[4] != '-' || s[7] != '-') return 0;
    for (int i = 0; i < 10; i++)
        if (i != 4 && i != 7 && !isdigit((unsigned char)s[i])) return 0;
    int y = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
    int m = (s[5]-'0')*10 + (s[6]-'0');
    int d = (s[8]-'0')*10 + (s[9]-'0');
    if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
    int yy, mm, dd;
    days_to_ymd(ymd_to_days(y, m, d), &yy, &mm, &dd);
    return yy == y && mm == m && dd == d;
}

/* Resolve a user date arg into normalized YYYY-MM-DD. Accepts an ISO
 * date, "today"/"tomorrow"/"yesterday", or relative "+N[dw]" / "-N[dw]".
 * Empty -> today. Returns 1 on success. */
static int parse_date_arg(const char *in, char *out, size_t cap) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", in ? in : "");
    char *s = buf;
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = '\0';

    long days;
    if (!*s || strcasecmp(s, "today") == 0) {
        days = today_days();
    } else if (strcasecmp(s, "tomorrow") == 0) {
        days = today_days() + 1;
    } else if (strcasecmp(s, "yesterday") == 0) {
        days = today_days() - 1;
    } else if (s[0] == '+' || s[0] == '-') {
        char *end;
        long k = strtol(s, &end, 10);
        long unit = 1;
        if (*end == 'w' || *end == 'W') unit = 7;
        else if (*end && *end != 'd' && *end != 'D') return 0;
        days = today_days() + k * unit;
    } else if (valid_date(s, (int)n)) {
        snprintf(out, cap, "%.10s", s); /* validated: exactly 10 bytes */
        return 1;
    } else {
        return 0;
    }
    days_to_str(days, out, cap);
    return 1;
}

static void now_str(char *out, size_t cap) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, cap, "%Y-%m-%d %H:%M", &tm);
}

/* --- heading parsing -------------------------------------------------- */

typedef struct {
    int level;       /* 1..6 */
    int head_end;    /* byte offset just past the run of '#'s */
    int status;      /* index into STATUS[], or -1 if none */
    int kw_start;    /* byte offset of '[' or -1 */
    int kw_end;      /* byte offset just past ']' or -1 */
    int title_start; /* byte offset where the title text begins */
} Heading;

static int parse_heading_buf(const char *s, int len, Heading *h) {
    int i = 0, ws = 0;
    h->level = 0;
    h->status = -1;
    h->kw_start = h->kw_end = -1;

    while (i < len && s[i] == ' ') { i++; ws++; }
    if (ws > 3) return 0;

    int level = 0;
    while (i < len && s[i] == '#' && level < 7) { i++; level++; }
    if (level < 1 || level > 6) return 0;
    if (i < len && s[i] != ' ' && s[i] != '\t') return 0;

    h->level = level;
    h->head_end = i;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    int after_hashes = i;

    if (i < len && s[i] == '[') {
        int j = i + 1;
        while (j < len && s[j] != ']') j++;
        if (j < len) {
            int idx = status_lookup(s + i + 1, j - (i + 1));
            if (idx >= 0) {
                h->status = idx;
                h->kw_start = i;
                h->kw_end = j + 1;
                i = j + 1;
                while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
                h->title_start = i;
                return 1;
            }
        }
    }
    h->title_start = after_hashes;
    return 1;
}

static int parse_heading(const Row *row, Heading *h) {
    return parse_heading_buf(row->chars.data, (int)row->chars.len, h);
}

static int heading_at_or_above(Buffer *buf, int y) {
    Heading h;
    for (int r = y; r >= 0; r--)
        if (r < buf->num_rows && parse_heading(&buf->rows[r], &h))
            return r;
    return -1;
}

/* --- inline field parsing --------------------------------------------- */
/* Parsing helpers (md_parse_field / md_is_field_line / md_field_is_key)
 * live in the markdown plugin; see markdown_fields.h. */

/* End of the contiguous field block under heading `hy` (exclusive). */
static int field_block_end(Buffer *buf, int hy) {
    int e = hy + 1;
    while (e < buf->num_rows && md_is_field_line(&buf->rows[e])) e++;
    return e;
}

/* Upsert (or, when value is empty/NULL, clear) field `key` in the
 * heading's field block. */
static void task_field_set(Buffer *buf, int hy, const char *key,
                           const char *value) {
    int e = field_block_end(buf, hy);
    int at = -1;
    for (int r = hy + 1; r < e; r++)
        if (md_field_is_key(&buf->rows[r], key)) { at = r; break; }

    int clear = !value || !*value;
    if (clear) {
        if (at >= 0) buf_row_del_in(buf, at);
        return;
    }
    char line[512];
    int n = snprintf(line, sizeof(line), "%s:: %s", key, value);
    if (at >= 0) {
        buf_row_del_in(buf, at);
        buf_row_insert_in(buf, at, line, (size_t)n);
    } else {
        buf_row_insert_in(buf, e, line, (size_t)n); /* append to block */
    }
}

/* --- status mutation -------------------------------------------------- */

static void task_set(Buffer *buf, int hy, int next) {
    Heading h;
    Row *row = &buf->rows[hy];
    if (!parse_heading(row, &h)) return;
    int old = h.status;

    char out[2048];
    size_t n = 0;
    const char *s = row->chars.data;
    if ((size_t)h.head_end >= sizeof(out)) goto toolong;
    memcpy(out, s, (size_t)h.head_end);
    n = (size_t)h.head_end;
    out[n++] = ' ';
    if (next >= 0) {
        size_t kl = strlen(STATUS[next].name);
        if (n + kl + 3 >= sizeof(out)) goto toolong;
        out[n++] = '[';
        memcpy(out + n, STATUS[next].name, kl); n += kl;
        out[n++] = ']';
        out[n++] = ' ';
    }
    size_t tlen = row->chars.len - (size_t)h.title_start;
    if (n + tlen >= sizeof(out)) goto toolong;
    memcpy(out + n, s + h.title_start, tlen); n += tlen;

    buf_row_del_in(buf, hy);
    buf_row_insert_in(buf, hy, out, n);

    int old_closed = old >= 0 && STATUS[old].closed;
    int new_closed = next >= 0 && STATUS[next].closed;
    if (new_closed && !old_closed) {
        char date[16];
        days_to_str(today_days(), date, sizeof(date));
        task_field_set(buf, hy, "completed", date);
    } else if (!new_closed && old_closed) {
        task_field_set(buf, hy, "completed", "");
    }

    ed_set_status_message(next >= 0 ? "task: %s" : "task: cleared",
                          next >= 0 ? STATUS[next].name : "");
    return;
toolong:
    ed_set_status_message("task: heading too long");
}

/* Resolve the heading the cursor is on/under; -1 (with status msg) if none. */
static int cursor_heading(Buffer **out_buf) {
    Buffer *buf = buf_cur();
    if (!buf || !buf->cursor) return -1;
    int hy = heading_at_or_above(buf, buf->cursor->y);
    if (hy < 0) { ed_set_status_message("task: no heading here"); return -1; }
    *out_buf = buf;
    return hy;
}

/* --- commands --------------------------------------------------------- */

static void cmd_task_cycle(const char *args) {
    (void)args;
    Buffer *buf;
    int hy = cursor_heading(&buf);
    if (hy < 0) return;
    Heading h;
    parse_heading(&buf->rows[hy], &h);
    int next;
    if (h.status < 0) next = IDX_TODO;
    else {
        int pos = cycle_pos(h.status);
        next = (pos >= 0 && pos + 1 < N_CYCLE) ? CYCLE[pos + 1] : -1;
    }
    task_set(buf, hy, next);
}

static void cmd_task_status(const char *args) {
    int next = status_parse(args ? args : "");
    if (next == -2) {
        ed_set_status_message(
            "usage: :task_status <todo|in-progress|blocked|done|cancelled|none>");
        return;
    }
    Buffer *buf;
    int hy = cursor_heading(&buf);
    if (hy < 0) return;
    task_set(buf, hy, next);
}

/* Shared by :task_deadline / :task_schedule. */
static void set_date_field(const char *key, const char *args) {
    Buffer *buf;
    int hy = cursor_heading(&buf);
    if (hy < 0) return;
    if (args && (strcasecmp(args, "none") == 0 || strcasecmp(args, "clear") == 0)) {
        task_field_set(buf, hy, key, "");
        ed_set_status_message("task: %s cleared", key);
        return;
    }
    char date[16];
    if (!parse_date_arg(args ? args : "", date, sizeof(date))) {
        ed_set_status_message("task: bad date (use YYYY-MM-DD, today, +3d, +2w)");
        return;
    }
    task_field_set(buf, hy, key, date);
    ed_set_status_message("task: %s %s", key, date);
}

static void cmd_task_deadline(const char *args) { set_date_field("deadline", args); }
static void cmd_task_schedule(const char *args) { set_date_field("schedule", args); }

static void cmd_task_prio(const char *args) {
    Buffer *buf;
    int hy = cursor_heading(&buf);
    if (hy < 0) return;
    if (!args || !*args ||
        strcasecmp(args, "none") == 0 || strcasecmp(args, "clear") == 0) {
        task_field_set(buf, hy, "prio", "");
        ed_set_status_message("task: prio cleared");
        return;
    }
    int lvl = prio_level(args, (int)strlen(args));
    if (lvl < 0) {
        ed_set_status_message("usage: :task_prio <A|B|C>");
        return;
    }
    char v[2] = {(char)('A' + lvl), 0};
    task_field_set(buf, hy, "prio", v);
    ed_set_status_message("task: prio %s", v);
}

static void cmd_task_field(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("usage: :task_field <key> [value]");
        return;
    }
    char key[64];
    const char *p = args;
    while (*p == ' ') p++;
    int ki = 0;
    while (*p && *p != ' ' && ki < (int)sizeof(key) - 1) key[ki++] = *p++;
    key[ki] = '\0';
    while (*p == ' ') p++; /* p now at value (possibly empty) */

    Buffer *buf;
    int hy = cursor_heading(&buf);
    if (hy < 0) return;

    /* Normalize the value when the field is a recognized date/priority. */
    const MdFieldDef *fd = md_field_lookup(key, ki);
    if (fd && fd->kind == MD_FK_DATE && *p) {
        char date[16];
        if (!parse_date_arg(p, date, sizeof(date))) {
            ed_set_status_message("task: bad date for %s", key);
            return;
        }
        task_field_set(buf, hy, key, date);
    } else if (fd && fd->kind == MD_FK_PRIO && *p) {
        int lvl = prio_level(p, (int)strlen(p));
        if (lvl < 0) { ed_set_status_message("task: bad prio"); return; }
        char v[2] = {(char)('A' + lvl), 0};
        task_field_set(buf, hy, key, v);
    } else {
        task_field_set(buf, hy, key, p);
    }
    ed_set_status_message("task: %s set", key);
}

/* Insert position for a new log bullet: just after the last non-blank
 * line in the section (before the next same-or-shallower heading). */
static int section_log_pos(Buffer *buf, int hy) {
    Heading h;
    parse_heading(&buf->rows[hy], &h);
    int last = hy;
    for (int r = hy + 1; r < buf->num_rows; r++) {
        Heading hh;
        if (parse_heading(&buf->rows[r], &hh) && hh.level <= h.level) break;
        if (buf->rows[r].chars.len > 0) last = r;
    }
    return last + 1;
}

static void cmd_task_note(const char *args) {
    if (!args || !*args) {
        ed_set_status_message("usage: :task_note <text>");
        return;
    }
    Buffer *buf;
    int hy = cursor_heading(&buf);
    if (hy < 0) return;
    char dt[24], line[1024];
    now_str(dt, sizeof(dt));
    int n = snprintf(line, sizeof(line), "- %s %s", dt, args);
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    buf_row_insert_in(buf, section_log_pos(buf, hy), line, (size_t)n);
    ed_set_status_message("task: note added");
}

/* --- agenda ----------------------------------------------------------- */

typedef struct {
    char *file;
    int   line;
    int   prio;      /* 0..2, or 99 if none */
    int   has_ddl;
    long  ddl;       /* days, valid when has_ddl */
    char *title;     /* full heading text */
} Agenda;

static long g_today; /* set before qsort */

static int agenda_cmp(const void *pa, const void *pb) {
    const Agenda *a = pa, *b = pb;
    int ga = a->has_ddl ? 0 : 1, gb = b->has_ddl ? 0 : 1;
    if (ga != gb) return ga - gb;           /* dated tasks first */
    if (a->has_ddl && a->ddl != b->ddl)
        return a->ddl < b->ddl ? -1 : 1;    /* soonest (overdue) first */
    if (a->prio != b->prio) return a->prio - b->prio;
    return strcmp(a->title ? a->title : "", b->title ? b->title : "");
}

/* Scan one file, appending open tasks (with deadline/prio) to `out`. */
static void scan_file_tasks(const char *path, Agenda **out) {
    FsLines *r = NULL;
    if (fs_lines_open(&r, path) != ED_OK) return;
    const char *ln;
    size_t      llen;
    int lineno = 0, cur = -1;
    while (fs_lines_next(r, &ln, &llen)) {
        lineno++;
        int len = (int)llen;

        Heading h;
        if (parse_heading_buf(ln, len, &h)) {
            cur = -1;
            if (h.status >= 0 && !STATUS[h.status].closed) {
                Agenda a;
                memset(&a, 0, sizeof(a));
                a.file = strdup(path);
                a.line = lineno;
                a.prio = 99;
                a.title = strndup(ln, (size_t)len);
                arrput(*out, a);
                cur = (int)arrlen(*out) - 1;
            }
            continue;
        }
        if (cur < 0) continue;
        int k0, k1, v0, v1;
        if (md_parse_field(ln, len, &k0, &k1, &v0, &v1)) {
            const MdFieldDef *fd = md_field_lookup(ln + k0, k1 - k0);
            if (fd) {
                int klen = k1 - k0;
                const char *k = ln + k0;
                if (fd->kind == MD_FK_DATE &&
                    ((klen == 8 && strncmp(k, "deadline", 8) == 0) ||
                     (klen == 3 && strncmp(k, "due", 3) == 0)) &&
                    valid_date(ln + v0, v1 - v0)) {
                    int y = (ln[v0]-'0')*1000 + (ln[v0+1]-'0')*100 +
                            (ln[v0+2]-'0')*10 + (ln[v0+3]-'0');
                    int m = (ln[v0+5]-'0')*10 + (ln[v0+6]-'0');
                    int d = (ln[v0+8]-'0')*10 + (ln[v0+9]-'0');
                    Agenda *e = &(*out)[cur];
                    e->has_ddl = 1;
                    e->ddl = ymd_to_days(y, m, d);
                } else if (fd->kind == MD_FK_PRIO) {
                    int lvl = prio_level(ln + v0, v1 - v0);
                    if (lvl >= 0) (*out)[cur].prio = lvl;
                }
            }
            continue; /* stay in the field block */
        }
        cur = -1; /* blank/prose ends the block */
    }
    fs_lines_close(r);
}

static void cmd_task_agenda(const char *args) {
    (void)args;
    char **paths = NULL;
    int    npaths = 0;
    if (!term_cmd_capture(
            "rg -l --color=never -g '*.md' -g '*.markdown' "
            "-e '^[[:space:]]*#{1,6} +\\[(TODO|IN-PROGRESS|BLOCKED)\\]' 2>/dev/null",
            &paths, &npaths)) {
        ed_set_status_message("agenda: ripgrep not available");
        return;
    }
    Agenda *items = NULL;
    for (int i = 0; i < npaths; i++) {
        if (paths[i][0]) scan_file_tasks(paths[i], &items);
    }
    term_cmd_free(paths, npaths);

    int count = (int)arrlen(items);
    if (count == 0) {
        ed_set_status_message("agenda: no open tasks");
        return;
    }
    g_today = today_days();
    qsort(items, (size_t)count, sizeof(Agenda), agenda_cmp);

    qf_clear(&E.qf);
    for (int i = 0; i < count; i++) {
        Agenda *e = &items[i];
        char prefix[64] = "";
        size_t pn = 0;
        if (e->prio < 99)
            pn += (size_t)snprintf(prefix + pn, sizeof(prefix) - pn,
                                   "[#%c] ", (char)('A' + e->prio));
        if (e->has_ddl) {
            long diff = e->ddl - g_today;
            if (diff < 0)
                snprintf(prefix + pn, sizeof(prefix) - pn, "!OVERDUE %ldd ", -diff);
            else if (diff == 0)
                snprintf(prefix + pn, sizeof(prefix) - pn, "due today ");
            else
                snprintf(prefix + pn, sizeof(prefix) - pn, "due +%ldd ", diff);
        }
        char text[2048];
        snprintf(text, sizeof(text), "%s%s", prefix, e->title ? e->title : "");
        qf_add(&E.qf, e->file, e->line, 1, text);
    }
    qf_open(&E.qf, E.qf.height > 0 ? E.qf.height : 8);
    ed_set_status_message("agenda: %d open task(s)", count);

    for (int i = 0; i < count; i++) {
        free(items[i].file);
        free(items[i].title);
    }
    arrfree(items);
}

/* --- archival --------------------------------------------------------- */
/* Archiving moves a whole task section (heading + field block + prose +
 * log + any sub-tasks) out of `foo.md` and appends it to the sibling
 * `foo.md_archive`, stamping `archived:: <date>`. The source buffer is
 * saved so disk and buffer stay consistent — archiving is a durable move. */

/* End of the section rooted at heading `hy`: the next heading whose level
 * is <= this heading's, or end-of-buffer. Includes nested sub-headings. */
static int section_extent(Buffer *buf, int hy) {
    Heading h;
    parse_heading(&buf->rows[hy], &h);
    int r = hy + 1;
    for (; r < buf->num_rows; r++) {
        Heading hh;
        if (parse_heading(&buf->rows[r], &hh) && hh.level <= h.level) break;
    }
    return r;
}

/* Common precheck: resolve the archive path and reject buffers that can't
 * be archived (no file, the quickfix buffer, or an archive file itself). */
static int archive_precheck(Buffer **out, char *ap, size_t apcap) {
    Buffer *buf = buf_cur();
    if (!buf) return 0;
    if (!buf->filename || !*buf->filename) {
        ed_set_status_message("archive: save the file first");
        return 0;
    }
    if (qf_is_quickfix_buffer(buf)) {
        ed_set_status_message("archive: not available here");
        return 0;
    }
    size_t fl = strlen(buf->filename);
    if (fl >= 8 && strcmp(buf->filename + fl - 8, "_archive") == 0) {
        ed_set_status_message("archive: this is already an archive file");
        return 0;
    }
    snprintf(ap, apcap, "%s_archive", buf->filename);
    *out = buf;
    return 1;
}

static int append_section(const char *path, Buffer *buf, int hy, int end) {
    FILE *f = fopen(path, "a");
    if (!f) return 0;
    for (int r = hy; r < end; r++) {
        Row *row = &buf->rows[r];
        if (row->chars.len) fwrite(row->chars.data, 1, row->chars.len, f);
        fputc('\n', f);
    }
    fputc('\n', f); /* blank line separates archived sections */
    fclose(f);
    return 1;
}

/* Stamp, append to the archive file, then delete the rows. No save —
 * the caller saves once after a batch. Returns 1 on success. */
static int archive_section(Buffer *buf, int hy, const char *ap) {
    char date[16];
    days_to_str(today_days(), date, sizeof(date));
    task_field_set(buf, hy, "archived", date);

    int end = section_extent(buf, hy);
    if (!append_section(ap, buf, hy, end)) return 0;

    for (int r = end - 1; r >= hy; r--) buf_row_del_in(buf, r);
    /* swallow one leftover blank line so sections don't pile up gaps */
    if (hy < buf->num_rows && buf->rows[hy].chars.len == 0)
        buf_row_del_in(buf, hy);
    return 1;
}

static void clamp_cursor(Buffer *buf) {
    if (!buf->cursor) return;
    if (buf->cursor->y >= buf->num_rows)
        buf->cursor->y = buf->num_rows > 0 ? buf->num_rows - 1 : 0;
    if (buf->cursor->y < 0) buf->cursor->y = 0;
    buf->cursor->x = 0;
}

static void cmd_task_archive(const char *args) {
    (void)args;
    Buffer *buf;
    char ap[PATH_MAX];
    if (!archive_precheck(&buf, ap, sizeof(ap))) return;
    int hy = heading_at_or_above(buf, buf->cursor->y);
    if (hy < 0) { ed_set_status_message("task: no heading here"); return; }
    if (!archive_section(buf, hy, ap)) {
        ed_set_status_message("archive: could not write %s", ap);
        return;
    }
    clamp_cursor(buf);
    buf_save_in(buf);
    ed_set_status_message("archived 1 task -> %s", ap);
}

static void cmd_task_archive_done(const char *args) {
    (void)args;
    Buffer *buf;
    char ap[PATH_MAX];
    if (!archive_precheck(&buf, ap, sizeof(ap))) return;
    int n = 0;
    for (;;) {
        int hy = -1;
        for (int r = 0; r < buf->num_rows; r++) {
            Heading h;
            if (parse_heading(&buf->rows[r], &h) && h.status >= 0 &&
                STATUS[h.status].closed) { hy = r; break; }
        }
        if (hy < 0) break;
        if (!archive_section(buf, hy, ap)) {
            ed_set_status_message("archive: could not write %s", ap);
            break;
        }
        n++;
    }
    if (n) {
        clamp_cursor(buf);
        buf_save_in(buf);
        ed_set_status_message("archived %d task(s) -> %s", n, ap);
    } else {
        ed_set_status_message("archive: no closed (DONE/CANCELLED) tasks");
    }
}

static void cmd_task_archive_open(const char *args) {
    (void)args;
    Buffer *buf;
    char ap[PATH_MAX];
    if (!archive_precheck(&buf, ap, sizeof(ap))) return;
    buf_open_or_switch(ap, true);
}

/* --- keybinds --------------------------------------------------------- */

static void kb_task_cycle(void)  { cmd_task_cycle(NULL); }
static void kb_task_agenda(void) { cmd_task_agenda(NULL); }

/* Open the ":" prompt pre-filled with `cmdline` (with a trailing space)
 * so commands that take an argument are one keystroke from typing it.
 * Enter runs whatever default the command applies to an empty arg. */
static void prompt_prefilled(const char *cmdline) {
    cmd_prompt_open();
    Prompt *p = prompt_current();
    if (!p) return;
    prompt_set_text(p, cmdline, (int)strlen(cmdline));
    ed_set_status_message(":%s", p->buf);
}

static void kb_task_note(void)     { prompt_prefilled("task_note "); }
static void kb_task_deadline(void) { prompt_prefilled("task_deadline "); }
static void kb_task_schedule(void) { prompt_prefilled("task_schedule "); }
static void kb_task_prio(void)     { prompt_prefilled("task_prio "); }
static void kb_task_archive(void)      { cmd_task_archive(NULL); }
static void kb_task_archive_done(void) { cmd_task_archive_done(NULL); }

/* --- highlight (HOOK_RENDER_PRE) -------------------------------------- */

static void on_render_pre(const HookRenderEvent *e) {
    if (!e || !e->buf || !e->spans) return;
    Buffer *buf = e->buf;
    int hi = e->row_end < buf->num_rows ? e->row_end : buf->num_rows;
    for (int y = e->row_start; y < hi; y++) {
        const char *s = buf->rows[y].chars.data;
        int len = (int)buf->rows[y].chars.len;

        Heading h;
        if (parse_heading_buf(s, len, &h)) {
            if (h.status >= 0)
                attrspan_push(e->spans, y, h.kw_start, h.kw_end,
                              STATUS[h.status].sgr, 100);
            continue;
        }

        int k0, k1, v0, v1;
        if (!md_parse_field(s, len, &k0, &k1, &v0, &v1)) continue;
        const MdFieldDef *fd = md_field_lookup(s + k0, k1 - k0);
        if (!fd) continue;
        attrspan_push(e->spans, y, k0, k1, SGR_FIELD_KEY, 90);
        if (v1 <= v0) continue;
        if (fd->kind == MD_FK_DATE)
            attrspan_push(e->spans, y, v0, v1,
                          valid_date(s + v0, v1 - v0) ? SGR_DATE_OK : SGR_DATE_BAD,
                          90);
        else if (fd->kind == MD_FK_PRIO) {
            int lvl = prio_level(s + v0, v1 - v0);
            if (lvl >= 0)
                attrspan_push(e->spans, y, v0, v1, PRIO_SGR[lvl], 90);
        }
    }
}

/* --- lifecycle -------------------------------------------------------- */

static int tasks_init(void) {
    cmd("task_cycle",    cmd_task_cycle,    "rotate task status on the heading at the cursor");
    cmd("task_status",   cmd_task_status,   "set task status: todo|in-progress|blocked|done|cancelled|none");
    cmd("task_deadline", cmd_task_deadline, "set/clear deadline:: (date, today, +3d, +2w, none)");
    cmd("task_schedule", cmd_task_schedule, "set/clear schedule:: (date, today, +3d, +2w, none)");
    cmd("task_prio",     cmd_task_prio,     "set/clear prio:: (A|B|C|none)");
    cmd("task_field",    cmd_task_field,    "upsert any field: :task_field <key> [value]");
    cmd("task_note",     cmd_task_note,     "append a dated log bullet to the task section");
    cmd("task_agenda",   cmd_task_agenda,   "open tasks across the tree -> quickfix, sorted by deadline+prio");
    cmd("task_archive",      cmd_task_archive,      "move the task under the cursor to <file>_archive");
    cmd("task_archive_done", cmd_task_archive_done, "move all DONE/CANCELLED tasks to <file>_archive");
    cmd("task_archive_open", cmd_task_archive_open, "open this file's <file>_archive");

    /* Markdown-only: filetype-scoped exact matches beat the global
     * <space>m multicursor cluster inside markdown buffers, and stay
     * invisible everywhere else. */
    mapn_ft("markdown", " mc", kb_task_cycle,        "task: cycle status");
    mapn_ft("markdown", " ma", kb_task_agenda,       "task: agenda");
    mapn_ft("markdown", " mn", kb_task_note,         "task: add dated note");
    mapn_ft("markdown", " md", kb_task_deadline,     "task: set deadline");
    mapn_ft("markdown", " ms", kb_task_schedule,     "task: set schedule");
    mapn_ft("markdown", " mp", kb_task_prio,         "task: set priority");
    mapn_ft("markdown", " mx", kb_task_archive,      "task: archive task under cursor");
    mapn_ft("markdown", " mX", kb_task_archive_done, "task: archive all done tasks");

    hook_register_render(HOOK_RENDER_PRE, -1, "markdown", on_render_pre);
    return 0;
}

const Plugin plugin_tasks = {
    .name   = "tasks",
    .desc   = "markdown literate task tracker — [STATUS] headings, fields, agenda",
    .init   = tasks_init,
    .deinit = NULL,
};
