/* Chat-view rendering of mail threads: mail_render_show_text(..., chat=1)
 * over notmuch-style `hml show --format=text` framing. */
#include "../plugins/mail/mail_parse.h"
#include "../src/lib/vector.h"
#include "unity/unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* mail_parse's only editor dependency: the HTML → text pipe. Report
 * failure so HTML bodies fall back to the placeholder line. */
int term_cmd_filter(const char *cmd, const char *in, size_t in_len, char **out,
                    size_t *out_len) {
    (void)cmd;
    (void)in;
    (void)in_len;
    *out = NULL;
    *out_len = 0;
    return -1;
}

void setUp(void) {}
void tearDown(void) {}

#define ASSERT_EQ_INT(e, a) TEST_ASSERT_TRUE_MESSAGE((e) == (a), #e " != " #a)
#define ASSERT_EQ_STR(e, a) TEST_ASSERT_EQUAL_STRING_MESSAGE((e), (a), #a)

/* Build the framing for one text/plain message. `body` lines are
 * separated by \n. */
static void push(char ***raw, const char *s) { arrput(*raw, strdup(s)); }

static void push_msg(char ***raw, const char *id, const char *from,
                     const char *date, const char *subject, const char *body) {
    char line[512];
    snprintf(line, sizeof(line), "\fmessage{ id:%s depth:0 match:1", id);
    push(raw, line);
    push(raw, "\fheader{");
    snprintf(line, sizeof(line), "From: %s", from);
    push(raw, line);
    push(raw, "To: bob@example.com");
    snprintf(line, sizeof(line), "Subject: %s", subject);
    push(raw, line);
    snprintf(line, sizeof(line), "Date: %s", date);
    push(raw, line);
    push(raw, "\fheader}");
    push(raw, "\fbody{");
    push(raw, "\fpart{ ID: 1, Content-type: text/plain");
    const char *p = body;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char *dup = malloc(n + 1);
        memcpy(dup, p, n);
        dup[n] = '\0';
        arrput(*raw, dup);
        if (!nl)
            break;
        p = nl + 1;
    }
    push(raw, "\fpart}");
    push(raw, "\fbody}");
    push(raw, "\fmessage}");
}

static void free_raw(char **raw) {
    for (ptrdiff_t i = 0; i < arrlen(raw); i++)
        free(raw[i]);
    arrfree(raw);
}

static MailRender R;

static void render(char **raw, int chat, const char *self) {
    mail_render_free(&R);
    mail_render_init(&R);
    mail_render_show_text(&R, raw, (int)arrlen(raw), chat, self);
}

static void assert_lines(const char *const *want) {
    int n = 0;
    while (want[n])
        n++;
    for (int i = 0; i < n && i < (int)arrlen(R.lines); i++)
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i], R.lines[i], "line");
    ASSERT_EQ_INT(n, (int)arrlen(R.lines));
}

void test_chat_orders_by_date_in_local_time_with_subject_once(void) {
    char **raw = NULL;
    /* Tree order puts Bob first; the clock (Alice 08:14Z, Bob 09:02Z)
     * puts him second. Zones differ; the view shows local (UTC here). */
    push_msg(
        &raw, "b@x", "bob@example.com", "Tue, 18 May 2026 02:02:31 -0700",
        "Re: Review",
        "Sure.\n\nOn Tue, 18 May 2026 Alice wrote:\n> Hi Bob,\n>\n> notes?");
    push_msg(&raw, "a@x", "Alice Smith <alice@example.com>",
             "Tue, 18 May 2026 10:14:00 +0200", "Review", "Hi Bob,\n\nnotes?");
    render(raw, 1, NULL);
    const char *const want[] = {"Subject: Review",
                                "",
                                "● Alice Smith — 18 May 2026 08:14",
                                "Hi Bob,",
                                "",
                                "notes?",
                                "",
                                "● bob@example.com — 18 May 2026 09:02",
                                "Sure.",
                                NULL};
    assert_lines(want);
    ASSERT_EQ_INT(2, (int)arrlen(R.msgs));
    ASSERT_EQ_STR("a@x", R.msgs[0].msg_id);
    ASSERT_EQ_INT(2, R.msgs[0].hdr_row);
    ASSERT_EQ_INT(3, R.msgs[0].body_row);
    ASSERT_EQ_INT(6, R.msgs[1].row);
    ASSERT_EQ_INT(7, R.msgs[1].hdr_row);
    ASSERT_EQ_STR("Re: Review", R.msgs[1].subject);
    ASSERT_EQ_STR("bob@example.com", R.msgs[1].from);
    ASSERT_EQ_STR("b@x", R.msgs[1].msg_id);
    free_raw(raw);
}

