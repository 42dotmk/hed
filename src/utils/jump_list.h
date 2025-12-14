#ifndef JUMP_LIST_H
#define JUMP_LIST_H

/* Jump list for buffer navigation (like Vim's jumplist) */

typedef struct {
    char* filepath;
    int cursor_x;
    int cursor_y;
} JumpEntry;

typedef struct {
    JumpEntry *entries;
    int len;
    int cap;
    int current; /* Current position in the list (-1 if not navigating) */
} JumpList;

/* Initialize jump list */
void jump_list_init(JumpList *jl);

/* Free jump list */
void jump_list_free(JumpList *jl);

/* Add a new jump entry (buffer switch) */
void jump_list_add(JumpList *jl, char *filepath, int cursor_x, int cursor_y);

/* Navigate backward in jump list (Ctrl-O) */
/* Returns 1 if successful, 0 if at beginning */
int jump_list_backward(JumpList *jl, char **filepath, int *out_x, int *out_y);

/* Navigate forward in jump list (Ctrl-I) */
/* Returns 1 if successful, 0 if at end */
int jump_list_forward(JumpList *jl, char **filepath, int *out_x, int *out_y);

/* Reset navigation state */
void jump_list_reset_navigation(JumpList *jl);

#endif /* JUMP_LIST_H */
