#ifndef MAIL_H
#define MAIL_H

#include "plugin.h"

extern const Plugin plugin_mail;

/* Set the base notmuch query (default: "tag:inbox"). */
void        mail_set_query(const char *q);
const char *mail_get_query(void);

/* Set an extra filter ANDed with the base query. Empty string clears it. */
void mail_set_filter(const char *filter);

/* Set the maildir root used for mailbox discovery (default: "$HOME/.mail"). */
void        mail_set_dir(const char *dir);
const char *mail_get_dir(void);

/* Restrict the listing to a notmuch sub-query, e.g. `folder:work/Inbox`
 * or `path:work/...`. Empty string clears the scope (all mailboxes). */
void        mail_set_mailbox(const char *q);
const char *mail_get_mailbox(void);

/* Register a named saved view shown at the top of the mailbox sidebar.
 * Selecting it sets the base query (e.g. "tag:inbox", "tag:unread",
 * "date:today..", "from:alice"). Call from config_init before opening
 * the sidebar; later registrations with the same name replace earlier
 * ones. Pass NULL/empty `query` to remove. */
void mail_add_view(const char *name, const char *query);

/* Open/refresh the mailbox sidebar (accounts + folders discovered under
 * mail_get_dir()). */
void mail_open_mailboxes(void);

/* Called by <CR> while in the mailbox sidebar. */
void mail_handle_mailbox_enter(void);

/* Open a filter input prompt. */
void mail_filter_prompt(void);

/* Open/refresh the mail list buffer. */
void mail_open_list(void);

/* Called by <CR> keybind while in a mail list buffer. */
void mail_handle_enter(void);

/* Open the next/previous thread in the current listing while viewing a
 * mail-message buffer. No-op (with status message) if not viewing one
 * or if already at the end/beginning. */
void mail_next_message(void);
void mail_prev_message(void);

/* Apply notmuch tags to the thread(s) under the cursor or visual selection.
 * `args` is a whitespace-separated list of tags; tokens without a leading
 * +/- get a + prefix. */
void mail_apply_tags(const char *args);

/* Apply notmuch tags to every thread matching the current base + filter
 * query (i.e. everything currently listed). */
void mail_apply_tags_query(const char *args);

/* Set the mbsync profile argument (default: "-a" for all channels). */
void mail_set_mbsync_profile(const char *profile);

/* Run mbsync + notmuch new, then refresh the list. */
void mail_sync(void);

/* ------------------------------------------------------------------ */
/* Sending                                                             */
/* ------------------------------------------------------------------ */

/* Set the outgoing-mail command. Default: "msmtp -t -a default".
 * The command must read an RFC 822 message on stdin and route it
 * based on the To/Cc/Bcc headers (the `-t` flag for msmtp/sendmail). */
void mail_set_send_cmd(const char *cmd);

/* Set the default From: address used in compose templates. If empty
 * or NULL, the From: line is left blank for the user to fill in. */
void mail_set_from(const char *from);

/* Open a new editable buffer pre-filled with a compose template. */
void mail_compose(void);

/* Send the current buffer as an email through the configured send
 * command. The buffer must look like an RFC 822 message: headers,
 * a blank line, then the body. */
void mail_send_current(void);

/* Open a compose buffer pre-filled with a reply to the message being
 * viewed. reply_all=0 → sender only, 1 → reply-all. */
void mail_reply(int reply_all);

/* Open a compose buffer pre-filled with a forward of the message
 * being viewed (raw original inlined after a separator). */
void mail_forward(void);

/* Open attachment of the current mail-message buffer.
 * If part_id < 0: with 1 attachment, opens it; with N>1, prints list
 * to the status line. If part_id >= 0: extracts that part and opens it. */
void mail_open_attachment(int part_id);

#endif
