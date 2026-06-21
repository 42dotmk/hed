#ifndef HED_MAIL_GIT_PATCH_H
#define HED_MAIL_GIT_PATCH_H

#include "plugin.h"

extern const Plugin plugin_mail_git_patch;

/* Open the : prompt prefilled with `mail-git-patch ` so the user can
 * type extra git-format-patch args (or just press Enter for the
 * default `-1 HEAD`). Bind from config.h via mapn. */
void kb_mail_git_patch_prompt(void);

#endif
