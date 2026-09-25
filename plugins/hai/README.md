# hai

hai's agents inside hed, and text sent to them.

Reading the conversations needs no view of its own. Every hai agent
has one Maildir (`hai/MAIL.md`): its inbox in `new/`, every session it
had in `cur/`, one thread each. hml indexes it, so the `mail` plugin
already reads it (chat style by default), filters, tags and replies to
it. This plugin points the mail list at those sessions and adds the
one thing mail cannot show: **the agents** — who is alive, what each
is doing, on which model, in which directory.

A session is a thread, scoped to its agent's box. A child agent's
session starts from the mail that spawned it, so its first message
carries the parent's `References` and hml threads the two together —
ask for the thread alone and the parent's whole conversation comes
with it. Every scope is therefore a `path:` query over the agents'
boxes: one session (`path:hai/<agent> and thread:<T>`), one agent's
(`path:hai/<agent>`), or all of them (`path:hai/**`, the configurable
base query). A message deleted in the mail view (`D`, the `deleted`
tag) is flagged T in its file: hai no longer loads it, these scopes
leave it out, and `hml recv` removes it.

## Requirements

- `hml` — `hml send -t` delivers a turn into an agent's inbox; `hml
  new` keeps the index current; `hml show` reads a session's box
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
| `<CR>` | This agent's live session, in the mail conversation view |
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
everything that follows under it — and stores it in a Maildir of its
own, which is what the views here scope to.

`:hai-send` is the one that earns its keep from any buffer: select a
block of code, or leave the cursor in a paragraph, and it reaches the
agent with no copy-paste.

Inside a session's thread there is nothing to do here: `i` opens the
mail plugin's reply box under the conversation and `C-c C-c` sends it
— `hml reply` gives the headers that put it back into the session, and
no quote, so the agent gets what you typed and nothing else. The hai
plugin only tells mail that `user@hai` is you, so the box is addressed
to the agent even when your own turn is the newest message.

## Colour

Two layers the plugin adds to a thread it is reading, on top of what
the mail plugin paints:

- **Tool calls.** hai writes one `-> name {arguments}` line per call
  into the text part of an assistant message, so that is what the
  thread shows. The tool gets the keyword colour, its argument names
  the property colour, string values the string colour, numbers and
  booleans the number colour, the punctuation gets out of the way.
- **The agents view**, above: address, state, directory, task.

The prose around them is the mail plugin's doing — it hands thread
buffers to the markdown grammar, so headings, emphasis, lists and
fenced code blocks are coloured, the block in whatever language its
fence names (```sh through bash's grammar, ```json through JSON's).

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
hai_set_query("path:hai/** and not tag:deleted"); /* what :hai lists */
```

The agent of a session is read from its path: `<mailbox>/<name>/cur/`
belongs to `<name>@hai` (a child's child is `pm.scout`, and its box
sits beside the others); the session from the file's
`Hai-Conversation`.

## The reply as it is written

While a run is on, hai streams the reply into the agent's
`tmp/reply.<session id>` token by token. That file is not mail and hml never sees it, so the
thread view would stop at the last stored turn. The plugin tails it:
each thread buffer is asked once (one hml lookup) whether it is a hai
session, and while one is open a 400 ms timer appends what the model
has said so far under the conversation:

```
● You — 23 Sep 12:03
what is eating the disk

● main@hai — writing…
Let me look at the biggest directories
```

When the turn lands as a real message the index refreshes, the thread
re-renders and the tail disappears — the message is simply there,
where it belongs. `working…` in place of `writing…` means the run is
on but nothing has been said yet (a tool is running). The cursor
follows the end of the conversation when it was already there, and
stays put when you have scrolled up to read.

Ordinary mail has no such file: nothing is appended, and the buffer is
left exactly as the mail plugin rendered it.

## Source layout

```
plugins/hai/
├── hai.c           # the agents view, the mail-list scopes, sending
├── hai.h           # public configuration API
├── hai_session.c   # the Maildir side: a session's files in its agent's
│                   # box, the mailed
│                   # question, mail out
└── hai_session.h
```

`hai_session.c` knows nothing about buffers or windows — it is the
part of `hai/MAIL.md` this plugin needs, as C. The mail plugin is
reached through the command registry (`mail-query`, `mail-filter`) and
three weak symbols, so neither plugin links against the other.
