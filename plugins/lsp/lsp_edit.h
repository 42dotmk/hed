#ifndef LSP_EDIT_H
#define LSP_EDIT_H

#include "buf/buffer.h"
#include "cjson/cJSON.h"
#include <stddef.h>

/* ------------------------------------------------- UTF-16 positions
 * LSP `character` offsets are UTF-16 code units; hed columns are byte
 * offsets. 1-3 byte UTF-8 sequences are one UTF-16 unit, 4-byte
 * sequences (astral plane) are a surrogate pair = two units. */
int lsp_cx_to_utf16(const char *s, size_t len, int cx);
int lsp_utf16_to_cx(const char *s, size_t len, int u16);

/* Buffer index for a document URI: an open buffer whose filename maps
 * to `uri` (file: URIs and server-owned virtual URIs alike). With
 * `open_missing`, a file: URI with no buffer is loaded from disk
 * (without switching to it). -1 if not found / not openable. Buffer
 * pointers are invalidated by an open — callers hold indices. */
int lsp_buffer_for_uri(const char *uri, int open_missing);

/* Apply a TextEdit[] to one buffer as a single undo group. Edits are
 * applied bottom-up so earlier ranges stay valid; positions are
 * converted from UTF-16 at apply time. Cursors of every window showing
 * the buffer are clamped afterwards and a didChange goes out. Returns
 * the number of edits applied. */
int lsp_apply_text_edits(int buf_idx, cJSON *edits, const char *desc);

/* Apply a WorkspaceEdit (`changes` map and/or `documentChanges`
 * array; resource operations are skipped with a log line). Files not
 * open in a buffer are loaded first and left dirty for the user to
 * save. Returns the number of edits applied; *out_files (optional)
 * receives the number of documents touched. */
int lsp_apply_workspace_edit(cJSON *edit, const char *desc, int *out_files);

#endif /* LSP_EDIT_H */
