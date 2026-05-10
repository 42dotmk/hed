← [hed](../../readme.md)

# shell

`:shell` runs an external command. With no arguments it opens a `!`
prompt; with arguments it runs them directly. Both paths share the
same execution: `--skipwait` flag, `%`-token expansion against the
current buffer, and trailing capture tokens that splice stdout back
into the buffer.

## Usage

```
:shell <cmd>           # run <cmd> in a child shell, then wait for Enter
:shell                 # open the `!` prompt; submit runs the line
:shell --skipwait <cmd> # don't wait for Enter when the command finishes
```

The `--skipwait` flag is recognized anywhere in the command line as
long as it stands alone (whitespace-separated).

## %-token expansion

`%`-tokens are replaced with shell-escaped values from the current
buffer. They're recognized only at word boundaries (the next char
must be non-letter or end-of-string), so `%path` stays literal.

| Token | Expands to |
|---|---|
| `%p` | full path of the current buffer |
| `%d` | directory portion of the path |
| `%n` | basename |
| `%b` | the entire buffer text |
| `%y` | contents of the unnamed yank register |
| `%%` | literal `%` |

Example:

```
:shell wc -l %p
:shell diff <(echo %b) %p
```

## Capture tokens

A trailing token (preceded by whitespace) redirects stdout back into
the editor instead of running the command interactively. The whole
splice is one undo group.

| Token | Effect |
|---|---|
| `>%b`  | replace whole buffer with stdout |
| `>>%b` | insert stdout at the cursor |
| `>%v`  | replace the visual selection with stdout (charwise / linewise; block not supported) |
| `>%y`  | store stdout in the unnamed yank register, no buffer edit |

Examples:

```
:shell sort -u %b >%b      # sort & dedupe the buffer in place
:shell date >>%b           # insert today's date at the cursor
:shell jq . >%v            # pretty-print the selected JSON
:shell git rev-parse HEAD >%y  # yank the current commit
```

The capture path bypasses the interactive child-process flow — no
`--skipwait` wait, no terminal handoff. Stderr is *not* captured;
non-zero exit codes still produce whatever stdout was written.

## The `!` prompt

`:shell` with no args opens a one-line prompt labelled `!`. Submit
runs the typed line through the same `shell_execute` path, so
`%`-tokens, `--skipwait`, and capture tokens all work there too.
Cancel (`<Esc>`) does nothing.

## Public API

```c
#include "shell/shell.h"

void cmd_shell(const char *args);
```

Exported so other plugins can shell out without re-parsing through
the colon-command machinery — the `treesitter` plugin uses it to run
`:tsi` installer commands.
