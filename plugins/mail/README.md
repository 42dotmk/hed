# mail

Maildir-based mail reader plugin.

## Usage

| Command | Action |
|---|---|
| `:mail` | Open mail list (top 100 messages, newest first) |
| `:mail-refresh` | Rescan maildir and reload the list |
| `<CR>` in list | Open selected email in a new buffer |

## Configuration

The default maildir root is `~/.mail`. To change it, call `mail_set_dir()` in
`src/config.c` after `plugin_load(&plugin_mail, 1)`:

```c
#include "mail/mail.h"
...
plugin_load(&plugin_mail, 1);
mail_set_dir("/home/user/my-mail");
```

## Maildir layout

The plugin handles two structures:

- **Single maildir**: `~/.mail/` contains `cur/` and `new/` directly.
- **Collection**: `~/.mail/` contains subdirectories (e.g. `Inbox/`, `Sent/`)
  each with their own `cur/` and `new/`.

Emails from both `cur/` (read) and `new/` (unread) are included, sorted by
modification time descending (newest first). The list shows the top 100.

## Display

Each line in the mail list shows:

```
<From>                              <Date>                  <Subject>
```

Opening an email (`<CR>`) switches to the raw message file. The file is
editable (no readonly enforced).
