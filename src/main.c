#include "colors.h"
#include "ui.h"
#include <locale.h>
#include <ncurses.h>
#include <stdio.h>

#define BANNER_HEX "#F9B759"
enum { BANNER_COLOR_SLOT = 8 };
#define LINE1_HEX "#FF4F4F"
#define LINE2_HEX "#4FFF91"
#define LINE3_HEX "#4FB8FF"
#define LINE4_HEX "#FFD700"
#define LINE5_HEX "#FF8CFF"

static void init_colors(void) {
  if (!has_colors()) {
    return;
  }
  start_color();
  use_default_colors();
  init_pair(CP_TOP1, COLOR_YELLOW, -1);
  init_pair(CP_TOP2, COLOR_CYAN, -1);
  init_pair(CP_ALERT, COLOR_RED, -1);
  init_pair(CP_ACCENT, COLOR_MAGENTA, -1);
  init_pair(CP_LAME, COLOR_RED, -1);
  if (COLORS <= 8 ||
      init_pair_from_hex(CP_BANNER, BANNER_COLOR_SLOT, BANNER_HEX) < 0) {
    init_pair(CP_BANNER, COLOR_YELLOW, COLOR_BLACK);
  }
  if (init_pair_from_hex(CP_LINE_1, 9, LINE1_HEX) < 0) {
    init_pair(CP_LINE_1, COLOR_RED, -1);
  }
  if (init_pair_from_hex(CP_LINE_2, 10, LINE2_HEX) < 0) {
    init_pair(CP_LINE_2, COLOR_GREEN, -1);
  }
  if (init_pair_from_hex(CP_LINE_3, 11, LINE3_HEX) < 0) {
    init_pair(CP_LINE_3, COLOR_CYAN, -1);
  }
  if (init_pair_from_hex(CP_LINE_4, 12, LINE4_HEX) < 0) {
    init_pair(CP_LINE_4, COLOR_YELLOW, -1);
  }
  if (init_pair_from_hex(CP_LINE_5, 13, LINE5_HEX) < 0) {
    init_pair(CP_LINE_5, COLOR_MAGENTA, -1);
  }
}
int main(void) {
  freopen("leaderboard.log", "a", stderr);

  setlocale(LC_ALL, "");
  initscr();
  cbreak();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  init_colors();
  ui_init();
  ui_run();
  ui_shutdown();
  endwin();
  return 0;
}
