#include "editor.h"
#include "input/registers.h"
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
} Registers;

static Registers R;
extern Ed E;

static void rs_assign(StrBuf *dst, const char *data, size_t len) {
    strbuf_free(dst);
    if (!data || len == 0) {
        *dst = strbuf_new();
    } else {
        *dst = strbuf_from(data, len);
    }
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

void regs_set_yank_typed(const char *data, size_t len, RegType type) {
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
