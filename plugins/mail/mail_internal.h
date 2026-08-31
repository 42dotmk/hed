#ifndef MAIL_INTERNAL_H
#define MAIL_INTERNAL_H

#include "mail_parse.h"

/* Cross-file plumbing shared by mail.c / mail_impl.c / mail_send.c.
 * The user-facing configuration API is mail.h. */

/* Open/refresh the mailbox sidebar (accounts + folders discovered under
 * mail_get_dir()). */
void mail_open_mailboxes(void);

/* Open the sidebar with the cursor on the "── Tags ──" section (the
 * active tag if the base query is one, else the first tag). */
void mail_open_tags(void);

/* Register HOOK_RENDER_PRE handlers for the mail filetypes.
 * Called once from mail_plugin_init. */
void mail_register_render_hooks(void);

/* Called by <CR> while in the mailbox sidebar. */
void mail_handle_mailbox_enter(void);

/* Open a filter input prompt. */
void mail_filter_prompt(void);

/* Open/refresh the mail list buffer. */
void mail_open_list(void);

/* Called by <CR> keybind while in a mail list buffer. */
void mail_handle_enter(void);

/* Open a thread by id — "thread:…" or a full "mail://thread:…" URL (the
 * form used as thread buffer filenames and in captured markdown links).
 * Works whether or not the thread is in the current listing; when it
 * is, it's also marked read. */
void mail_open_thread(const char *tid);

/* Open the next/previous thread in the current listing while viewing a
 * mail-message buffer. No-op (with status message) if not viewing one
 * or if already at the end/beginning. */
void mail_next_message(void);
void mail_prev_message(void);

/* Apply tags to the thread(s) under the cursor or visual selection.
 * `args` is a whitespace-separated list of tags; tokens without a leading
 * +/- get a + prefix. */
void mail_apply_tags(const char *args);

/* Apply tags to every thread matching the current base + filter
 * query (i.e. everything currently listed). */
void mail_apply_tags_query(const char *args);

/* Run the configured sync command + hml new asynchronously, then
 * refresh the list. */
void mail_sync(void);

/* Open a new editable buffer pre-filled with a compose template. */
void mail_compose(void);

/* Send the current buffer as an email through the configured send
 * command. The buffer must look like an RFC 822 message: headers,
 * a blank line, then the body. */
void mail_send_current(void);

/* Add an `Attach:` pseudo-header to the current compose buffer.
 * `path` non-empty → attach that file (~ expanded, must be readable).
 * `path` NULL/empty → fzf multi-pick over project files; each pick
 * becomes one Attach: line. The headers are consumed by
 * mail_send_current, which emits a multipart/mixed message. */
void mail_attach_add(const char *path);

/* Open a compose buffer pre-filled from a `mailto:` URI (RFC 6068).
 * Recognized query keys: to, cc, bcc, subject, body, in-reply-to,
 * references. Recipients in the path and any `to` query values are
 * merged. `%0A` / `%0D%0A` in the body become real line breaks.
 * Used by the buffer pre-open hook so `hed mailto:foo@bar?...` works
 * when hed is registered as the system mailto handler. */
void mail_compose_uri(const char *uri);

/* Open a compose buffer pre-filled with a reply to the message being
 * viewed. reply_all=0 → sender only, 1 → reply-all. */
void mail_reply(int reply_all);

/* Open a compose buffer pre-filled with a forward of the message
 * under the cursor (headers + body inlined after a separator, its
 * attachments re-attached). */
void mail_forward(void);

/* Open a compose buffer that forwards the message under the cursor
 * verbatim, as a single message/rfc822 (.eml) attachment — HTML,
 * inline images and attachments all travel untouched. */
void mail_forward_eml(void);

/* The message under the cursor in the viewed thread (the rendered
 * buffer is newest-first; the cursor belongs to the last message
 * block starting at or above it). Returns NULL when the current
 * buffer is not a mail-message. `*idx` gets the 0-based display index
 * and `*count` the number of messages, when non-NULL. */
const MailMsgSpan *mail_cursor_msg(int *idx, int *count);

/* Open the viewed message's HTML body in the system browser (written
 * to /tmp, handed to open_path). Status note when there is none. */
void mail_open_html(void);

/* Act on attachments of the current mail-message buffer.
 *   dest_dir == NULL → extract to /tmp and open with `open_path`.
 *   dest_dir != NULL → extract into dest_dir (created if missing).
 *                      `~` is expanded; trailing slash is fine.
 * Selection:
 *   att_no >= 0 → act on that attachment, 1-based as numbered in the
 *                 rendered "Attachments:" line (thread-wide).
 *   MAIL_ATT_CURSOR → the attachments of the message under the
 *                 cursor (the whole thread's when it has none);
 *   MAIL_ATT_ALL → the whole thread's.
 *                 Either way: 1 attachment auto-acts; many → fzf
 *                 multi-select (Tab to pick multiple, <C-a> for all). */
#define MAIL_ATT_CURSOR (-1)
#define MAIL_ATT_ALL (-2)
void mail_attach_action(int att_no, const char *dest_dir);

/* Extract the cached attachments of message `m` of the currently-viewed
 * thread (NULL → every message) into a fresh /tmp dir. Returns an
 * stb_ds array of malloc'd paths (caller frees each entry, then
 * arrfree), or NULL if the current buffer is not a mail-message or
 * there are no attachments. */
char **mail_extract_attachments_to_tmp(const MailMsgSpan *m);

/* Write message `m` of the currently-viewed thread verbatim (`hml show
 * --format=raw`) to a fresh /tmp dir as `<subject>.eml`. Returns the
 * malloc'd path, or NULL on failure. */
char *mail_extract_raw_to_tmp(const MailMsgSpan *m, const char *subject);

#endif
