#ifndef FOLD_METHODS_H
#define FOLD_METHODS_H

#include "buf/buffer.h"

/*
 * FOLD DETECTION METHODS
 * ======================
 *
 * This module provides different strategies for automatically detecting
 * fold regions in a buffer. Each method scans the buffer content and
 * generates appropriate fold regions.
 *
 * Available methods:
 * - Bracket-based: Detects folds based on opening/closing brackets { }
 * - Indent-based: Detects folds based on indentation levels
 *
 * Note: FoldMethod enum is defined in buf/buffer.h
 */

/*
 * Detect and generate folds using bracket matching { }
 * Scans the buffer for opening and closing brackets and creates
 * fold regions for each matched pair.
 */
void fold_detect_brackets(Buffer *buf);

/*
 * Detect and generate folds based on indentation levels
 * Creates fold regions for consecutive lines with deeper indentation
 * than the starting line.
 */
void fold_detect_indent(Buffer *buf);

/*
 * Apply the specified fold detection method to a buffer
 * Clears existing folds and regenerates them using the chosen method.
 */
void fold_apply_method(Buffer *buf, FoldMethod method);

#endif /* FOLD_METHODS_H */
