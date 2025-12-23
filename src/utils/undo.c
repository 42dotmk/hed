#include "undo.h"

void undo_init(void) {}

void undo_set_cap(size_t bytes) { (void)bytes; }

void undo_begin_group(void) {}
void undo_commit_group(void) {}

void undo_open_insert_group(void) {}
void undo_close_insert_group(void) {}

void undo_clear_redo(void) {}

void undo_on_mode_change(EditorMode old_mode, EditorMode new_mode) {
    (void)old_mode;
    (void)new_mode;
}
void undo_push_insert(int y, int x, const char *data, size_t len, int cy_before,
                      int cx_before, int cy_after, int cx_after) {
    (void)y;
    (void)x;
    (void)data;
    (void)len;
    (void)cy_before;
    (void)cx_before;
    (void)cy_after;
    (void)cx_after;
}
void undo_push_delete(int y, int x, const char *data, size_t len, int cy_before,
                      int cx_before, int cy_after, int cx_after) {
    (void)y;
    (void)x;
    (void)data;
    (void)len;
    (void)cy_before;
    (void)cx_before;
    (void)cy_after;
    (void)cx_after;
}
int undo_perform(void) { return 0; }

int redo_perform(void) { return 0; }

int undo_is_applying(void) { return 0; }

