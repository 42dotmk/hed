#ifndef TERMINAL_H
#define TERMINAL_H

#include <termios.h>

/* Global terminal state */
extern struct termios orig_termios;

/* Terminal functions */
void die(const char *s);
void disable_raw_mode(void);
void enable_raw_mode(void);
int get_cursor_position(int *rows, int *cols);
int get_window_size(int *rows, int *cols);

/* File I/O */
char *buf_rows_to_string(int *buflen);
void buf_open(char *filename);
void buf_save(void);

/* Output / Rendering */
void buf_scroll(void);
void ed_draw_rows(char *ab, int *ablen, int maxlen);
void ed_draw_status_bar(char *ab, int *ablen, int maxlen);
void ed_draw_message_bar(char *ab, int *ablen, int maxlen);
void buf_refresh_screen(void);

#endif
