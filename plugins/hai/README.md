# hai

hai's agents inside hed, and text sent to them.

Reading the conversations needs no view of its own. A hai session is
a Maildir (`hai/MAIL.md`), hml indexes it, so it is a mail thread like
any other — the `mail` plugin already reads threads (chat style by
default), filters, tags and replies to them. This plugin points the
mail list at those threads and adds the one thing mail cannot show:
**the agents** — who is alive, what each is doing, on which model, in
which directory.

## Requirements

- `hml` — `hml send -t` delivers a turn into an agent's inbox; `hml
  new` keeps the index current; `hml search` finds a session's thread
- `hai` — the client, for the agents view (`hai tree`) and "the live
  session of agent X" (`hai status`). Sending works without it
- the `mail` plugin — for reading. Weakly linked: hai builds and runs
  without it, minus the keys that open a thread or a compose

## Commands

| Command | Action |
|---|---|
| `:hai-agents` | The agents view: `hai tree` — address, state, mode, model, directory, task |
| `:hai` | Every agent's sessions, in the mail list |
| `:hai-sessions [agent]` | One agent's sessions (no argument: the one under the cursor) |
| `:hai-dir [path]` | The sessions of the agents working in `path` — no argument: this agent's directory, else the editor's |
| `:hai-send [text]` | Mail the visual selection / `text` / the paragraph under the cursor to an agent, into its live conversation |
| `:hai-send-new [text]` | The same, as a new session |
| `:hai-say [text]` | Say one line (prompts when given none) |
| `:hai-compose` | The mail plugin's compose buffer, addressed to the agent (`C-c C-c` sends) |

Leader keys: `<space>aa` agents, `<space>am` sessions in the mail
list, `<space>ad` sessions for this directory, `<space>as` send
(paragraph in normal mode, selection in visual), `<space>an` send as a
new session, `<space>ac` compose.

## The agents view (`hai-agents`)

```
main@hai  idle · ask · 1 attached  — Re: taskman v1 — the re-check round
└─ pm@hai  running · auto  (terminal) in ~/probe/high-company/pm on deepseek-r  — check: qa's report in?
```

hai's own tree, one row per agent: the address, its state (`idle`,
`running`, `asking`), the mode, whether a terminal is attached, the
**working directory** for an agent that has one, the model, and the
task it is on. The address, state and directory are coloured; the
task is dim.

| Key | Action |
|---|---|
| `<CR>` | This agent's live conversation, in the mail thread view |
| `f` | Its sessions in the mail list |
| `d` | The sessions of every agent working in its directory |
| `s` | Say a line to it |
| `c` | Compose a message to it |
| `T` | A terminal on it (`$HAI_TERMINAL -e hai -s NAME`, hterm by default) |
| `r` | Refresh |
| `m` | Every hai session in the mail list |
| `q` | Close |

## Filtering the mail list

`:hai` sets the mail query to the session Maildirs of every agent, and
from there the mail plugin's own tools apply: `/` to filter, `b` for
the sidebar (its Views section gets **hai sessions** and **hai asks**),
`t` for the tags — hml tags every message `hai:<intent>`, so
`not tag:hai:tool-call` is the human side of a conversation and
`tag:hai:ask` is the questions waiting for you.

Filtering by directory is a phrase search: every session carries
`Working directory: <path>` in the system message hai stores with it,
so `:hai-dir ~/projects/hackable/hed` lists the sessions of whatever
agent worked there — including agents that have since exited, which
the agents view cannot show.

Before every listing the plugin runs `hml new` (mtime-gated,
milliseconds) so a session hai has just written, or a turn just sent,
is in the index.

## Sending

A turn is mail: `From: <you>`, `To: <agent>`, threaded with
`In-Reply-To` the session's last message and `References <root>
<last>` — which is how hai routes it into that session rather than
starting a new one. When the last turn is a question hai mailed you
(`Hai-Intent: ask`), the reply goes to the mailed copy in
`<mailbox>/user/` instead, which is what makes it the answer.

A new session is rooted at the Message-ID hed writes, so hai threads
everything that follows under it.

`:hai-send` is the one that earns its keep from any buffer: select a
block of code, or leave the cursor in a paragraph, and it reaches the
agent with no copy-paste.

## Configuration

In `hai.h`, from `~/.config/hed/config.c` or `src/config.h` after
`plugin_load(&plugin_hai, 1)`:

```c
#include "hai/hai.h"
...
plugin_load(&plugin_hai, 1);

hai_set_mailbox("~/.mail/hai");   /* hai.conf `mailbox`               */
hai_set_user("user@hai");         /* hai.conf `useraddr`: you         */
hai_set_agent("main@hai");        /* where a block goes by default    */
hai_set_send_cmd("hml send -t");  /* reads RFC 822 on stdin           */
hai_set_query("path:hai/s/**");   /* what :hai scopes the list to     */
```

The agent of a session is read from its path: `s/<id>` is main's,
`s/<name>/<id>` belongs to `<name>@hai` (a child's child is
`pm.scout`, and its directory sits beside the others).

## Source layout

```
plugins/hai/
├── hai.c           # the agents view, the mail-list scopes, sending
├── hai.h           # public configuration API
├── hai_session.c   # the Maildir side: a session's files, the mailed
│                   # question, mail out
└── hai_session.h
```

`hai_session.c` knows nothing about buffers or windows — it is the
part of `hai/MAIL.md` this plugin needs, as C. The mail plugin is
reached through the command registry (`mail-query`, `mail-filter`) and
three weak symbols, so neither plugin links against the other.

## What this does not do

Live streaming. While a run is on, hai writes the reply to
`<session>/tmp/reply` token by token; that file is not mail and hml
does not index it, so the mail view shows a turn when it lands, not
as it is typed. `hai` in a terminal (or `T` from the agents view) is
where you watch a run.
