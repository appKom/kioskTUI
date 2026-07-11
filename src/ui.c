#define _XOPEN_SOURCE 700
#include "ui.h"
#include "banners.h"
#include "colors.h"
#include "config.h"
#include "data.h"
#include "draw.h"
#include "utf8.h"
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t resized = 0;
static void handle_winch(int sig) {
  (void)sig;
  resized = 1;
}

static int clamp_i(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
static int max_i(int a, int b) { return a > b ? a : b; }

#define SLIDE_SECONDS 5
#define TICK_MS 500
#define RELOAD_SECONDS 1
#define MIN_CHART_H 10
#define MIN_LATEST_H 8
#define LATEST_ITEM_MAX 16
#define LEADERBOARD_ITEMS 5

typedef enum { LAYOUT_STACKED, LAYOUT_SIDE_BY_SIDE } LayoutMode;

typedef struct {
  LayoutMode mode;
  int banner_lines, fame_lines, lame_lines;
  int top_y, bot_y, chart_y, latest_y;
  int win_h, chart_h, latest_h, win_w, half_w;
} Layout;

static int ideal_panel_h(int banner_lines) {
  int h = 1 + banner_lines + 1 + LEADERBOARD_ITEMS + 1;
  return h < MIN_WIN_H ? MIN_WIN_H : h;
}

static int try_side_by_side(int rows, int cols, Layout *out) {
  if (cols <= rows)
    return 0;

  int win_w = cols - SIDE_MARGIN * 2;
  int half_w = win_w / 2;
  if (half_w < MIN_WIN_W)
    return 0;

  int banner_lines = out->banner_lines;
  int fame_lines = out->fame_lines;
  int lame_lines = out->lame_lines;

  int *trim[3] = {&banner_lines, &fame_lines, &lame_lines};
  int max_iters = banner_lines + fame_lines + lame_lines;

  for (int iter = 0; iter <= max_iters; ++iter) {
    int avail = rows - (banner_lines + 1 + FOOTER_LINES);

    int win_h = max_i(ideal_panel_h(fame_lines), ideal_panel_h(lame_lines));
    int latest_h = MIN_LATEST_H;
    int chart_h = avail - win_h - latest_h;

    if (chart_h >= MIN_CHART_H) {
      int y = banner_lines + 1;
      int top_y = y;
      y += win_h;
      int chart_y = y;
      y += chart_h;
      int latest_y = y;
      y += latest_h + FOOTER_LINES;

      if (y <= rows) {
        out->mode = LAYOUT_SIDE_BY_SIDE;
        out->banner_lines = banner_lines;
        out->fame_lines = fame_lines;
        out->lame_lines = lame_lines;
        out->top_y = top_y;
        out->bot_y = top_y;
        out->chart_y = chart_y;
        out->latest_y = latest_y;
        out->win_h = win_h;
        out->chart_h = chart_h;
        out->latest_h = latest_h;
        out->win_w = win_w;
        out->half_w = half_w;
        return 1;
      }
    }

    bool trimmed = 0;
    for (int k = 0; k < 3 && !trimmed; ++k) {
      int idx = (iter + k) % 3;
      if (*trim[idx] > 1) {
        --(*trim[idx]);
        trimmed = 1;
      }
    }
    if (!trimmed)
      break;
  }
  return 0;
}

static int try_stacked(int rows, int cols, Layout *out) {
  int win_w = cols - SIDE_MARGIN * 2;
  if (win_w < MIN_WIN_W)
    return 0;

  int banner_lines = out->banner_lines;
  int fame_lines = out->fame_lines;
  int lame_lines = out->lame_lines;

  int *trim[3] = {&banner_lines, &fame_lines, &lame_lines};
  int max_iters = banner_lines + fame_lines + lame_lines;

  for (int iter = 0; iter <= max_iters; ++iter) {
    int avail = rows - (banner_lines + 1 + FOOTER_LINES);

    int top_h = ideal_panel_h(fame_lines);
    int bot_h = ideal_panel_h(lame_lines);
    int latest_h = MIN_LATEST_H;
    int chart_h = avail - top_h - bot_h - latest_h;

    if (chart_h >= MIN_CHART_H) {
      int y = banner_lines + 1;
      int top_y = y;
      y += top_h;
      int bot_y = y;
      y += bot_h;
      int chart_y = y;
      y += chart_h;
      int latest_y = y;
      y += latest_h + FOOTER_LINES;

      if (y <= rows) {
        out->mode = LAYOUT_STACKED;
        out->banner_lines = banner_lines;
        out->fame_lines = fame_lines;
        out->lame_lines = lame_lines;
        out->top_y = top_y;
        out->bot_y = bot_y;
        out->chart_y = chart_y;
        out->latest_y = latest_y;
        out->win_h = top_h;
        out->chart_h = chart_h;
        out->latest_h = latest_h;
        out->win_w = win_w;
        out->half_w = win_w;
        return 1;
      }
    }

    bool trimmed = 0;
    for (int k = 0; k < 3 && !trimmed; ++k) {
      int idx = (iter + k) % 3;
      if (*trim[idx] > 1) {
        --(*trim[idx]);
        trimmed = 1;
      }
    }
    if (!trimmed)
      break;
  }
  return 0;
}

static int compute_layout(int rows, int cols, Layout *out) {
  if (cols < MIN_COLS || rows < MIN_ROWS)
    return 0;
  if (try_side_by_side(rows, cols, out))
    return 1;
  return try_stacked(rows, cols, out);
}

static WINDOW *resize_or_create_win(WINDOW *w, int h, int ww, int y, int x) {
  if (w) {
    wresize(w, h, ww);
    mvwin(w, y, x);
    return w;
  }
  return newwin(h, ww, y, x);
}

static void delete_windows(WINDOW **top, WINDOW **bot, WINDOW **chart,
                           WINDOW **latest) {
  if (*top) {
    delwin(*top);
    *top = NULL;
  }
  if (*bot) {
    delwin(*bot);
    *bot = NULL;
  }
  if (*chart) {
    delwin(*chart);
    *chart = NULL;
  }
  if (*latest) {
    delwin(*latest);
    *latest = NULL;
  }
}

static int apply_layout(WINDOW **top, WINDOW **bot, WINDOW **chart,
                        WINDOW **latest, const Layout *lo) {
  if (lo->mode == LAYOUT_SIDE_BY_SIDE) {
    int left_w = lo->half_w;
    int right_w = lo->win_w - lo->half_w;
    *top =
        resize_or_create_win(*top, lo->win_h, left_w, lo->top_y, SIDE_MARGIN);
    *bot = resize_or_create_win(*bot, lo->win_h, right_w, lo->top_y,
                                SIDE_MARGIN + left_w);
  } else {
    int top_h = ideal_panel_h(lo->fame_lines);
    int bot_h = ideal_panel_h(lo->lame_lines);
    *top = resize_or_create_win(*top, top_h, lo->win_w, lo->top_y, SIDE_MARGIN);
    *bot = resize_or_create_win(*bot, bot_h, lo->win_w, lo->bot_y, SIDE_MARGIN);
  }
  *chart = resize_or_create_win(*chart, lo->chart_h, lo->win_w, lo->chart_y,
                                SIDE_MARGIN);
  *latest = resize_or_create_win(*latest, lo->latest_h, lo->win_w, lo->latest_y,
                                 SIDE_MARGIN);
  if (*top && *bot && *chart && *latest)
    return 1;
  delete_windows(top, bot, chart, latest);
  return 0;
}

static void draw_banner(int cols, int banner_lines) {
  if (has_colors())
    wattron(stdscr, COLOR_PAIR(CP_BANNER) | A_BOLD);
  const char **lines = draw_banner_ptr();
  for (int i = 0; i < banner_lines; ++i) {
    int disp = utf8_display_width(lines[i]);
    int x = (cols - disp) / 2;
    if (x < 0)
      x = 0;
    if (x + disp > cols)
      x = (cols > disp) ? cols - disp : 0;
    mvprintw(i, x, "%s", lines[i]);
  }
  if (has_colors())
    wattroff(stdscr, COLOR_PAIR(CP_BANNER) | A_BOLD);
}

static void draw_top_window(WINDOW *win, int fame_lines) {
  werase(win);
  wborder(win, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE, ACS_ULCORNER,
          ACS_URCORNER, ACS_LLCORNER, ACS_LRCORNER);
  int win_h = getmaxy(win);
  int max_fame = clamp_i(fame_lines, 0, win_h - 3);
  if (max_fame > 0)
    draw_block_centered(win, draw_fame_ptr(), max_fame, 1, CP_TOP2);
  int rows_start = max_i(2, 1 + max_fame + 1);
  int capacity = clamp_i(win_h - rows_start - 1, 0, data_count());
  draw_rows_in_win_centered_safe(win, rows_start, capacity, 0);
}

static void draw_bot_window(WINDOW *win, int lame_lines) {
  werase(win);
  wborder(win, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE, ACS_ULCORNER,
          ACS_URCORNER, ACS_LLCORNER, ACS_LRCORNER);
  int win_h = getmaxy(win);
  int max_lame = clamp_i(lame_lines, 0, win_h - 3);
  if (max_lame > 0)
    draw_block_centered(win, draw_lame_ptr(), max_lame, 1, CP_LAME);
  int rows_start = max_i(2, 1 + max_lame + 1);
  int capacity = clamp_i(win_h - rows_start - 1, 0, data_count());
  draw_rows_in_win_centered_safe(win, rows_start, capacity, 1);
}

static void draw_latest_window(WINDOW *win) {
  werase(win);
  wborder(win, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE, ACS_ULCORNER,
          ACS_URCORNER, ACS_LLCORNER, ACS_LRCORNER);

  int win_w = getmaxx(win);
  int win_h = getmaxy(win);

  static const char title[] = " LATEST PURCHASE ";
  int title_x = (win_w - (int)(sizeof title - 1)) / 2;
  if (title_x < 1)
    title_x = 1;
  if (has_colors())
    wattron(win, COLOR_PAIR(CP_BANNER) | A_BOLD);
  mvwprintw(win, 0, title_x, "%s", title);
  if (has_colors())
    wattroff(win, COLOR_PAIR(CP_BANNER) | A_BOLD);

  PurchaseItem items[LATEST_ITEM_MAX];
  long ts = 0;
  int n = data_latest_purchase(&ts, items, LATEST_ITEM_MAX);

  if (n == 0) {
    static const char nd[] = "no data";
    int x = (win_w - (int)(sizeof nd - 1)) / 2;
    if (x < 1)
      x = 1;
    mvwprintw(win, (win_h - 1) / 2, x, "%s", nd);
    return;
  }

  char timebuf[32] = "";
  time_t t = (time_t)ts;
  struct tm *tp = localtime(&t);
  if (tp)
    strftime(timebuf, sizeof timebuf, "%a %d %b %Y  %H:%M:%S", tp);
  int tx = (win_w - (int)strlen(timebuf)) / 2;
  if (tx < 1)
    tx = 1;
  if (has_colors())
    wattron(win, A_DIM);
  mvwprintw(win, 1, tx, "%s", timebuf);
  if (has_colors())
    wattroff(win, A_DIM);

  int max_rows = win_h - 3;
  if (max_rows < 1)
    return;
  int page_rows = (n > max_rows) ? max_rows - 1 : max_rows;
  if (page_rows < 1)
    page_rows = 1;
  int n_pages = (n + page_rows - 1) / page_rows;
  int page = (n_pages > 1) ? (int)(time(NULL) / 3) % n_pages : 0;
  int start_idx = page * page_rows;
  int show = n - start_idx;
  if (show > page_rows)
    show = page_rows;

  for (int i = 0; i < show; ++i) {
    int idx = start_idx + i;
    char buf[PRODUCT_NAME_MAX + 20];
    int len =
        snprintf(buf, sizeof buf, "%s \xe2\x80\x94 %d %s", items[idx].name,
                 items[idx].units, items[idx].units == 1 ? "unit" : "units");
    int x = (win_w - len) / 2;
    if (x < 1)
      x = 1;
    if (x + len > win_w - 1)
      buf[win_w - 1 - x] = '\0';
    if (has_colors())
      wattron(win, COLOR_PAIR(CP_TOP2) | A_BOLD);
    mvwprintw(win, 2 + i, x, "%s", buf);
    if (has_colors())
      wattroff(win, COLOR_PAIR(CP_TOP2) | A_BOLD);
  }

  if (n_pages > 1) {
    char indicator[24];
    int ilen =
        snprintf(indicator, sizeof indicator, "%d / %d", page + 1, n_pages);
    int ix = (win_w - ilen) / 2;
    if (ix < 1)
      ix = 1;
    if (has_colors())
      wattron(win, A_DIM);
    mvwprintw(win, 2 + page_rows, ix, "%s", indicator);
    if (has_colors())
      wattroff(win, A_DIM);
  }
}

void ui_init(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = handle_winch;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGWINCH, &sa, NULL);
  draw_load_banners();
  data_sort_by_qty_desc();
}

