#include "input/registers.h"
#include "editor.h"
#include "lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
    StrBuf unnamed;   /* '"' */
    StrBuf yank0;     /* '0' */
    StrBuf num[9];    /* '1'..'9' */
    StrBuf named[26]; /* 'a'..'z' */
    StrBuf cmd;       /* ':' */
    StrBuf dot;       /* '.' last executed keybind sequence */

    /* Paste type per register, kept in lockstep with the contents above.
     * The unnamed register mirrors whichever register was last written. */
    RegType t_unnamed;
    RegType t_yank0;
    RegType t_num[9];
    RegType t_named[26];

    /* Per-cursor parts of the unnamed register (see regs_batch_*). */
    StrBuf *parts;
    int nparts;
} Registers;

static Registers R;
extern Ed E;

/* An open batch: recorded writes land in B.parts[B.slot] instead of the
 * registers; the last write decides the type and yank-vs-delete. */
static struct {
    int active;
    StrBuf *parts;
    int nparts;
    int slot;
    int wrote;
    int as_delete;
    RegType type;
} B;

static void parts_free(StrBuf **parts, int *n) {
    for (int i = 0; i < *n; i++)
        strbuf_free(&(*parts)[i]);
    free(*parts);
    *parts = NULL;
    *n = 0;
}

static void rs_assign(StrBuf *dst, const char *data, size_t len) {
    strbuf_free(dst);
    if (!data || len == 0) {
        *dst = strbuf_new();
    } else {
        *dst = strbuf_from(data, len);
    }
    /* Every write to the unnamed register replaces its parts; batch_end
     * re-attaches the batch's parts after its own write. */
    if (dst == &R.unnamed)
        parts_free(&R.parts, &R.nparts);
}

/* Record a batched write into the current slot. Returns 0 when no batch
 * is open, so the caller applies the write for real. */
static int batch_record(const char *data, size_t len, RegType type,
                        int as_delete) {
    if (!B.active)
        return 0;
    if (B.slot >= 0 && B.slot < B.nparts) {
        strbuf_free(&B.parts[B.slot]);
        B.parts[B.slot] = data && len ? strbuf_from(data, len) : strbuf_new();
    }
    B.wrote = 1;
    B.type = type;
    B.as_delete = as_delete;
    return 1;
}

void regs_init(void) {
    R.unnamed = strbuf_new();
    R.yank0 = strbuf_new();
    for (int i = 0; i < 9; i++)
        R.num[i] = strbuf_new();
    for (int i = 0; i < 26; i++)
        R.named[i] = strbuf_new();
    R.cmd = strbuf_new();
    R.dot = strbuf_new();
}

void regs_free(void) {
    parts_free(&R.parts, &R.nparts);
    parts_free(&B.parts, &B.nparts);
    B.active = 0;
    strbuf_free(&R.unnamed);
    strbuf_free(&R.yank0);
    for (int i = 0; i < 9; i++)
        strbuf_free(&R.num[i]);
    for (int i = 0; i < 26; i++)
        strbuf_free(&R.named[i]);
    strbuf_free(&R.cmd);
    strbuf_free(&R.dot);
}

void regs_set_unnamed(const char *data, size_t len) {
    rs_assign(&R.unnamed, data, len);
    R.t_unnamed = REG_CHARWISE;
}

void regs_set_unnamed_typed(const char *data, size_t len, RegType type) {
    rs_assign(&R.unnamed, data, len);
    R.t_unnamed = type;
}

void regs_set_yank_typed(const char *data, size_t len, RegType type) {
    if (batch_record(data, len, type, 0))
        return;
    rs_assign(&R.yank0, data, len);
    rs_assign(&R.unnamed, data, len);
    R.t_yank0 = type;
    R.t_unnamed = type;
}

void regs_set_yank_block(const char *data, size_t len, int is_block) {
    regs_set_yank_typed(data, len, is_block ? REG_BLOCKWISE : REG_CHARWISE);
}

void regs_set_yank(const char *data, size_t len) {
    regs_set_yank_typed(data, len, REG_CHARWISE);
}

void regs_push_delete_typed(const char *data, size_t len, RegType type) {
    if (batch_record(data, len, type, 1))
        return;
    /* Rotate '9' <- '8' <- ... <- '1' (types ride along with contents) */
    strbuf_free(&R.num[8]);
    for (int i = 8; i >= 1; i--) {
        R.num[i] = R.num[i - 1];
        R.t_num[i] = R.t_num[i - 1];
    }
    /* Copy into '1' */
    R.num[0] = strbuf_new();
    if (data && len)
        rs_assign(&R.num[0], data, len);
    R.t_num[0] = type;

    rs_assign(&R.unnamed, data, len);
    R.t_unnamed = type;
}