void test_unparseable_dates_keep_thread_order(void) {
    char **raw = NULL;
    push_msg(&raw, "a@x", "a@x", "yesterday", "S", "one");
    push_msg(&raw, "b@x", "b@x", "D", "S", "two");
    render(raw, 1, NULL);
    ASSERT_EQ_STR("● a@x — yesterday", R.lines[2]);
    ASSERT_EQ_STR("● b@x — D", R.lines[5]);
    free_raw(raw);
}

void test_client_signature_block_cut(void) {
    char **raw = NULL;
    push_msg(&raw, "a@x", "Alice Smith <alice@example.com>", "D", "S",
             "Thanks, see you.\n\nAlice\n\n*Alice Smith*\n\nCTO\n\n"
             "Example Inc\n+1 555 0100\nwww.example.com\n");
    render(raw, 1, NULL);
    const char *const want[] = {
        "Subject: S", "",  "● Alice Smith — D", "Thanks, see you.", "",
        "Alice",      NULL};
    assert_lines(want);
    free_raw(raw);

    /* Not a signature: the name line heads a long block. */
    raw = NULL;
    push_msg(&raw, "a@x", "Alice Smith <alice@example.com>", "D", "S",
             "Alice Smith\nl1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\n"
             "l12\nl13\nl14\nl15\nl16\n");
    render(raw, 1, NULL);
    ASSERT_EQ_INT(3 + 17, (int)arrlen(R.lines));
    free_raw(raw);
}

void test_full_view_still_newest_first_with_headers(void) {
    char **raw = NULL;
    push_msg(&raw, "a@x", "alice@example.com", "D1", "S", "one");
    push_msg(&raw, "b@x", "bob@example.com", "D2", "Re: S", "two");
    render(raw, 0, NULL);
    ASSERT_EQ_STR("From:    bob@example.com", R.lines[0]);
    ASSERT_EQ_STR("b@x", R.msgs[0].msg_id);
    ASSERT_EQ_STR("two", R.lines[R.msgs[0].body_row]);
    ASSERT_EQ_STR("one", R.lines[R.msgs[1].body_row]);
    free_raw(raw);
}

void test_self_is_you(void) {
    char **raw = NULL;
    push_msg(&raw, "a@x", "\"Me, Myself\" <ME@example.com>", "D", "S", "x");
    render(raw, 1, "Me <me@example.com>");
    ASSERT_EQ_STR("● You — D", R.lines[2]);
    render(raw, 1, NULL);
    ASSERT_EQ_STR("● Me, Myself — D", R.lines[2]);
    free_raw(raw);
}

void test_inline_replies_keep_answers_drop_quotes(void) {
    char **raw = NULL;
    push_msg(&raw, "a@x", "a@x", "D", "S",
             "On Mon, Alice\nSmith wrote:\n> q1\n\nanswer 1\n\n> q2\n> q2b\n\n"
             "answer 2\n");
    render(raw, 1, NULL);
    const char *const want[] = {"Subject: S", "",  "● a@x — D", "answer 1", "",
                                "answer 2",   NULL};
    assert_lines(want);
    free_raw(raw);
}

void test_html_style_attribution_cuts_tail(void) {
    char **raw = NULL;
    push_msg(&raw, "a@x", "a@x", "D", "S",
             "Thanks!\n\nOn Tue, May 18 at 10:14 Alice <a@x> wrote:\n"
             "    Hi Bob,\n    notes?\n");
    render(raw, 1, NULL);
    const char *const want[] = {"Subject: S", "", "● a@x — D", "Thanks!", NULL};
    assert_lines(want);
    free_raw(raw);
}

void test_signature_and_footers_cut(void) {
    char **raw = NULL;
    push_msg(&raw, "a@x", "a@x", "D", "S", "Body\n-- \nAlice\nCEO\n");
    render(raw, 1, NULL);
    ASSERT_EQ_INT(4, (int)arrlen(R.lines));
    ASSERT_EQ_STR("Body", R.lines[3]);
    free_raw(raw);

    raw = NULL;
    push_msg(&raw, "a@x", "a@x", "D", "S", "Body\n\nSent from my iPhone\n");
    render(raw, 1, NULL);
    ASSERT_EQ_INT(4, (int)arrlen(R.lines));
    free_raw(raw);
}

