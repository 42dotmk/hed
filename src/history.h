#ifndef HISTORY_H
#define HISTORY_H

/* Command history structure and API */

typedef struct {
    char **items;       /* newest at index 0 */
    int len;
    int cap;
    int idx;            /* browsing index; -1 when not browsing */
    char saved_line[80];
    int saved_len;
    char prefix[80];
    int prefix_len;
} CmdHistory;

/* Initialize and load from history file */
void hist_init(CmdHistory *h);
/* Free memory */
void hist_free(CmdHistory *h);
/* Add a new entry (in-mem and persist) */
void hist_add(CmdHistory *h, const char *line);
/* Reset browsing state */
void hist_reset_browse(CmdHistory *h);
/* Browse older entries matching current input prefix; copies into out */
int hist_browse_up(CmdHistory *h, const char *current_input, int current_len,
                   char *out, int out_cap);
/* Browse newer entries; if none, restores saved input */
int hist_browse_down(CmdHistory *h, char *out, int out_cap, int *restored);

/* Accessors */
int hist_len(const CmdHistory *h);
const char *hist_get(const CmdHistory *h, int idx);

#endif /* HISTORY_H */

