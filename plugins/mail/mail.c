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

static void cmd_mail_tag(const char *args) { mail_apply_tags(args); }

static void cmd_mail_tag_all(const char *args) { mail_apply_tags_query(args); }

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

/* Callback keybinds — only where no command expresses the behavior:
 * positional actions and composites (everything else is a cmap*_ft
 * straight onto the command). */
static void kb_enter(void) { mail_handle_enter(); }
static void kb_mark_read(void) {
    mail_apply_tags("-unread");
    kb_move_down();
}
static void kb_mbox_enter(void) { mail_handle_mailbox_enter(); }

static void cmd_mail_delete(const char *args) {
    (void)args;
    mail_apply_tags("+deleted -unread -inbox");
}

static void cmd_mail_reply(const char *args) {
    (void)args;
    mail_reply(0);
}
static void cmd_mail_reply_all(const char *args) {
    (void)args;
    mail_reply(1);
}
static void cmd_mail_forward(const char *args) {
    (void)args;
    mail_forward();
}
static void cmd_mail_open_html(const char *args) {
    (void)args;
    mail_open_html();
}

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

    char verb[16];
    const char *p = args_skip_ws(args_next_token(args, verb, sizeof(verb)));

    if (strcmp(verb, "save") == 0) {
        saving = 1;
        /* Optional id (digits) then optional dir. */
        if (*p >= '0' && *p <= '9') {
            char num[16];
            p = args_skip_ws(args_next_token(p, num, sizeof(num)));
            id = atoi(num);
        }
        dest = *p ? p : "~/Downloads";
    } else if (verb[0]) {
        id = atoi(verb);
    }

    mail_attach_action(id, saving ? dest : NULL);
}

static void cmd_mail_attach_add(const char *args) {
    const char *p = args ? args : "";
    while (*p == ' ')
        p++;
    mail_attach_add(*p ? p : NULL);
}

static void kb_next_msg(void) { mail_next_message(); }
static void kb_prev_msg(void) { mail_prev_message(); }

/* Intercept "open this path" for the mail URI schemes: mailto: routes
 * to compose (makes `hed mailto:foo@bar?subject=Hi` work for desktop
 * mail-handler registration), mail://thread:… opens that thread view —
 * which is how captured mail links in markdown are followed with gf. */
static void mail_open_pre(HookBufferEvent *ev) {
    if (!ev || !ev->filename)
        return;
    if (strncmp(ev->filename, "mailto:", 7) == 0) {
        mail_compose_uri(ev->filename);
        ev->consumed = 1;
    } else if (strncmp(ev->filename, "mail://", 7) == 0) {
        mail_open_thread(ev->filename);
        ev->consumed = 1;
    }
}

