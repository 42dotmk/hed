#ifndef ROW_H
#define ROW_H
#include "sizedstr.h"
#include <stdbool.h>

typedef struct {
    SizedStr chars;  /* Original text */
    SizedStr virtualText; /* Rendered text (with tabs expanded) */
    SizedStr render; /* Rendered text (with tabs expanded) */

    /* Fold markers */
    bool fold_start; /* True if this line starts a fold region */
    bool fold_end;   /* True if this line ends a fold region */
} Row;

/* Row operations */
int buf_row_cx_to_rx(const Row *row, int cx);
int buf_row_rx_to_cx(const Row *row, int rx);
void buf_row_update(Row *row);
void row_free(Row *row);
#endif
