#ifndef SESSION_H
#define SESSION_H

#include "lib/errors.h"

/* Persist / restore the editor's open-buffer list to a flat file.
 *
 * File format: one entry per line. The current buffer's line is
 * prefixed with "* "; all others with "  ". Buffers without a
 * filename (scratch, [No Name]) are skipped on save.
 *
 * These are pure mechanic — they don't decide where to put the file,
 * don't gate on env vars, and don't delete the file. Plugins compose
 * those policies on top. */

/* Write all named buffers to `path`, marking the current one. */
EdError session_save(const char *path);

/* Open every file listed in `path` via buf_open_or_switch. If a line
 * was marked as the current buffer, switches to it after opening
 * everything. Closes any empty unnamed placeholder buffer left over
 * from startup. */
EdError session_restore(const char *path);

#endif