static int mail_plugin_init(void) {
    cmd("mail", cmd_mail, "open notmuch mail list");
    cmd("mail-refresh", cmd_mail_refresh, "clear filter and refresh mail list");
    cmd("mail-filter", cmd_mail_filter, "filter mail (appended to base query)");
    cmd("mail-query", cmd_mail_query, "set base notmuch query");
    cmd("mail-sync", cmd_mail_sync, "mbsync + notmuch new, then refresh");
    cmd("mail-tag", cmd_mail_tag, "apply notmuch tags to thread under cursor");
    cmd("mail-tag-all", cmd_mail_tag_all,
        "apply notmuch tags to every thread in the current query");
    cmd("mail-compose", cmd_mail_compose, "open a new compose buffer");
    cmd("mail-send", cmd_mail_send, "send the current compose buffer");
    cmd("mail-mailbox", cmd_mail_mailbox,
        "scope listing to a notmuch subquery (empty = all)");
    cmd("mail-mailboxes", cmd_mail_mailboxes, "open the mailbox sidebar");
    cmd("mail-delete", cmd_mail_delete,
        "mark thread(s) under cursor/selection as deleted");
    cmd("mail-reply", cmd_mail_reply,
        "reply to the message being viewed (sender only)");
    cmd("mail-reply-all", cmd_mail_reply_all,
        "reply-all to the message being viewed");
    cmd("mail-forward", cmd_mail_forward, "forward the message being viewed");
    cmd("mail-open-html", cmd_mail_open_html,
        "open the viewed message's HTML body in the system browser");
    cmd("mail-attach", cmd_mail_attach,
        "open/save attachment(s) (no args: open, fzf multi-pick if >1; [id]; "
        "'save [id] [dir]')");
    cmd("mail-attach-add", cmd_mail_attach_add,
        "attach file(s) to the compose buffer ([path]; no arg: fzf "
        "multi-pick)");

    mapn_ft("mail", "<CR>", kb_enter, "open selected thread");
    cmapn_ft("mail", "/", "mail-filter", "open filter prompt");
    cmapn_ft("mail", "r", "mail-refresh", "refresh (clear filter)");
    cmapn_ft("mail", "R", "mail-sync", "sync mbsync + notmuch");
    mapn_ft("mail", "<C-r>", kb_mark_read, "mark thread under cursor as read");
    mapv_ft("mail", "<C-r>", kb_mark_read, "mark selected threads as read");
    keybind_register_ft(MODE_VISUAL_LINE, "<C-r>", "mail", kb_mark_read,
                        "mark selected threads as read");
    cmapn_ft("mail", "<C-S-r>", "mail-tag-all -unread",
             "mark all threads in current query as read");

    cmapn_ft("mail", "b", "mail-mailboxes", "open mailbox sidebar");
    cmapn_ft("mail", "C", "mail-compose", "start a new compose buffer");
    cmapn_ft("mail", "D", "mail-delete", "mark thread under cursor as deleted");
    cmapv_ft("mail", "D", "mail-delete", "mark selected threads as deleted");
    keybind_register_command_ft(MODE_VISUAL_LINE, "D", "mail", "mail-delete",
                                "mark selected threads as deleted");

    mapn_ft("mail-mailboxes", "<CR>", kb_mbox_enter, "select this mailbox");

    cmapn_ft("mail-message", "r", "mail-reply", "reply to this message");
    cmapn_ft("mail-message", "R", "mail-reply-all",
             "reply-all to this message");
    cmapn_ft("mail-message", "f", "mail-forward", "forward this message");
    cmapn_ft("mail-message", "o", "mail-open-html",
             "open HTML body in system browser");
    cmapn_ft("mail-message", "a", "mail-attach",
             "open attachment (1: direct; many: fzf multi-pick)");
    cmapn_ft("mail-message", "A", "mail-attach save",
             "save attachment(s) to ~/Downloads (fzf multi-pick if >1)");
    mapn_ft("mail-message", "<C-n>", kb_next_msg, "open next message in list");
    mapn_ft("mail-message", "<C-p>", kb_prev_msg,
            "open previous message in list");

    /* Compose buffer: C-c C-c sends (mutt / Emacs message-mode
     * convention), C-c C-a attaches. Registered for both normal and
     * insert mode since compose lands the user in insert. */
    cmapn_ft("mail-compose", "<C-c><C-c>", "mail-send", "send this message");
    cmapi_ft("mail-compose", "<C-c><C-c>", "mail-send", "send this message");
    cmapn_ft("mail-compose", "<C-c><C-a>", "mail-attach-add",
             "attach file(s) via fzf");
    cmapi_ft("mail-compose", "<C-c><C-a>", "mail-attach-add",
             "attach file(s) via fzf");

    /* Mode wildcard (-1): :e runs while still in command mode, and the
     * interception must work from gf (normal) and :e alike. */
    hook_register_buffer(HOOK_BUFFER_OPEN_PRE, -1, "*", mail_open_pre);

    mail_register_render_hooks();

    /* q closes the current mail buffer in normal mode, for any of the
     * mail filetypes (list, message, mailbox sidebar, compose). */
    cmapn_ft("mail", "q", "bd", "close mail buffer");
    cmapn_ft("mail-message", "q", "bd", "close mail buffer");
    cmapn_ft("mail-mailboxes", "q", "bd", "close mailbox sidebar");
    cmapn_ft("mail-compose", "q", "bd", "close compose buffer");

    return 0;
}

const Plugin plugin_mail = {
    .name = "mail",
    .desc = "notmuch mail reader with mbsync sync",
    .init = mail_plugin_init,
    .deinit = NULL,
};
