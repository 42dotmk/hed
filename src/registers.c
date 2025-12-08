#include "hed.h"

typedef struct {
    SizedStr unnamed;     /* '"' */
    SizedStr yank0;       /* '0' */
    SizedStr num[9];      /* '1'..'9' */
    SizedStr named[26];   /* 'a'..'z' */
    SizedStr cmd;         /* ':' */
} Registers;

static Registers R;
extern Ed E;

static void rs_assign(SizedStr *dst, const char *data, size_t len) {
    sstr_free(dst);
    if (!data || len == 0) {
        *dst = sstr_new();
    } else {
        *dst = sstr_from(data, len);
    }
}

void regs_init(void) {
    R.unnamed = sstr_new();
    R.yank0 = sstr_new();
    for (int i = 0; i < 9; i++) R.num[i] = sstr_new();
    for (int i = 0; i < 26; i++) R.named[i] = sstr_new();
    R.cmd = sstr_new();
}

void regs_free(void) {
    sstr_free(&R.unnamed);
    sstr_free(&R.yank0);
    for (int i = 0; i < 9; i++) sstr_free(&R.num[i]);
    for (int i = 0; i < 26; i++) sstr_free(&R.named[i]);
    sstr_free(&R.cmd);
}

void regs_set_unnamed(const char *data, size_t len) {
    rs_assign(&R.unnamed, data, len);
    /* Keep editor clipboard in sync */
    sstr_free(&E.clipboard);
    if (data && len) E.clipboard = sstr_from(data, len); else E.clipboard = sstr_new();
    E.clipboard_is_block = 0;
}

void regs_set_yank_block(const char *data, size_t len, int is_block) {
    rs_assign(&R.yank0, data, len);
    rs_assign(&R.unnamed, data, len);
    sstr_free(&E.clipboard);
    if (data && len) E.clipboard = sstr_from(data, len); else E.clipboard = sstr_new();
    E.clipboard_is_block = is_block ? 1 : 0;
}

void regs_set_yank(const char *data, size_t len) {
    regs_set_yank_block(data, len, 0);
}

void regs_push_delete(const char *data, size_t len) {
    /* Rotate '9' <- '8' <- ... <- '1' */
    sstr_free(&R.num[8]);
    for (int i = 8; i >= 1; i--) {
        R.num[i] = R.num[i - 1];
    }
    /* Copy into '1' */
    R.num[0] = sstr_new();
    if (data && len) rs_assign(&R.num[0], data, len);

    regs_set_unnamed(data, len);
}

void regs_set_named(char name, const char *data, size_t len) {
    if (name >= 'A' && name <= 'Z') name = (char)(name - 'A' + 'a');
    if (name < 'a' || name > 'z') return;
    int idx = name - 'a';
    rs_assign(&R.named[idx], data, len);
    regs_set_unnamed(data, len);
}

void regs_set_cmd(const char *data, size_t len) {
    rs_assign(&R.cmd, data, len);
}

const SizedStr *regs_get(char name) {
    if (name == '"') return &R.unnamed;
    if (name == '0') return &R.yank0;
    if (name >= '1' && name <= '9') return &R.num[name - '1'];
    if (name >= 'A' && name <= 'Z') name = (char)(name - 'A' + 'a');
    if (name >= 'a' && name <= 'z') return &R.named[name - 'a'];
    if (name == ':') return &R.cmd;
    return &R.unnamed;
}
