# sed

`:sed <expression>` pipes the active buffer through an external `sed`
process and replaces the buffer with the result.

```
:sed s/foo/bar/g       # substitute
:sed /^#/d             # delete comment lines
:sed 1,10d             # delete first ten lines
:sed 's/^/    /'       # indent every line by 4 spaces
```

The buffer's dirty flag is bumped on success; undo reverts the change
in one step. Cursor position is preserved (clamped to the new line
count if the buffer shrank).

## Requirements

The system `sed` binary must be on `$PATH`. GNU sed and BSD sed both
work — anything that reads from stdin and writes to stdout. The
expression is passed verbatim to `sed -e <expr>`, so escape shell
metacharacters as you would on the command line.

## Underlying API

```c
#include "sed/sed.h"

EdError sed_apply_to_buffer(Buffer *buf, const char *sed_expr);
```

Returns `ED_OK` on success, or one of `ED_ERR_INVALID_ARG`,
`ED_ERR_BUFFER_READONLY`, `ED_ERR_NOMEM`, `ED_ERR_SYSTEM`. Status
message is set on error.
