#ifndef MAIL_H
#define MAIL_H

#include "plugin.h"

extern const Plugin plugin_mail;

/* Set the base notmuch query (default: "tag:inbox"). */
void        mail_set_query(const char *q);
const char *mail_get_query(void);

/* Set an extra filter ANDed with the base query. Empty string clears it. */
void mail_set_filter(const char *filter);

/* Open a filter input prompt. */
void mail_filter_prompt(void);

/* Open/refresh the mail list buffer. */
void mail_open_list(void);

/* Called by <CR> keybind while in a mail list buffer. */
void mail_handle_enter(void);

/* Set the mbsync profile argument (default: "-a" for all channels). */
void mail_set_mbsync_profile(const char *profile);

/* Run mbsync + notmuch new, then refresh the list. */
void mail_sync(void);

#endif
