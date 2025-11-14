#ifndef ROW_H
#define ROW_H
#include "sizedstr.h"
typedef struct {
    SizedStr chars;   /* Original text */
    SizedStr render;  /* Rendered text (with tabs expanded) */
} Row;

/* Row operations */
int buf_row_cx_to_rx(const Row *row, int cx);
int buf_row_rx_to_cx(const Row *row, int rx);
void buf_row_update(Row *row);
void row_free(Row *row);
#endif
