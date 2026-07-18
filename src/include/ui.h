#ifndef UI_H
#define UI_H
#include <ncurses.h>

enum {
MAX_CONFETTI = 500
};

void ui_init(void);
void ui_run(void);
void ui_shutdown(void);

typedef struct {
  int x;
  int y;
  int speed;
  int dx;
  int color;
  char ch;
} Confetti;

#endif