void ui_run(void) {
  int rows, cols;
  getmaxyx(stdscr, rows, cols);

  Layout lo = {
      .banner_lines = draw_banner_lines(),
      .fame_lines = draw_fame_lines(),
      .lame_lines = draw_lame_lines(),
  };

  WINDOW *top = NULL, *bot = NULL, *chart = NULL, *latest = NULL;
  int have_win = 0;

  if (compute_layout(rows, cols, &lo))
    have_win = apply_layout(&top, &bot, &chart, &latest, &lo);

  int slide = 0;
  int n_slides = draw_chart_n_slides();
  time_t slide_start = time(NULL);
  time_t last_reload = time(NULL);

  timeout(TICK_MS);

  while (1) {
    time_t now = time(NULL);

    if ((int)(now - slide_start) >= SLIDE_SECONDS) {
      slide = (slide + 1) % n_slides;
      slide_start = now;
    }

    if ((int)(now - last_reload) >= RELOAD_SECONDS) {
      data_reload();
      data_sort_by_qty_desc();
      n_slides = draw_chart_n_slides();
      if (slide >= n_slides)
        slide = 0;
      last_reload = now;
    }

    if (resized) {
      resized = 0;
      getmaxyx(stdscr, rows, cols);
      lo.banner_lines = draw_banner_lines();
      lo.fame_lines = draw_fame_lines();
      lo.lame_lines = draw_lame_lines();
      if (compute_layout(rows, cols, &lo))
        have_win = apply_layout(&top, &bot, &chart, &latest, &lo);
      else {
        delete_windows(&top, &bot, &chart, &latest);
        have_win = 0;
      }
      resizeterm(rows, cols);
      clearok(curscr, TRUE);
    }

    int remaining = SLIDE_SECONDS - (int)(now - slide_start);
    werase(stdscr);
    draw_banner(cols, lo.banner_lines);
    printw_centered_stdscr_safe(
        rows - FOOTER_LINES, cols,
        "Slide %d/%d | Next in %ds | Tab: advance | q: quit | %dx%d", slide + 1,
        n_slides, remaining, cols, rows);

    if (have_win) {
      draw_top_window(top, lo.fame_lines);
      draw_bot_window(bot, lo.lame_lines);
      draw_chart_window(chart, slide, n_slides);
      draw_latest_window(latest);
      wnoutrefresh(stdscr);
      wnoutrefresh(top);
      wnoutrefresh(bot);
      wnoutrefresh(chart);
      wnoutrefresh(latest);
    } else {
      draw_rows_on_stdscr_centered_safe(lo.banner_lines + 2, rows, cols);
      wnoutrefresh(stdscr);
    }

    doupdate();

    int ch = getch();
    if (ch == 'q' || ch == 'Q')
      break;
    if (ch == '\t' || ch == KEY_RIGHT) {
      slide = (slide + 1) % n_slides;
      slide_start = time(NULL);
    }
    if (ch == KEY_LEFT) {
      slide = (slide - 1 + n_slides) % n_slides;
      slide_start = time(NULL);
    }
  }

  delete_windows(&top, &bot, &chart, &latest);
}

void ui_shutdown(void) { draw_free_banners(); }
