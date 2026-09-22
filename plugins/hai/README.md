# hai

hai's conversations inside hed, over mail. hai keeps every session
as a Maildir (`hai/MAIL.md`: one file per turn, `Hai-*` headers say
what each file is); this plugin reads those directories straight off
the disk and writes your turns back with `hml send -t`, which
delivers `@hai` mail locally into the agent's inbox. No socket, no
index: the session list is a directory scan, a chat is the files of
one session, "streaming" is the reply file hai writes while the model
talks, and a watcher timer re-renders the open chats when anything
under them changes.

## Requirements

- `hml` — `hml send -t` for local delivery (the only thing that
  writes); `hml search` only for `m` (open the same thread in the
  mail plugin)
- `hai` — the client, only for the agent tree and "the live session
  of agent X"; sessions and chats work with no daemon at all

## Buffers

| Buffer | Filetype | Filename |
|---|---|---|
| Sessions | `hai-sessions` | `hai://sessions` |
| Agent tree | `hai-tree` | `hai://tree` |
| Chat | `hai-chat` | `hai://<agent>/<session-id>` |
| Compose | `hai-compose` | `hai://compose-<n>` |

## Commands

| Command | Action |
|---|---|
| `:hai` | Sessions of every agent under the mailbox, newest first |
| `:hai-tree` | The agent tree (`hai -s main tree`) |
| `:hai-send [text]` | Mail the visual selection / `text` / the paragraph under the cursor into the session being viewed — or, from any other buffer, into the agent's live conversation (a new session when the daemon is not reachable) |
| `:hai-send-new [text]` | The same, always as a new session |
| `:hai-compose` | A compose buffer: a reply into the viewed session, else a new message to the agent. `C-c C-c` sends |
| `:hai-verbose [on\|off\|toggle]` | Whole tool results in the chat view (default: the first 6 lines) |
| `:hai-refresh` | Reread the current hai buffer |

Leader keys (from `src/config.h`-style defaults registered by the
plugin, overridable last-write-wins): `<space>aa` sessions,
`<space>at` tree, `<space>as` send (paragraph in normal mode,
selection in visual), `<space>an` send as a new session, `<space>ac`
compose.

## Keys

### Sessions (`hai-sessions`)

`<CR>` open the chat · `r` refresh · `t` tree · `n` compose · `q` close

### Agent tree (`hai-tree`)

`<CR>` open the agent's live conversation · `T` a terminal on it
(`$HAI_TERMINAL -e hai -s NAME`, hterm by default) · `l` sessions ·
`r` refresh · `q` close

### Chat (`hai-chat`)

`s` say a line (prompt; `:hai-say [text]`) · `S` compose a reply ·
`r` refresh · `v` whole tool results · `m` the same thread in the
mail plugin · `t` tree · `l` sessions · `q` close

### Compose (`hai-compose`)

`C-c C-c` send (normal and insert mode) · `q` close

## The chat view

```
Subject: what is eating the disk

● You — 18 Sep 17:19
what is eating the disk

● main@hai — 18 Sep 17:19
Let me look.
  → shell {"command": "du -sh ~/*"}
  │ 4.0G  /home/halicea/Downloads
  │ … +12 lines

● main@hai — 18 Sep 17:20
Downloads is the culprit.

? main@hai — 18 Sep 17:20 · asks
May I delete the ISOs?

● You — 18 Sep 17:21 · queued
yes

● main@hai · writing…
Deleting them now
```

Files in name order — the order hai loads them — one block per turn:
system prompts are hidden, an assistant message with tool calls shows
its prose then one `→ name args` line per call, tool results follow
as `│` lines (abridged unless `:hai-verbose`), `ask` notes get a `?`
header, `answer` and `summary` turns are labelled. A turn that came in
as mail loses the `From:/Date:/Subject:/Message-ID:` envelope hai
stores with it (the block header says the same).

After the stored turns come the ones **queued**: mailed into the
agent's inbox (`<mailbox>/<agent>/new`) but not yet taken (an agent
takes mail only between runs). Last, while `<session>/tmp/reply`
exists a run is on: its content is the reply being streamed, shown
under a `writing…` header — minus what is already stored, since an
assistant message with tool calls lands in `cur/` mid-run and the
reply file keeps every fragment of the run. `working…` means the run
is on but nothing has been said yet (a tool is running).

The watcher stats the session's `cur/`, `new/`, `tmp/`, the reply
file and the agent's inbox every 500 ms while a chat buffer exists;
a cursor on the last row follows the conversation, any other keeps
its place.

## Sending

A message from hed is `From: <you>`, `To: <agent>`, threaded into the
session with `In-Reply-To` the session's last message and `References
<root> <last>`, which is how hai routes it into that session. When the
last message is a question hai mailed you (`Hai-Intent: ask`), the
reply goes `In-Reply-To` the mailed copy in `<mailbox>/user/` instead —
that is what makes it the answer. A new session is rooted at the
Message-ID hed writes, so its chat opens at once and shows the message
queued until the agent takes it.

`:hai-send` outside a chat asks `hai -s <agent> status` for the live
session; with no daemon it starts a new one.

## Configuration

All in `hai.h`, from `~/.config/hed/config.c` or `src/config.h` after
`plugin_load(&plugin_hai, 1)`:

```c
#include "hai/hai.h"
...
plugin_load(&plugin_hai, 1);

hai_set_mailbox("~/.mail/hai");   /* hai.conf `mailbox`             */
hai_set_user("user@hai");         /* hai.conf `useraddr`: shown as You */
hai_set_agent("main@hai");        /* where blocks go by default      */
hai_set_send_cmd("hml send -t");  /* reads RFC 822 on stdin          */
hai_set_result_lines(6);          /* tool result lines when abridged */
```

The agent of a session is read from its path: `s/<id>` is main's,
`s/<name>/<id>` belongs to `<name>@hai` (a child's child is
`pm.scout`, its directory too).

## Source layout

```
plugins/hai/
├── hai.c           # buffers, rendering, watcher, sending, keys
├── hai.h           # public configuration API
├── hai_session.c   # the Maildir side: scan, load, preview, pending,
│                   # the mailed question, mail out
└── hai_session.h
```

`hai_session.c` knows nothing about buffers or windows — it is the
format in `hai/MAIL.md` as C. The only tie to the mail plugin is `m`,
through a weak `mail_open_thread`; the build links without it.
