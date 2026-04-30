#ifndef DIRED_H
#define DIRED_H

#include "buffer.h"

void dired_open(const char *path);
int dired_handle_enter(void);
int dired_handle_parent(void);
int dired_handle_home(void);
int dired_handle_chdir(void);
/* If buf is a dired buffer, applies pending create/rename/delete ops by
 * diffing the buffer rows against the snapshot taken at listing time, then
 * reloads the listing. Returns 1 if buf was handled (dired filetype),
 * 0 otherwise so the caller can fall through to the normal save path. */
int dired_handle_save(Buffer *buf);
void dired_hooks_init(void);

#endif
