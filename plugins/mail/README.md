# mail

A notmuch-backed mail reader for hed. Sync with `mbsync`, index with
`notmuch`, browse threads in a list buffer, read messages in a clean
rendered view, reply / forward / send via `msmtp` (or any sendmail-
compatible binary that reads RFC 822 on stdin).

## Requirements

- `notmuch` — search, tagging, message extraction
- `mbsync` (optional) — `:mail-sync`
- `msmtp` or compatible (optional) — `:mail-send`
- `w3m` or `lynx` (optional) — HTML-only message rendering

## Buffers

| Buffer | Filetype | Filename |
|---|---|---|
| Mail list | `mail` | `mail://list` |
| Message view | `mail-message` | `mail://<thread-id>` |
| Mailbox sidebar | `mail-mailboxes` | `mail://mailboxes` |
| Compose | `mail-compose` | `mail://compose-<n>` |

## Commands

| Command | Action |
|---|---|
| `:mail` | Open / refresh the mail list |
| `:mail-refresh` | Clear extra filter and reload |
| `:mail-query <q>` | Replace the base notmuch query (e.g. `tag:inbox`) |
| `:mail-filter [q]` | Append a filter to the base query (prompts if empty) |
| `:mail-mailbox [q]` | Scope listing to `folder:…` / `path:…` (empty = all) |
| `:mail-mailboxes` | Open the mailbox sidebar (accounts + folders + saved views) |
| `:mail-sync` | Run `mbsync` then `notmuch new`, then reload |
| `:mail-tag <±tags>` | Apply tags to thread under cursor (or visual selection) |
| `:mail-tag-all <±tags>` | Apply tags to every thread in the current query |
| `:mail-delete` | Shorthand for `+deleted -unread -inbox` on the current thread |
| `:mail-compose` | Open a fresh compose buffer |
| `:mail-send` | Send the current compose/reply/forward buffer |
| `:mail-reply` / `:mail-reply-all` | Reply to the message being viewed |
| `:mail-forward` | Forward the message being viewed |
| `:mail-attach [id]` | Open attachment(s) — single auto-opens; many → fzf multi-pick (Tab to select, `<C-a>` for all) |
| `:mail-attach save [id] [dir]` | Save attachment(s) instead of opening. `dir` defaults to `~/Downloads`; created if missing |

Tag tokens without a leading `+`/`-` get `+` prefixed, so
`:mail-tag work important` is equivalent to `:mail-tag +work +important`.

## Keybinds

### In the mail list (`mail` filetype)

| Key | Action |
|---|---|
| `<CR>` | Open the selected thread |
| `/` | Filter prompt |
| `r` | Refresh (clears filter) |
| `R` | Sync (`mbsync` + `notmuch new`) |
| `<C-r>` | Mark thread(s) under cursor / selection as read |
| `<C-S-r>` | Mark every thread in the current query as read |
| `D` | Mark thread(s) as deleted |
| `b` | Open mailbox sidebar |
| `q` | Close the mail list |

### In the message view (`mail-message` filetype)

| Key | Action |
|---|---|
| `r` / `R` | Reply / Reply-all |
| `f` | Forward |
| `a` | Open attachment(s) — auto if one, fzf multi-pick if many |
| `A` | Save attachment(s) to `~/Downloads` (fzf multi-pick if many) |
| `q` | Close the message |

### In the mailbox sidebar (`mail-mailboxes` filetype)

| Key | Action |
|---|---|
| `<CR>` | Select the highlighted view / folder |
| `q` | Close the sidebar |

## Message rendering

`mail_parse.{c,h}` consumes the output of `notmuch show --format=text`
and produces a clean per-message section:

```
From:    Alice <alice@example.com>
To:      bob@example.com
Cc:      team@example.com
Subject: Quarterly review
Date:    Tue, 18 May 2026 10:14:00 +0200
Attachments:  [2] notes.pdf  [3] chart.png

<body>
```

- Headers are normalized to the columns shown above; missing fields
  are simply omitted.
- The `Attachments:` line appears right below `Date:` only when the
  message has attachments. Each entry is `[<notmuch part id>] <name>`.
- Multi-message threads render every message as its own section with
  a horizontal-rule divider between them.
- The body always prefers `text/plain` if present. When only HTML is
  available, it is piped through `w3m -dump -T text/html` (or `lynx`
  if w3m is missing). With neither available, a placeholder line is
  shown.
