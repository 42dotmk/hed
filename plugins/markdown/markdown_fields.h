#ifndef HED_PLUGIN_MARKDOWN_FIELDS_H
#define HED_PLUGIN_MARKDOWN_FIELDS_H

/*
 * Named fields ("tags") in a markdown document.
 *
 * A field is a line of the form `key:: value`, placed directly under a
 * heading. The markdown plugin owns the vocabulary and the parser so a
 * single definition is shared by everything that cares about it:
 *
 *   - markdown_fold.c folds the contiguous field block as the innermost
 *     (deepest) fold level under each heading.
 *   - the tasks plugin gives recognized fields meaning (dates validated,
 *     prio A/B/C, agenda sorting) on top of this parser.
 *
 * Recognized field names get a FieldKind; unrecognized `key:: value`
 * lines still parse (so the folder collapses them) but resolve to no
 * FieldDef via md_field_lookup.
 */

#include "buf/row.h"

typedef enum {
    MD_FK_DATE,
    MD_FK_PRIO,
    MD_FK_TAGS,
    MD_FK_TEXT,
} MdFieldKind;

typedef struct {
    const char *name;
    MdFieldKind kind;
} MdFieldDef;

/* Look up a recognized field by name span. NULL if unrecognized. */
const MdFieldDef *md_field_lookup(const char *name, int len);

/* Parse `key:: value` from (s,len). Fills key span [k0,k1) and trimmed
 * value span [v0,v1). Returns 1 if the line is a field line, else 0. */
int md_parse_field(const char *s, int len,
                   int *k0, int *k1, int *v0, int *v1);

/* True if `row` is any `key:: value` field line (recognized or not). */
int md_is_field_line(const Row *row);

/* True if `row` is a field line whose key equals `key`. */
int md_field_is_key(const Row *row, const char *key);

#endif /* HED_PLUGIN_MARKDOWN_FIELDS_H */
