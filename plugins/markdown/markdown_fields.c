/* markdown_fields: the `key:: value` field ("tag") vocabulary and parser.
 * See markdown_fields.h for the rationale — this is the single home for
 * the recognized-field table, shared by the markdown folder and the
 * tasks plugin. */

#include "markdown_fields.h"

#include <ctype.h>
#include <string.h>

static const MdFieldDef FIELDS[] = {
    {"deadline",  MD_FK_DATE}, {"due",       MD_FK_DATE},
    {"schedule",  MD_FK_DATE}, {"scheduled", MD_FK_DATE},
    {"completed", MD_FK_DATE}, {"created",   MD_FK_DATE},
    {"archived",  MD_FK_DATE},
    {"prio",      MD_FK_PRIO}, {"priority",  MD_FK_PRIO},
    {"tags",      MD_FK_TAGS},
    {"owner",     MD_FK_TEXT}, {"assignee",  MD_FK_TEXT}, {"id", MD_FK_TEXT},
};
#define N_FIELDS ((int)(sizeof(FIELDS) / sizeof(FIELDS[0])))

const MdFieldDef *md_field_lookup(const char *name, int len) {
    for (int i = 0; i < N_FIELDS; i++)
        if ((int)strlen(FIELDS[i].name) == len &&
            strncmp(FIELDS[i].name, name, (size_t)len) == 0)
            return &FIELDS[i];
    return NULL;
}

int md_parse_field(const char *s, int len,
                   int *k0, int *k1, int *v0, int *v1) {
    int i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i >= len || !isalpha((unsigned char)s[i])) return 0;
    int ks = i;
    while (i < len &&
           (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '-'))
        i++;
    int ke = i;
    if (i + 1 >= len || s[i] != ':' || s[i + 1] != ':') return 0;
    i += 2;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    int vs = i, ve = len;
    while (ve > vs && (s[ve-1] == ' ' || s[ve-1] == '\t' ||
                       s[ve-1] == '\r' || s[ve-1] == '\n')) ve--;
    *k0 = ks; *k1 = ke; *v0 = vs; *v1 = ve;
    return 1;
}

int md_is_field_line(const Row *row) {
    int a, b, c, d;
    return md_parse_field(row->chars.data, (int)row->chars.len, &a, &b, &c, &d);
}

int md_field_is_key(const Row *row, const char *key) {
    int k0, k1, v0, v1;
    if (!md_parse_field(row->chars.data, (int)row->chars.len,
                        &k0, &k1, &v0, &v1))
        return 0;
    size_t kl = strlen(key);
    return (int)kl == k1 - k0 &&
           strncmp(row->chars.data + k0, key, kl) == 0;
}
