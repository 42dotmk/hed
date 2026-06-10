#ifndef REGISTERS_H
#define REGISTERS_H

#include "lib/sizedstr.h"

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
const SizedStr *regs_get(char name);

#endif /* REGISTERS_H */