void test_outlook_original_message_and_header_block_cut(void) {
    char **raw = NULL;
    push_msg(&raw, "a@x", "a@x", "D", "S",
             "Ok\n\n-----Original Message-----\nFrom: x\nSent: y\nquoted\n");
    render(raw, 1, NULL);
    ASSERT_EQ_INT(4, (int)arrlen(R.lines));
    ASSERT_EQ_STR("Ok", R.lines[3]);
    free_raw(raw);

    raw = NULL;
    push_msg(
        &raw, "a@x", "a@x", "D", "S",
        "Ok\n\nFrom: Alice\nSent: Tuesday\nTo: Bob\nSubject: hi\n\nquoted\n");
    render(raw, 1, NULL);
    ASSERT_EQ_INT(4, (int)arrlen(R.lines));
    free_raw(raw);

    raw = NULL;
    push_msg(&raw, "a@x", "a@x", "D", "S",
             "Ok\n________________________________\nFrom: Alice\nquoted\n");
    render(raw, 1, NULL);
    ASSERT_EQ_INT(4, (int)arrlen(R.lines));
    free_raw(raw);
}

void test_w3m_reference_list_cut(void) {
    char **raw = NULL;
    push_msg(&raw, "a@x", "a@x", "D", "S",
             "See [1]\n\nReferences:\n\n[1] https://example.com\n");
    render(raw, 1, NULL);
    const char *const want[] = {"Subject: S", "", "● a@x — D", "See [1]", NULL};
    assert_lines(want);
    free_raw(raw);
}

void test_quote_only_message_gets_placeholder(void) {
    char **raw = NULL;
    push_msg(&raw, "a@x", "a@x", "D", "S", "> just\n> a quote\n");
    render(raw, 1, NULL);
    ASSERT_EQ_STR("(quoted text only)", R.lines[3]);
    ASSERT_EQ_INT(4, (int)arrlen(R.lines));
    free_raw(raw);
}

void test_attachments_line_kept_in_chat(void) {
    char **raw = NULL;
    push(&raw, "\fmessage{ id:a@x depth:0 match:1");
    push(&raw, "\fheader{");
    push(&raw, "From: a@x");
    push(&raw, "Subject: S");
    push(&raw, "Date: D");
    push(&raw, "\fheader}");
    push(&raw, "\fbody{");
    push(&raw, "\fpart{ ID: 1, Content-type: multipart/mixed");
    push(&raw, "\fpart{ ID: 2, Content-type: text/plain");
    push(&raw, "see attached");
    push(&raw, "\fpart}");
    push(&raw, "\fattachment{ ID: 3, Filename: notes.pdf, Content-type: "
               "application/pdf");
    push(&raw, "Non-text part: application/pdf");
    push(&raw, "\fattachment}");
    push(&raw, "\fpart}");
    push(&raw, "\fbody}");
    push(&raw, "\fmessage}");
    render(raw, 1, NULL);
    const char *const want[] = {"Subject: S",   "",
                                "● a@x — D",    "Attachments:  [1] notes.pdf",
                                "see attached", NULL};
    assert_lines(want);
    ASSERT_EQ_INT(4, R.msgs[0].body_row);
    ASSERT_EQ_INT(1, R.msgs[0].attach_count);
    free_raw(raw);
}

int main(void) {
    setenv("TZ", "UTC", 1);
    tzset();
    UNITY_BEGIN();
    RUN_TEST(test_chat_orders_by_date_in_local_time_with_subject_once);
    RUN_TEST(test_unparseable_dates_keep_thread_order);
    RUN_TEST(test_client_signature_block_cut);
    RUN_TEST(test_full_view_still_newest_first_with_headers);
    RUN_TEST(test_self_is_you);
    RUN_TEST(test_inline_replies_keep_answers_drop_quotes);
    RUN_TEST(test_html_style_attribution_cuts_tail);
    RUN_TEST(test_signature_and_footers_cut);
    RUN_TEST(test_outlook_original_message_and_header_block_cut);
    RUN_TEST(test_w3m_reference_list_cut);
    RUN_TEST(test_quote_only_message_gets_placeholder);
    RUN_TEST(test_attachments_line_kept_in_chat);
    int rc = UNITY_END();
    mail_render_free(&R);
    return rc;
}
