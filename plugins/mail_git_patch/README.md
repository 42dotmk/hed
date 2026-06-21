# mail_git_patch

Turns `git format-patch` output of the cwd repo into a mail-compose
buffer so you can ship a commit to a maintainer the kernel way.

## Commands

```
:mail-git-patch [range]      # outbound: compose a patch as an email
:mail-git-am    [args]       # inbound:  apply a patch email to cwd
```

### `:mail-git-patch [range]`

- No arg → `-1 HEAD` (most recent commit).
- Anything else is passed verbatim to `git format-patch`:
  `:mail-git-patch -3` (last three),
  `:mail-git-patch origin/main..HEAD` (the branch's commits),
  `:mail-git-patch abc1234` (one specific commit).

If the range produces more than one patch only the first lands in the
compose; for a whole series, use `git send-email` outside hed.

## What happens

1. `git rev-parse --is-inside-work-tree` checks we're in a repo.
2. `git format-patch --stdout <range>` runs in hed's cwd.
3. The leading `From <sha> Mon Sep 17 …` mbox separator is stripped.
4. The patch's `From:` line is replaced with `mail_get_from()` (the
   address you registered via `mail_set_from` in `config.c`), so
   msmtp's account-by-from-header lookup matches the same account
   that compose/reply/forward use.
5. Empty `To:` and `Cc:` headers are injected.
6. The rest (subject, commit message, diffstat, diff) is dropped into
   a fresh `mail-compose` buffer with title `Patch`.

Fill in `To:` and run `:mail-send` to send. The recipient receives an
RFC 822 message whose body is a `git am`-applicable patch, exactly
what `git format-patch` + `git send-email` produces.

### `:mail-git-am [args]`

The reverse direction. Three call shapes:

- **No args, viewing a patch email** — pipes the current notmuch
  thread as mbox into `git am`. Works for a single patch email and
  for a thread that *is* a patch series (the thread mbox contains
  every message in order; `git am` applies the patch ones and barfs
  on the others, which is when you reach for `--abort` / `--skip`).
- **`:mail-git-am --3way` / `--continue` / `--abort` / `--skip` /
  `--quit`** — flags forward to `git am`. If the buffer is a mail-
  message we still feed the notmuch mbox on stdin; otherwise (e.g.
  while you're resolving a conflict in the worktree) `git am` runs
  standalone, so `:mail-git-am --continue` works from any buffer.
- **`:mail-git-am /path/to/foo.patch`** — apply a file (or pass any
  other positional `git am` argument).

The command hands the terminal off to `git am` so you see its
progress and any conflict prompts; press Enter when it finishes to
return to hed. Non-zero exits leave a status hint pointing at
`--continue` / `--abort`.

## Dependencies

- `git` on `$PATH`.
- The `mail` plugin must be loaded (the compose buffer + `:mail-send`
  come from there).
