#ifndef MAIL_PARSE_H
#define MAIL_PARSE_H

#include <stddef.h>

typedef struct {
    int  part_id;
    char filename[256];
    char content_type[128];
    char msg_id[256];
} MailAttachInfo;

typedef struct {
    char **lines;        /* rendered display lines (each is malloc'd) */
    int    line_count;
    int    line_cap;

    MailAttachInfo *attaches;
    int             attach_count;
    int             attach_cap;
} MailRender;

void mail_render_init(MailRender *r);
void mail_render_free(MailRender *r);

/* Parse the output of `notmuch show --format=text` into clean display
 * lines. Each message in the thread is rendered as:
 *
 *     From:    ...
 *     To:      ...
 *     Cc:      ...           (omitted when empty)
 *     Subject: ...
 *     Date:    ...
 *     Attachments: [1] a.pdf  [2] b.png      (omitted when none)
 *
 *     <body, text/plain when available, w3m-rendered HTML otherwise>
 *
 * Messages after the first are preceded by a one-line divider.
 * Attachments collected across all messages land in `r->attaches`. */
void mail_render_notmuch_text(MailRender *r, char **raw, int raw_count);

#endif
