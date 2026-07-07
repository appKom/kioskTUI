#pragma once
#include <ncurses.h>
#include <stdbool.h>

void draw_block_centered(WINDOW *w, const char **lines, int count, int start_y,
                         int color_pair);
void mvwprintw_centered_safe(WINDOW *w, int y, const char *fmt, ...);
void printw_centered_stdscr_safe(int y, int cols, const char *fmt, ...);

void draw_rows_in_win_centered_safe(WINDOW *win, int start_y, int capacity,
                                    int reverse);
void draw_rows_on_stdscr_centered_safe(int start_y, int rows, int cols);

/* Returns number of chart slides based on current product count. */
int draw_chart_n_slides(void);
void draw_chart_window(WINDOW *win, int slide, int n_slides);

void draw_load_banners(void);
void draw_free_banners(void);
int draw_banner_lines(void);
const char **draw_banner_ptr(void);
int draw_fame_lines(void);
const char **draw_fame_ptr(void);
int draw_lame_lines(void);
const char **draw_lame_ptr(void);
