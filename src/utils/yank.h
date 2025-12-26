#pragma once

#include "../buf/buffer.h"
#include "../buf/textobj.h"
#include "../lib/errors.h"
#include "../lib/sizedstr.h"

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
    SizedStr *rows;         /* Array of strings, one per row */
} YankData;


/* Core operations - all use TextSelection */
EdError yank_selection(const TextSelection *sel);

/* Paste operations */
EdError paste_from_register(Buffer *buf, char reg_name ,bool after);
void yank_data_free(YankData *yd);
YankData yank_data_new(Buffer *buf, const TextSelection *sel);