void regs_push_delete(const char *data, size_t len) {
    regs_push_delete_typed(data, len, REG_CHARWISE);
}

void regs_set_named(char name, const char *data, size_t len) {
    if (name >= 'A' && name <= 'Z')
        name = (char)(name - 'A' + 'a');
    if (name < 'a' || name > 'z')
        return;
    int idx = name - 'a';
    rs_assign(&R.named[idx], data, len);
    R.t_named[idx] = REG_CHARWISE;
    regs_set_unnamed(data, len);
}

void regs_append_named(char name, const char *data, size_t len) {
    if (name >= 'A' && name <= 'Z')
        name = (char)(name - 'A' + 'a');
    if (name < 'a' || name > 'z')
        return;
    if (!data || len == 0)
        return;

    int idx = name - 'a';
    StrBuf *reg = &R.named[idx];

    /* Calculate new size */
    size_t new_len = reg->len + len;

    /* Allocate new buffer */
    char *new_data = malloc(new_len + 1);
    if (!new_data)
        return;

    /* Copy old data */
    if (reg->data && reg->len > 0) {
        memcpy(new_data, reg->data, reg->len);
    }

    /* Append new data */
    memcpy(new_data + reg->len, data, len);
    new_data[new_len] = '\0';

    /* Replace register contents */
    strbuf_free(reg);
    reg->data = new_data;
    reg->len = new_len;
    reg->cap = new_len + 1;
}

void regs_set_cmd(const char *data, size_t len) {
    rs_assign(&R.cmd, data, len);
}

void regs_set_dot(const char *data, size_t len) {
    rs_assign(&R.dot, data, len);
}

const StrBuf *regs_get(char name) {
    if (name == '"')
        return &R.unnamed;
    if (name == '0')
        return &R.yank0;
    if (name >= '1' && name <= '9')
        return &R.num[name - '1'];
    if (name >= 'A' && name <= 'Z')
        name = (char)(name - 'A' + 'a');
    if (name >= 'a' && name <= 'z')
        return &R.named[name - 'a'];
    if (name == ':')
        return &R.cmd;
    if (name == '.')
        return &R.dot;
    return &R.unnamed;
}

void regs_batch_begin(int nslots) {
    if (B.active)
        regs_batch_end();
    if (nslots < 1)
        nslots = 1;
    B.parts = calloc((size_t)nslots, sizeof(StrBuf));
    if (!B.parts)
        return;
    for (int i = 0; i < nslots; i++)
        B.parts[i] = strbuf_new();
    B.nparts = nslots;
    B.slot = 0;
    B.wrote = 0;
    B.active = 1;
}

void regs_batch_slot(int idx) { B.slot = idx; }

int regs_batch_active(void) { return B.active; }

int regs_batch_end(void) {
    if (!B.active)
        return 0;
    B.active = 0;
    if (!B.wrote) {
        parts_free(&B.parts, &B.nparts);
        return 0;
    }

    /* Join: parts separated by '\n'; linewise parts each end in one
     * '\n' so the joined text is a well-formed run of lines (paste
     * strips at most one trailing newline). */
    StrBuf joined = strbuf_new();
    for (int i = 0; i < B.nparts; i++) {
        StrBuf *p = &B.parts[i];
        strbuf_append(&joined, p->data, p->len);
        int nl_terminated = p->len > 0 && p->data[p->len - 1] == '\n';
        if (B.type == REG_LINEWISE ? !nl_terminated : i < B.nparts - 1)
            strbuf_append_char(&joined, '\n');
    }

    if (B.as_delete)
        regs_push_delete_typed(joined.data, joined.len, B.type);
    else
        regs_set_yank_typed(joined.data, joined.len, B.type);
    strbuf_free(&joined);

    /* The write above dropped any old parts; attach ours. */
    R.parts = B.parts;
    R.nparts = B.nparts;
    B.parts = NULL;
    B.nparts = 0;
    return !B.as_delete;
}

const StrBuf *regs_get_part(char name, int idx, int nparts) {
    if (name != '"' || !R.parts || R.nparts != nparts)
        return NULL;
    if (idx < 0 || idx >= R.nparts)
        return NULL;
    return &R.parts[idx];
}

RegType regs_get_type(char name) {
    if (name == '"')
        return R.t_unnamed;
    if (name == '0')
        return R.t_yank0;
    if (name >= '1' && name <= '9')
        return R.t_num[name - '1'];
    if (name >= 'A' && name <= 'Z')
        name = (char)(name - 'A' + 'a');
    if (name >= 'a' && name <= 'z')
        return R.t_named[name - 'a'];
    return REG_CHARWISE;
}
