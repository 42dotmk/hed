#ifndef TS_H
#define TS_H

#include "buffer.h"
#include <stddef.h>

/* Global toggle */
void ts_set_enabled(int on);
int  ts_is_enabled(void);

/* Per-buffer lifecycle */
void ts_buffer_init(Buffer *buf);
void ts_buffer_free(Buffer *buf);
void ts_buffer_reparse(Buffer *buf);
/* Try to load language for buffer based on path or explicit name */
int  ts_buffer_load_language(Buffer *buf, const char *lang_name);
/* Attempt autoload by filename/filetype */
int  ts_buffer_autoload(Buffer *buf);

/* Highlight a single visual line into dst (ANSI colored). Returns bytes written. */
size_t ts_highlight_line(Buffer *buf, int line_index,
                         char *dst, size_t dst_cap,
                         int col_offset, int max_cols);

#endif /* TS_H */
