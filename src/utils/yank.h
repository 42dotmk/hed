#pragma once

#include "buf/buffer.h"
#include "buf/textobj.h"
#include "lib/errors.h"
#include "lib/strbuf.h"

/*
 * Yank Module - Centralized clipboard operations
 *
 * This module provides a clean API for all yank/paste/delete/change operations.
 * It uses the register system (registers.c) for storage and TextSelection as
 * the basis for all operations.
 *
 * All yank operations store to the yank register '0' and unnamed register '"'.
 * Delete operations rotate through numbered registers '1'-'9'.
 */

/*
 * YankData - Structured yank storage
 *
 * Stores yanked text with its selection type. For block mode, stores text
 * per row to preserve rectangular structure.
 */
typedef struct YankData {
    SelectionType type;     /* Type of selection (char/line/block) */
    int num_rows;           /* Number of rows in the yank */
    StrBuf *rows;         /* Array of strings, one per row */
} YankData;


/* Core operations - all use TextSelection */
EdError yank_selection(const TextSelection *sel);

/* Block-wise yank of a render-column rectangle [start_rx, end_rx_excl)
 * across rows sy..ey (inclusive). Resolves columns per row (tab/UTF-8
 * aware) and stores blockwise into the yank/unnamed registers. */
EdError yank_block(Buffer *buf, int sy, int ey, int start_rx,
                   int end_rx_excl);

/* Paste operations */
EdError paste_from_register(Buffer *buf, char reg_name ,bool after);
void yank_data_free(YankData *yd);
YankData yank_data_new(Buffer *buf, const TextSelection *sel);