- `\fmessage{`, `\fheader{`, `\fpart{` and other notmuch framing
  markers never reach the buffer.

The highlighter (`mail_msg_hl`) styles header keys, header values,
quoted lines (`>`), the `Attachments:` pseudo-header, and the
section divider.

## Configuration

All knobs go in `src/config.c` after `plugin_load(&plugin_mail, 1)`:

```c
#include "mail/mail.h"
...
plugin_load(&plugin_mail, 1);

mail_set_dir("/home/user/.mail");              /* maildir root        */
mail_set_query("tag:inbox AND NOT tag:muted"); /* default base query  */
mail_set_mbsync_profile("personal");           /* default: "-a"       */
mail_set_send_cmd("msmtp -t -a personal");     /* default: "msmtp -t" */
mail_set_from("Me <me@example.com>");          /* From: in compose    */

/* Saved views shown at the top of the mailbox sidebar. */
mail_add_view("Unread",      "tag:unread");
mail_add_view("Today",       "date:today..");
mail_add_view("From boss",   "from:boss@example.com");
```

## Mailbox sidebar

`:mail-mailboxes` (or `b` from the list) scans `mail_get_dir()` and
shows:

- `[All mail]` — clears both the base query and the mailbox scope.
- `── Views ──` — every entry registered with `mail_add_view`. Selecting
  one sets the base query.
- `── Mailboxes ──` — accounts (top-level dirs under the maildir root)
  and their folders. Selecting one sets a `folder:…` or `path:…` scope
  that is ANDed with the base query.

Two layouts are supported: a single maildir (`~/.mail/cur,new`) or a
collection of accounts (`~/.mail/<account>/<folder>/cur,new`).

## Compose / reply / forward

`:mail-compose` opens a buffer pre-filled with `From:`, `To:`, `Cc:`,
`Subject:` and lands you in insert mode on the To: line. `:mail-send`
pipes the buffer (validated to have non-empty `To:` and `Subject:`) to
the configured send command via a temp file, so msmtp/sendmail still
inherit a controlling terminal for password prompts.

`:mail-reply` / `:mail-reply-all` use `notmuch reply` to generate the
quoted draft; the configured `mail_set_from` (if any) overrides the
`From:` line produced by notmuch.

`:mail-forward` builds a clean compose: `From: <your address>`,
`To: ` (left empty for you to fill), `Cc: ` empty, and
`Subject: Fwd: <original>` (unless the source subject already starts
with `Fwd:`/`Fw:`). Below the blank line goes a
`---------- Forwarded message ----------` block with the original
`From` / `Date` / `Subject` / `To` / `Cc` headers and the body lines
copied straight out of the message buffer the user is reading. Any
attachments on the source message are extracted to a per-forward
`/tmp/hed-mail-fwd-…/` directory and listed as `Attach:` pseudo-
headers in the compose buffer; `:mail-send` consumes those at send
time and emits a real `multipart/mixed` MIME message (text body part +
base64-encoded attachment parts, with mime type sniffed via the
`file` command, falling back to `application/octet-stream`).

You can delete `Attach:` lines from the compose to drop individual
attachments, or add new `Attach: /path/to/file` lines anywhere in the
header block to attach extra files. Without any `Attach:` headers,
`:mail-send` produces a plain-text RFC 822 message exactly as before.

## Attachments

Attachments are detected at parse time from the notmuch text stream —
no second pass over the buffer. `:mail-attach` (or `a` in a message
buffer) extracts each part with
`notmuch show --part=<id> --format=raw` to
`/tmp/hed-mail-<id>-<safename>` and opens it via the `open` plugin.
With multiple attachments, an fzf picker shows `[id] name` for each
part — `Tab` toggles a row, `<C-a>` selects all; every picked part is
opened. You can still bypass the picker by passing an explicit id
(`:mail-attach 3`).

`:mail-attach save [id] [dir]` (or `A` in a message buffer) goes
through the same picker but writes the picked parts into `dir`
(default `~/Downloads`, created if needed) instead of opening them.

## Source layout

```
plugins/mail/
├── mail.c          # plugin entry, commands, keybinds
├── mail.h          # public API
├── mail_impl.c     # query, list/sidebar/message buffers, highlighter
├── mail_parse.{c,h}# notmuch text → clean display lines + attachments
└── mail_send.c     # compose / send / reply / forward
```
