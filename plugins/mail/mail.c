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

static void kb_enter(void)   { mail_handle_enter(); }
static void kb_filter(void)  { mail_filter_prompt(); }
static void kb_refresh(void) { mail_set_filter(""); mail_open_list(); }
static void kb_sync(void)    { mail_sync(); }

static int mail_plugin_init(void) {
    cmd("mail",         cmd_mail,         "open notmuch mail list");
    cmd("mail-refresh", cmd_mail_refresh, "clear filter and refresh mail list");
    cmd("mail-filter",  cmd_mail_filter,  "filter mail (appended to base query)");
    cmd("mail-query",   cmd_mail_query,   "set base notmuch query");
    cmd("mail-sync",    cmd_mail_sync,    "mbsync + notmuch new, then refresh");

    mapn_ft("mail", "<CR>", kb_enter,   "open selected thread");
    mapn_ft("mail", "/",    kb_filter,  "open filter prompt");
    mapn_ft("mail", "r",    kb_refresh, "refresh (clear filter)");
    mapn_ft("mail", "R",    kb_sync,    "sync mbsync + notmuch");

    return 0;
}

const Plugin plugin_mail = {
    .name   = "mail",
    .desc   = "notmuch mail reader with mbsync sync",
    .init   = mail_plugin_init,
    .deinit = NULL,
};
