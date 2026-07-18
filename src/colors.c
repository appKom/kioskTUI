#include "colors.h"
#include <ncurses.h>
#include <stdio.h>

enum { RGB_SCALE = 1000, CUBE_STEPS = 5, CUBE_OFFSET = 127, CUBE_BASE = 16 };

static void hex_to_rgb(const char *hex, int *r, int *g, int *b) {
  if (!hex) {
    *r = *g = *b = 0;
    return;
  }
  if (*hex == '#') {
    ++hex;
  }
  unsigned int ri = 0;
  unsigned int gi = 0;
  unsigned int bi = 0;
  sscanf(hex, "%02x%02x%02x", &ri, &gi, &bi);
  *r = (int)ri;
  *g = (int)gi;
  *b = (int)bi;
}

static void rgb_to_ncurses(int r, int g, int b, short *nr, short *ng,
                           short *nb) {
  *nr = (short)((r * RGB_SCALE) / 255);
  *ng = (short)((g * RGB_SCALE) / 255);
  *nb = (short)((b * RGB_SCALE) / 255);
}

static int rgb_to_256(int r, int g, int b) {
  int ri = ((r * CUBE_STEPS) + CUBE_OFFSET) / 255;
  int gi = ((g * CUBE_STEPS) + CUBE_OFFSET) / 255;
  int bi = ((b * CUBE_STEPS) + CUBE_OFFSET) / 255;
  return CUBE_BASE + (36 * ri) + (6 * gi) + bi;
}

int init_pair_from_hex(short pair_number, short slot, const char *hex) {
  if (!has_colors()) {
    return -1;
  }
  int r;
  int g;
  int b;
  hex_to_rgb(hex, &r, &g, &b);
  if (can_change_color() && slot >= 0 && slot < COLORS) {
    short nr;
    short ng;
    short nb;
    rgb_to_ncurses(r, g, b, &nr, &ng, &nb);
    init_color(slot, nr, ng, nb);
    init_pair(pair_number, slot, -1);
    return 0;
  }
  if (COLORS >= 256) {
    int idx = rgb_to_256(r, g, b);
    if (idx >= COLORS) {
      idx = COLORS - 1;
    }
    init_pair(pair_number, (short)idx, -1);
    return 0;
  }
  return -2;
}
