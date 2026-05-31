/* mail plugin: notmuch-backed mail reader.
 *
 * :mail               open mail list (default query: tag:inbox)
 * :mail-refresh       clear filter and reload
 * :mail-filter [q]    filter by extra query terms (prompt if no args)
 * :mail-query [q]     replace the base query entirely
 * :mail-sync          run mbsync + notmuch new, then reload
 *
 * Inside a mail list buffer:
 *   <CR>  open the selected thread
 *   /     open filter prompt
 *   r     refresh (clear filter)
 *   R     sync (mbsync + notmuch new)
 *
 * Override base query or mbsync profile in config.c:
 *   mail_set_query("tag:inbox AND NOT tag:muted");
 *   mail_set_mbsync_profile("personal");           */

#include "mail.h"
#include "hed.h"
#include <stdlib.h>
#include <string.h>

static void cmd_mail(const char *args) {
    (void)args;
    mail_open_list();
    ed_render_frame();
}

static void cmd_mail_refresh(const char *args) {
    (void)args;
    mail_set_filter("");
    mail_open_list();
    ed_render_frame();
}
static void cmd_mail_filter(const char *args) {
    if (args && *args) {
        mail_set_filter(args);
        mail_open_list();
    } else {
        mail_filter_prompt();
    }
}

static void cmd_mail_query(const char *args) {
    if (args && *args) {
        mail_set_query(args);
        mail_set_filter("");
        mail_open_list();
    } else {
        ed_set_status_message("mail-query: %s", mail_get_query());
    }
}

static void cmd_mail_sync(const char *args) {
    (void)args;
    mail_sync();
}

static void cmd_mail_tag(const char *args) {
    mail_apply_tags(args);
}

static void cmd_mail_tag_all(const char *args) {
    mail_apply_tags_query(args);
}

static void cmd_mail_compose(const char *args) {
    (void)args;
    mail_compose();
}

static void cmd_mail_send(const char *args) {
    (void)args;
    mail_send_current();
}

static void cmd_mail_mailbox(const char *args) {
    if (args && *args) {
        mail_set_mailbox(args);
        mail_open_list();
    } else {
        const char *m = mail_get_mailbox();
        ed_set_status_message("mail-mailbox: %s", m && *m ? m : "(all)");
    }
}

static void cmd_mail_mailboxes(const char *args) {
    (void)args;
    mail_open_mailboxes();
    ed_render_frame();
}

static void kb_enter(void)        { mail_handle_enter(); }
static void kb_filter(void)       { mail_filter_prompt(); }
static void kb_refresh(void)      { mail_set_filter(""); mail_open_list(); }
static void kb_sync(void)         { mail_sync(); }
static void kb_mark_read(void)    { 
	mail_apply_tags("-unread");
	kb_move_down(); 
}
static void kb_mark_read_all(void){ mail_apply_tags_query("-unread"); }
static void kb_delete(void)       { mail_apply_tags("+deleted -unread -inbox"); }
static void kb_mailboxes(void)    { mail_open_mailboxes(); }
static void kb_mbox_enter(void)   { mail_handle_mailbox_enter(); }

static void cmd_mail_delete(const char *args) {
    (void)args;
    mail_apply_tags("+deleted -unread -inbox");
}

static void cmd_mail_reply(const char *args)     { (void)args; mail_reply(0); }
static void cmd_mail_reply_all(const char *args) { (void)args; mail_reply(1); }
static void cmd_mail_forward(const char *args)   { (void)args; mail_forward(); }

static void cmd_mail_attach(const char *args) {
    /* Forms:
     *   :mail-attach                  → open (multi fzf if >1)
     *   :mail-attach <id>             → open part <id>
     *   :mail-attach save [dir]       → save (multi fzf if >1) to dir
     *                                   (default ~/Downloads)
     *   :mail-attach save <id> [dir]  → save part <id> to dir
     */
    int id = -1;
    const char *dest = NULL;
    int saving = 0;

    const char *p = args ? args : "";
    while (*p == ' ') p++;

    if (strncmp(p, "save", 4) == 0 && (p[4] == '\0' || p[4] == ' ')) {
        saving = 1;
        p += 4;
        while (*p == ' ') p++;
        /* Optional id (digits) then optional dir. */
        if (*p >= '0' && *p <= '9') {
            id = atoi(p);
            while (*p >= '0' && *p <= '9') p++;
            while (*p == ' ') p++;
        }
        if (*p) dest = p;
        else    dest = "~/Downloads";
    } else if (*p) {
        id = atoi(p);
    }

    mail_attach_action(id, saving ? dest : NULL);
}

