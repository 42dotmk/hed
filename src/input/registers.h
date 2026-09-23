#ifndef REGISTERS_H
#define REGISTERS_H

#include "lib/strbuf.h"

/* Simple Vim-like registers */

/* How a register's contents should be pasted, mirroring Vim's
 * characterwise / linewise / blockwise distinction. Tracked per
 * register so `p`/`P` reproduce the yank/delete shape exactly. */
typedef enum {
    REG_CHARWISE = 0,
    REG_LINEWISE,
    REG_BLOCKWISE,
} RegType;

void regs_init(void);
void regs_free(void);

/* Set unnamed '"' register (also syncs editor clipboard) */
void regs_set_unnamed(const char *data, size_t len);

/* Set only the unnamed register with an explicit paste type, leaving
 * '0' and the numbered registers alone (visual paste restores the
 * pasted content this way so it can be pasted repeatedly). */
void regs_set_unnamed_typed(const char *data, size_t len, RegType type);

/* Set yank register '0' and unnamed */
void regs_set_yank(const char *data, size_t len);
void regs_set_yank_block(const char *data, size_t len, int is_block);

/* Set yank register '0' and unnamed with an explicit paste type. */
void regs_set_yank_typed(const char *data, size_t len, RegType type);

/* Push a delete into numbered registers '1'..'9' and set unnamed */
void regs_push_delete(const char *data, size_t len);

/* Push a delete with an explicit paste type. */
void regs_push_delete_typed(const char *data, size_t len, RegType type);

/* The paste type recorded for a register (REG_CHARWISE if unknown). */
RegType regs_get_type(char name);

/* Set a named register 'a'..'z' (lowercase only for now) */
void regs_set_named(char name, const char *data, size_t len);

/* Append to a named register 'a'..'z' */
void regs_append_named(char name, const char *data, size_t len);

/* Set the last command-line in ':' register */
void regs_set_cmd(const char *data, size_t len);

/* Set the last keybind sequence in '.' register */
void regs_set_dot(const char *data, size_t len);

/* Get register by name: '"', '0', '1'..'9', 'a'..'z', ':', '.' */
const StrBuf *regs_get(char name);

/*
 * Multi-part writes (multicursor yank/delete).
 *
 * Between regs_batch_begin(n) and regs_batch_end(), every yank
 * (regs_set_yank*) and delete (regs_push_delete*) is recorded into the
 * part selected by regs_batch_slot() instead of being applied. At
 * batch_end one real write happens — the parts joined by '\n' (each
 * linewise part newline-terminated) — and the parts stay attached to
 * the unnamed register, so a paste at N cursors can hand each cursor
 * its own part (regs_get_part) while a single-cursor paste, or any
 * other reader, sees the joined text. Slots never written are empty
 * parts. Any later plain write to the unnamed register drops the
 * parts. batch_end is a no-op when nothing was recorded.
 */
void regs_batch_begin(int nslots);
void regs_batch_slot(int idx);
int regs_batch_active(void);
/* Returns 1 when the batch wrote a yank (not a delete) — the caller
 * fires HOOK_YANK once for the whole batch. */
int regs_batch_end(void);

/* Part idx of the unnamed register, if it carries exactly nparts parts
 * (the caller's cursor count); NULL otherwise. Only '"' has parts. */
const StrBuf *regs_get_part(char name, int idx, int nparts);

#endif /* REGISTERS_H */
