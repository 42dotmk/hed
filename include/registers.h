#ifndef REGISTERS_H
#define REGISTERS_H

#include "sizedstr.h"

/* Simple Vim-like registers */

void regs_init(void);
void regs_free(void);

/* Set unnamed '"' register (also syncs editor clipboard) */
void regs_set_unnamed(const char *data, size_t len);

/* Set yank register '0' and unnamed */
void regs_set_yank(const char *data, size_t len);

/* Push a delete into numbered registers '1'..'9' and set unnamed */
void regs_push_delete(const char *data, size_t len);

/* Set a named register 'a'..'z' (lowercase only for now) */
void regs_set_named(char name, const char *data, size_t len);

/* Set the last command-line in ':' register */
void regs_set_cmd(const char *data, size_t len);

/* Get register by name: '"', '0', '1'..'9', 'a'..'z', ':' */
const SizedStr *regs_get(char name);

#endif /* REGISTERS_H */

