# tasks

A literate-programming task tracker that lives entirely in markdown.
The document *is* the task tree — no database, no sidecar files. Status
rides on the heading; everything else is recognized fields + plain prose
you read top to bottom.

## Format

```markdown
## [IN-PROGRESS] Wire keybinds
deadline:: 2026-06-10
schedule:: 2026-06-08
prio:: A
owner:: costa
tags:: ui, input

Prose under the heading is the literate description — narrative,
code fences, sub-lists.

- 2026-06-08 14:30 split the parser into its own file
- 2026-06-08 16:05 deadline pushed a day

## [DONE] OSC52 clipboard
completed:: 2026-06-08
Works over SSH, verified on alacritty + tmux.
```

- **Status** = a `[KEYWORD]` immediately after the `#`s. A heading with
  no keyword is just a heading, not a task — prose and tasks coexist.
- **Fields** = `key:: value` lines directly under the heading
  (Dataview/Logseq style).
- **Notes** = see [Notes](#notes) below.

Statuses: `TODO` · `IN-PROGRESS` · `BLOCKED` (open) and `DONE` ·
`CANCELLED` (closed).

## Recognized fields

| Field | Kind | Notes |
|---|---|---|
| `deadline`, `due` | date | Drives the agenda (overdue first). |
| `schedule`, `scheduled` | date | When to start. |
| `completed`, `created` | date | `completed::` is stamped on close. |
| `prio`, `priority` | A/B/C | Secondary agenda sort key. |
| `tags` | list | Free-form. |
| `owner`, `assignee`, `id` | text | Recognized, no special handling. |

Dates are `YYYY-MM-DD`. The highlighter colours recognized field keys
and validates values: a good date renders green, garbage red, and the
priority is coloured by level (A red, B amber, C blue). Unrecognized
field keys are left as plain text.

## Notes

Two tiers, both plain markdown:

1. **Body prose** under the heading is the description — the literate
   part. Write freely: paragraphs, code fences, sub-lists.
2. **A dated log** at the end of the section. `:task_note <text>`
   appends `- YYYY-MM-DD HH:MM <text>` just after the section's last
   non-blank line. Append-only, chronological, greppable. Closing a
   task additionally records the date in `completed::`.

## Commands

| Command | Does |
|---|---|
| `:task_cycle` | Rotate status at/above the cursor: `(none) → TODO → IN-PROGRESS → BLOCKED → DONE → (none)`. |
| `:task_status <name>` | Set status directly (`…`, `cancelled`, `none`). |
| `:task_deadline [date]` | Set/clear `deadline::`. Date arg, or empty = today. |
| `:task_schedule [date]` | Set/clear `schedule::`. |
| `:task_prio <A\|B\|C>` | Set/clear `prio::` (also accepts `1`/`2`/`3`). |
| `:task_field <key> [value]` | Upsert any field; empty value clears it. Dates/priorities are normalized. |
| `:task_note <text>` | Append a dated log bullet to the section. |
| `:task_agenda` | All open tasks across `*.md` → quickfix, sorted by deadline (overdue first) then priority. |
| `:task_archive` | Move the task under the cursor to `<file>_archive`. |
| `:task_archive_done` | Move every `DONE`/`CANCELLED` task to `<file>_archive`. |
| `:task_archive_open` | Open this file's `<file>_archive`. |

Date arguments accept an ISO date (`2026-06-10`), `today` / `tomorrow` /
`yesterday`, or a relative offset (`+3d`, `-1d`, `+2w`). `none`/`clear`
removes the field.

## Keybinds (defaults, last-write-wins)

```
<space>mc   :task_cycle           (acts immediately)
<space>ma   :task_agenda          (acts immediately)
<space>mn   :task_note ▏          (opens the : prompt pre-filled)
<space>md   :task_deadline ▏
<space>ms   :task_schedule ▏
<space>mp   :task_prio ▏
<space>mx   :task_archive         (acts immediately)
<space>mX   :task_archive_done    (acts immediately)
```

The argument-taking commands open the `:` prompt pre-filled with the
command + a trailing space, so you land ready to type the value (or
press Enter for the command's empty-arg default — e.g. `:task_deadline`
with no arg = today). Override or add binds in `src/config.h`.

## Agenda

`:task_agenda` greps the tree for open tasks (via `rg -l`), scans each
file's heading + field block in C, and builds a quickfix list sorted by
deadline (overdue first), then priority, then title. Each line is
prefixed with `[#A]` and a deadline marker (`!OVERDUE Nd`, `due today`,
`due +Nd`). Requires `rg` on `PATH`.

## Archival

Archiving moves a whole task section — heading + field block + prose +
log + any sub-tasks — out of `foo.md` and appends it to the sibling
`foo.md_archive`, stamping `archived:: <date>` first. Sections in the
archive are separated by a blank line, and the archive is itself a valid
task markdown file (highlighting and `:task_*` commands work there too;
`:task_archive` refuses to run *on* an archive file).

- `:task_archive` (`<space>mx`) — the section under the cursor.
- `:task_archive_done` (`<space>mX`) — sweep every closed task.
- `:task_archive_open` — jump to the archive.

Archiving is a durable move: the source buffer is **saved** afterward so
disk and buffer never disagree. The write is ordered archive-first —
if appending to `_archive` fails, nothing is removed from the source.

## Notes & limitations

- The cursor can sit on the heading *or* anywhere in its field/prose
  block — commands walk up to the nearest heading.
- Heading/field detection does not track fenced code blocks, so a
  `# [TODO]` or `key:: v` line inside a ``` fence is also recognized.
  The conventions make that rare.
