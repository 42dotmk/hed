#ifndef UNDO_H
#define UNDO_H

#include "editor.h"
#include <stddef.h>

typedef enum { UREC_INSERT_TEXT = 1, UREC_DELETE_TEXT = 2 } UndoRecType;

typedef struct {
    UndoRecType type;
    int y; /* start position (line) */
    int x; /* start position (column) */
    /* payload is required for INSERT (text inserted) and DELETE (text deleted)
     */
    SizedStr payload;
    int cy_before, cx_before; /* cursor before operation */
    int cy_after, cx_after;   /* cursor after operation */
    int group_id;             /* logical action grouping */
} UndoRec;

/* Initialization and configuration */
void undo_init(void);
void undo_set_cap(size_t bytes);

/* Group lifecycle (for insert runs and single-shot ops) */
void undo_begin_group(void);
void undo_commit_group(void);
void undo_open_insert_group(void);
void undo_close_insert_group(void);
void undo_clear_redo(void);

/* Mode change hook (commit insert group when leaving INSERT) */
void undo_on_mode_change(EditorMode old_mode, EditorMode new_mode);

/* Logging helpers (call around edits). These also clear redo. */
void undo_push_insert(int y, int x, const char *data, size_t len, int cy_before,
                      int cx_before, int cy_after, int cx_after);
void undo_push_delete(int y, int x, const char *data, size_t len, int cy_before,
                      int cx_before, int cy_after, int cx_after);

/* Perform undo/redo. Return 1 if something was done, 0 otherwise. */
int undo_perform(void);
int redo_perform(void);

/* True while applying undo/redo (to skip logging). */
int undo_is_applying(void);

#endif /* UNDO_H */
