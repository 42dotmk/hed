# sed

`:sed <expression>` pipes the active buffer through an external `sed`
process and replaces the buffer with the output.

## Examples

```
:sed s/foo/bar/g       # global substitution
:sed /^#/d             # delete comment lines
:sed 1,10d             # delete first ten lines
:sed 's/^/    /'       # indent every line by 4 spaces
:sed -E 's/[0-9]+/N/g' # extended regex
```

The expression is passed to `sed -e <expr>`, so escape shell
metacharacters as you would on the command line.

## Behavior

- Reads the buffer's current contents (in memory, not from disk).
- Pipes them to `sed` on stdin.
- Reads `sed`'s stdout and replaces the buffer atomically.
- Bumps the dirty flag on success — undo reverts the change in one
  step.
- Cursor position is preserved (clamped to the new line count if the
  buffer shrank).

## Requirements

The system `sed` binary on `$PATH`. GNU sed and BSD sed both work.

## Limitations

- The whole buffer is replaced — no range support yet (`:5,10sed
  ...` is on the wishlist).
- Read-only buffers refuse the substitution.
- Very large buffers (>10 MB) may be slow because the whole buffer
  is serialized through the pipe.

## Underlying API

Other plugins can reuse the mechanic:

```c
#include "sed/sed.h"

EdError sed_apply_to_buffer(Buffer *buf, const char *sed_expr);
```

Returns `ED_OK` on success, or one of `ED_ERR_INVALID_ARG`,
`ED_ERR_BUFFER_READONLY`, `ED_ERR_NOMEM`, `ED_ERR_SYSTEM`. Status
message is set on error.
