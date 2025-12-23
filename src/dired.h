#ifndef DIRED_H
#define DIRED_H

#include "buffer.h"

void dired_open(const char *path);
int dired_handle_enter(void);
int dired_handle_parent(void);
int dired_handle_home(void);
void dired_hooks_init(void);

#endif
