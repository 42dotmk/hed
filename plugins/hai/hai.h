#ifndef HAI_H
#define HAI_H

#include "plugin.h"

extern const Plugin plugin_hai;

/* Configuration — call from config_init / config_user_init after
 * plugin_load(&plugin_hai, 1). Reading conversations is the mail
 * plugin's job (a session is a mail thread); this one shows the
 * agents and mails text to them. */

/* hai's Maildir root (default "~/.mail/hai", hai.conf's `mailbox`). */
void hai_set_mailbox(const char *dir);
const char *hai_get_mailbox(void);

/* Your address (default "user@hai", hai.conf's `useraddr`) and the
 * agent a block is sent to when nothing else says otherwise
 * (default "main@hai"). */
void hai_set_user(const char *addr);
void hai_set_agent(const char *addr);

/* The command that reads an RFC 822 message on stdin and delivers it
 * (default "hml send -t"). */
void hai_set_send_cmd(const char *cmd);

/* The mail query :hai scopes the mail list to. The default is a path
 * glob over the session Maildirs of every agent. */
void hai_set_query(const char *q);

#endif
