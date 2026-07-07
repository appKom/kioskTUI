#ifndef COLORS_H
#define COLORS_H
#include <ncurses.h>

/* Color pair identifiers used across the UI (start at 1) */
enum {
  CP_TOP1 = 1,
  CP_TOP2,
  CP_ALERT,
  CP_ACCENT,
  CP_BANNER,
  CP_LAME,
  /* Per-item line colors for the chart (one per slot in a 5-item slide) */
  CP_LINE_1,
  CP_LINE_2,
  CP_LINE_3,
  CP_LINE_4,
  CP_LINE_5,
  CP_MAX
};

int init_pair_from_hex(short pair, short color_slot, const char *hex);

#endif /* COLORS_H */