static void kb_reply(void)     { mail_reply(0); }
static void kb_reply_all(void) { mail_reply(1); }
static void kb_forward(void)   { mail_forward(); }
static void kb_attach(void)    { mail_attach_action(-1, NULL); }
static void kb_attach_save(void){ mail_attach_action(-1, "~/Downloads"); }
static void kb_next_msg(void)  { mail_next_message(); }
static void kb_prev_msg(void)  { mail_prev_message(); }

static void kb_close(void) {
    EdError err = buf_close(E.current_buffer);
    if (err == ED_ERR_BUFFER_DIRTY)
        ed_set_status_message("buffer has unsaved changes (use :bd! to force)");
}

static int mail_plugin_init(void) {
    cmd("mail",         cmd_mail,         "open notmuch mail list");
    cmd("mail-refresh", cmd_mail_refresh, "clear filter and refresh mail list");
    cmd("mail-filter",  cmd_mail_filter,  "filter mail (appended to base query)");
    cmd("mail-query",   cmd_mail_query,   "set base notmuch query");
    cmd("mail-sync",    cmd_mail_sync,    "mbsync + notmuch new, then refresh");
    cmd("mail-tag",     cmd_mail_tag,     "apply notmuch tags to thread under cursor");
    cmd("mail-tag-all", cmd_mail_tag_all, "apply notmuch tags to every thread in the current query");
    cmd("mail-compose", cmd_mail_compose, "open a new compose buffer");
    cmd("mail-send",    cmd_mail_send,    "send the current compose buffer");
    cmd("mail-mailbox",   cmd_mail_mailbox,   "scope listing to a notmuch subquery (empty = all)");
    cmd("mail-mailboxes", cmd_mail_mailboxes, "open the mailbox sidebar");
    cmd("mail-delete",    cmd_mail_delete,    "mark thread(s) under cursor/selection as deleted");
    cmd("mail-reply",     cmd_mail_reply,     "reply to the message being viewed (sender only)");
    cmd("mail-reply-all", cmd_mail_reply_all, "reply-all to the message being viewed");
    cmd("mail-forward",   cmd_mail_forward,   "forward the message being viewed");
    cmd("mail-attach",    cmd_mail_attach,    "open/save attachment(s) (no args: open, fzf multi-pick if >1; [id]; 'save [id] [dir]')");

    mapn_ft("mail", "<CR>",  kb_enter,         "open selected thread");
    mapn_ft("mail", "/",     kb_filter,        "open filter prompt");
    mapn_ft("mail", "r",     kb_refresh,       "refresh (clear filter)");
    mapn_ft("mail", "R",     kb_sync,          "sync mbsync + notmuch");
    mapn_ft("mail", "<C-r>", kb_mark_read,     "mark thread under cursor as read");
    mapv_ft("mail", "<C-r>", kb_mark_read,     "mark selected threads as read");
    keybind_register_ft(MODE_VISUAL_LINE, "<C-r>", "mail", kb_mark_read,
                        "mark selected threads as read");
    mapn_ft("mail", "<C-S-r>", kb_mark_read_all,
            "mark all threads in current query as read");

    mapn_ft("mail", "b", kb_mailboxes, "open mailbox sidebar");
    mapn_ft("mail", "C", mail_compose, "start a new compose buffer");
    mapn_ft("mail", "D", kb_delete,    "mark thread under cursor as deleted");
    mapv_ft("mail", "D", kb_delete,    "mark selected threads as deleted");
    keybind_register_ft(MODE_VISUAL_LINE, "D", "mail", kb_delete,
                        "mark selected threads as deleted");

    mapn_ft("mail-mailboxes", "<CR>", kb_mbox_enter, "select this mailbox");

    mapn_ft("mail-message", "r", kb_reply,     "reply to this message");
    mapn_ft("mail-message", "R", kb_reply_all, "reply-all to this message");
    mapn_ft("mail-message", "f", kb_forward,   "forward this message");
    mapn_ft("mail-message", "a", kb_attach,      "open attachment (1: direct; many: fzf multi-pick)");
    mapn_ft("mail-message", "A", kb_attach_save, "save attachment(s) to ~/Downloads (fzf multi-pick if >1)");
    mapn_ft("mail-message", "<C-n>", kb_next_msg, "open next message in list");
    mapn_ft("mail-message", "<C-p>", kb_prev_msg, "open previous message in list");

    /* q closes the current mail buffer in normal mode, for any of the
     * mail filetypes (list, message, mailbox sidebar, compose). */
    mapn_ft("mail",            "q", kb_close, "close mail buffer");
    mapn_ft("mail-message",    "q", kb_close, "close mail buffer");
    mapn_ft("mail-mailboxes",  "q", kb_close, "close mailbox sidebar");
    mapn_ft("mail-compose",    "q", kb_close, "close compose buffer");

    return 0;
}

const Plugin plugin_mail = {
    .name   = "mail",
    .desc   = "notmuch mail reader with mbsync sync",
    .init   = mail_plugin_init,
    .deinit = NULL,
};
