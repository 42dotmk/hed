#ifndef MAIL_PARSE_H
#define MAIL_PARSE_H

#include <stddef.h>

typedef struct {
    int part_id; /* MIME part id (notmuch numbering) — unique per message, not
                    per thread */
    char filename[256];
    char content_type[128];
    char msg_id[256];
} MailAttachInfo;

/* One rendered message of the thread: where it starts in the display
 * lines and which slice of `attaches` belongs to it. */
typedef struct {
    int row;     /* first display row of the block (divider, if any) */
    int hdr_row; /* row of the first header line */
    char msg_id[256];
    int attach_start; /* index into MailRender.attaches */
    int attach_count;
} MailMsgSpan;

typedef struct {
    char **lines; /* stb_ds array of malloc'd display lines */

    MailAttachInfo *attaches; /* stb_ds array, whole-thread order */

    /* stb_ds array, one per message in display order (newest first). */
    MailMsgSpan *msgs;

    /* Raw text/html source of the newest message that carries one.
     * malloc'd, NUL-terminated; NULL when no message has an HTML part. */
    char *html;
    size_t html_len;
} MailRender;

void mail_render_init(MailRender *r);
void mail_render_free(MailRender *r);

/* Parse the output of `hml show --format=text` (notmuch's text
 * framing) into clean display
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
 * Attachments collected across all messages land in `r->attaches`;
 * the [n] labels are 1-based indices into that array (stable across
 * the whole thread, unlike MIME part ids which restart per
 * message). `r->msgs` records where each message starts and which
 * attachments are its own, so callers can map a cursor row back to
 * one message. */
void mail_render_show_text(MailRender *r, char **raw, int raw_count);

#endif
