#include "editor.h"
#include "ui/winmodal.h"
#include <stdlib.h>

Window *winmodal_create(int x, int y, int width, int height) {
    /* Allocate a new modal window */
    Window *modal = malloc(sizeof(Window));
    if (!modal)
        return NULL;

    /* Calculate centered position if requested */
    if (x == -1) {
        x = (E.screen_cols - width) / 2;
        if (x < 1)
            x = 1;
    }
    if (y == -1) {
        y = (E.screen_rows - height) / 2;
        if (y < 1)
            y = 1;
    }

    /* Ensure modal fits on screen */
    if (x + width > E.screen_cols)
        x = E.screen_cols - width;
    if (y + height > E.screen_rows)
        y = E.screen_rows - height;
    if (x < 1)
        x = 1;
    if (y < 1)
        y = 1;

    /* Initialize window fields */
    modal->left = x;
    modal->top = y;
    modal->width = width;
    modal->height = height;
    modal->buffer_index = -1; /* No buffer attached by default */
    modal->focus = 0;
    modal->is_quickfix = 0;
    modal->is_modal = 1; /* Mark as modal */
    modal->visible = 0;  /* Hidden by default */
    modal->wrap = 0;
    modal->row_offset = 0;
    modal->col_offset = 0;
    modal->cursor.x = 0;
    modal->cursor.y = 0;
    modal->gutter_mode = 0;
    modal->gutter_fixed_width = 0;
    modal->sel.type = SEL_NONE;
    modal->sel.anchor_y = 0;
    modal->sel.anchor_x = 0;
    modal->sel.cursor_y = 0;
    modal->sel.cursor_x = 0;
    modal->sel.anchor_rx = 0;
    modal->sel.block_start_rx = 0;
    modal->sel.block_end_rx = 0;

    return modal;
}

Window *winmodal_create_anchored(int anchor_x, int anchor_y,
                                 int width, int height,
                                 WModalAnchor prefer) {
    int below_y     = anchor_y + 1;
    int space_below = E.screen_rows - below_y + 1; /* rows from below_y inclusive */
    int space_above = anchor_y - 1;                /* rows above anchor_y */

    int place_below;
    if (prefer == WMODAL_ABOVE) {
        place_below = (space_above < height && space_below > space_above);
    } else if (prefer == WMODAL_BELOW) {
        place_below = !(space_below < height && space_above > space_below);
    } else { /* WMODAL_AUTO */
        place_below = (space_below >= space_above);
    }

    int y;
    if (place_below) {
        y = below_y;
        int avail = E.screen_rows - y + 1;
        if (avail < 1) { y = E.screen_rows; avail = 1; }
        if (height > avail) height = avail;
    } else {
        if (height > space_above) height = space_above > 0 ? space_above : 1;
        y = anchor_y - height;
        if (y < 1) y = 1;
    }
    if (height < 1) height = 1;

    return winmodal_create(anchor_x, y, width, height);
}

void winmodal_show(Window *modal) {
    if (!modal || !modal->is_modal)
        return;

    /* Hide any currently shown modal */
    if (E.modal_window && E.modal_window != modal) {
        winmodal_hide(E.modal_window);
    }

    /* Show this modal */
    modal->visible = 1;
    modal->focus = 1;
    E.modal_window = modal;
}

void winmodal_hide(Window *modal) {
    if (!modal || !modal->is_modal)
        return;

    modal->visible = 0;
    modal->focus = 0;

    /* Clear the global modal reference if this was the current modal */
    if (E.modal_window == modal) {
        E.modal_window = NULL;
    }
}

void winmodal_destroy(Window *modal) {
    if (!modal)
        return;

    /* Hide the modal first */
    if (modal->visible) {
        winmodal_hide(modal);
    }

    /* Free the modal */
    free(modal);
}

int winmodal_is_shown(void) { return E.modal_window != NULL && E.modal_window->visible; }

Window *winmodal_current(void) {
    if (E.modal_window && E.modal_window->visible)
        return E.modal_window;
    return NULL;
}

Window *winmodal_from_current(void) {
    /* Get the current window from layout */
    if (arrlen(E.windows) == 0)
        return NULL;

    Window *current = &E.windows[E.current_window];

    /* Create a new modal with the window's content */
    Window *modal = malloc(sizeof(Window));
    if (!modal)
        return NULL;

    /* Copy all window state */
    *modal = *current;

    /* Resize modal to half screen size and center it */
    int width = E.screen_cols / 2;
    int height = E.screen_rows / 2;

    /* Ensure minimum size */
    if (width < 10)
        width = 10;
    if (height < 5)
        height = 5;

    modal->width = width;
    modal->height = height;

    modal->left = (E.screen_cols - width) / 2;
    if (modal->left < 1)
        modal->left = 1;

    modal->top = (E.screen_rows - height) / 2;
    if (modal->top < 1)
        modal->top = 1;

    /* Mark as modal */
    modal->is_modal = 1;
    modal->visible = 0; /* Hidden by default, caller should show it */
    modal->focus = 0;

    /* Hide the original window in the layout by marking it as detached */
    current->visible = 0;

    return modal;
}

void winmodal_to_layout(Window *modal) {
    if (!modal || !modal->is_modal)
        return;

    /* Hide the modal first */
    if (modal->visible) {
        winmodal_hide(modal);
    }

    /* Find the window in E.windows that was detached (visible=0) */
    /* and restore it with the modal's state */
    for (int i = 0; i < (int)arrlen(E.windows); i++) {
        if (!E.windows[i].visible &&
            E.windows[i].buffer_index == modal->buffer_index) {
            /* Restore window state from modal */
            E.windows[i].cursor = modal->cursor;
            E.windows[i].row_offset = modal->row_offset;
            E.windows[i].col_offset = modal->col_offset;
            E.windows[i].wrap = modal->wrap;
            E.windows[i].sel = modal->sel;
            E.windows[i].gutter_mode = modal->gutter_mode;
            E.windows[i].gutter_fixed_width = modal->gutter_fixed_width;

            /* Mark as visible again */
            E.windows[i].visible = 1;
            E.windows[i].is_modal = 0;

            /* Make it the current window */
            E.current_window = i;
            E.windows[i].focus = 1;

            break;
        }
    }

    /* Destroy the modal */
    free(modal);
}
