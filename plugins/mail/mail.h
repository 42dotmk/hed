#ifndef MAIL_H
#define MAIL_H

#include "plugin.h"

extern const Plugin plugin_mail;

/* Public configuration API — call from config_init /
 * config_user_init. The plugin's own cross-file plumbing lives in
 * mail_internal.h. */

/* Set the base query (hml / notmuch syntax; default: "tag:inbox"). */
void mail_set_query(const char *q);
const char *mail_get_query(void);

/* Set an extra filter ANDed with the base query. Empty string clears it. */
void mail_set_filter(const char *filter);

/* Set the maildir root used for mailbox discovery (default: "$HOME/.mail"). */
void mail_set_dir(const char *dir);
const char *mail_get_dir(void);

/* Restrict the listing to a sub-query, e.g. `folder:work/Inbox`
 * or `path:work/...`. Empty string clears the scope (all mailboxes). */
void mail_set_mailbox(const char *q);
const char *mail_get_mailbox(void);

/* Register a named saved view shown at the top of the mailbox sidebar.
 * Selecting it sets the base query (e.g. "tag:inbox", "tag:unread",
 * "date:today..", "from:alice"). Call from config_init before opening
 * the sidebar; later registrations with the same name replace earlier
 * ones. Pass NULL/empty `query` to remove. */
void mail_add_view(const char *name, const char *query);

/* Set the shell command :mail-sync runs before `hml new`
 * (default: "hml recv"; e.g. "mbsync -a" to use mbsync instead). */
void mail_set_sync_cmd(const char *cmd);

/* Set the outgoing-mail command. Default: "msmtp -t".
 * The command must read an RFC 822 message on stdin and route it
 * based on the To/Cc/Bcc headers (the `-t` flag for msmtp/sendmail). */
void mail_set_send_cmd(const char *cmd);

/* Set the default From: address used in compose templates. If empty
 * or NULL, the From: line is left blank for the user to fill in. */
void mail_set_from(const char *from);

/* Read back the configured From: address (set via mail_set_from).
 * Returns "" if none has been configured. The returned pointer is
 * owned by the mail plugin — copy if you need to retain it past the
 * next mail_set_from call. */
const char *mail_get_from(void);

/* Open a compose buffer pre-filled from a flat array of header+body
 * lines (one row per line, the first empty line marks the start of
 * the body). Used by external producers like the git-patch plugin
 * that already have a fully-formed RFC 822 message. `title` becomes
 * the buffer title; filetype is "mail-compose" so :mail-send picks
 * it up. */
void mail_compose_with_lines(const char *title, char **lines, int count);

#endif
