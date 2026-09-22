#ifndef HAI_H
#define HAI_H

#include "plugin.h"

extern const Plugin plugin_hai;

/* Configuration — call from config_init / config_user_init after
 * plugin_load(&plugin_hai, 1). Everything goes over mail: sessions
 * are read from hai's Maildirs, turns are sent with a sendmail-shaped
 * command (hml delivers `@hai` locally). */

/* hai's Maildir root (default "~/.mail/hai", hai.conf's `mailbox`). */
void hai_set_mailbox(const char *dir);
const char *hai_get_mailbox(void);

/* Your address (default "user@hai", hai.conf's `useraddr`) and the
 * agent a block is sent to when no session says otherwise (default
 * "main@hai"). */
void hai_set_user(const char *addr);
void hai_set_agent(const char *addr);

/* The command that reads an RFC 822 message on stdin and delivers it
 * (default "hml send -t"). */
void hai_set_send_cmd(const char *cmd);

/* Tool results in the chat view: the first N lines (default 6), or
 * all of them with :hai-verbose on. 0 hides them. */
void hai_set_result_lines(int n);

#endif
