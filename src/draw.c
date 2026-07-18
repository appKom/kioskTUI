#define _XOPEN_SOURCE 700
#include "draw.h"
#include "banners.h"
#include "colors.h"
#include "config.h"
#include "data.h"
#include "utf8.h"
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PRINT_BUF_SIZE 512
#define MAX_NAME_COLS 24
#define REV_COL_W 12

static char **loaded_banner = NULL;
static char **loaded_fame = NULL;
static char **loaded_lame = NULL;
static const char *empty_banner[] = {"", NULL};

static void center_print(WINDOW *w, int y, const char *s) {
  if (!w || !s)
    return;
  int win_cols = getmaxx(w);
  int disp_width = utf8_display_width(s);
  int x = (win_cols - disp_width) / 2;
  if (x < 0)
    x = 0;
  mvwprintw(w, y, x, "%s", s);
}

static int clamp_x_to_inner(int x, int len, int win_cols) {
  int min_x = 1, max_x = win_cols - 2 - len;
  if (x < min_x)
    return min_x;
  if (x > max_x)
    return (max_x >= min_x) ? max_x : min_x;
  return x;
}

static int count_lines(char **lines) {
  int n = 0;
  if (lines)
    while (lines[n])
      ++n;
  return n;
}

static int clamp_i(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void draw_block_centered(WINDOW *w, const char **lines, int count, int start_y,
                         int color_pair) {
  if (!w || !lines || count <= 0)
    return;
  bool use_color = color_pair && has_colors();
  if (use_color)
    wattron(w, COLOR_PAIR(color_pair) | A_BOLD);
  for (int i = 0; i < count; ++i) {
    if (!lines[i])
      break;
    center_print(w, start_y + i, lines[i]);
  }
  if (use_color)
    wattroff(w, COLOR_PAIR(color_pair) | A_BOLD);
}

void mvwprintw_centered_safe(WINDOW *w, int y, const char *fmt, ...) {
  if (!w || !fmt)
    return;
  char buf[PRINT_BUF_SIZE];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  int len = utf8_display_width(buf);
  int win_cols = getmaxx(w);
  int x = clamp_x_to_inner((win_cols - len) / 2, len, win_cols);
  mvwprintw(w, y, x, "%s", buf);
}

void printw_centered_stdscr_safe(int y, int cols, const char *fmt, ...) {
  if (!fmt)
    return;
  char buf[PRINT_BUF_SIZE];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  int len = utf8_display_width(buf);
  int x = (cols - len) / 2;
  if (x < 0)
    x = 0;
  if (x + len > cols)
    x = (cols > len) ? cols - len : 0;
  mvprintw(y, x, "%s", buf);
}

static void fmt_revenue(char *buf, size_t bufsz, int qty, int price_ore) {
  int nok = (qty * price_ore) / 100;
  if (nok >= 1000000)
    snprintf(buf, bufsz, "%d %03d %03d kr", nok / 1000000, (nok / 1000) % 1000,
             nok % 1000);
  else if (nok >= 1000)
    snprintf(buf, bufsz, "%d %03d kr", nok / 1000, nok % 1000);
  else
    snprintf(buf, bufsz, "%d kr", nok);
}

static void fmt_row(char *buf, size_t bufsz, int rank, const char *name,
                    int qty, int price_ore, int name_w, int trend) {
  char name_col[MAX_NAME_COLS + 4];
  int nw = utf8_display_width(name);
  if (nw <= name_w) {
    snprintf(name_col, sizeof name_col, "%s%*s", name, name_w - nw, "");
  } else {
    utf8_truncate_to_width(name, name_col, sizeof name_col, name_w - 1);
    strncat(name_col, "\xe2\x80\xa6", sizeof name_col - strlen(name_col) - 1);
  }

  /* ↑ U+2191 = \xe2\x86\x91, ↓ U+2193 = \xe2\x86\x93 */
  const char *arrow = (trend > 0)   ? "\xe2\x86\x91"
                      : (trend < 0) ? "\xe2\x86\x93"
                                    : " ";

  char rev_col[REV_COL_W + 8];
  if (price_ore > 0) {
    char rev[24];
    fmt_revenue(rev, sizeof rev, qty, price_ore);
    snprintf(rev_col, sizeof rev_col, "%*s", REV_COL_W, rev);
  } else {
    snprintf(rev_col, sizeof rev_col, "%*s", REV_COL_W, "");
  }

  snprintf(buf, bufsz, "%2d. %s %s %5d  %s", rank, name_col, arrow, qty,
           rev_col);
}

static const int top_row_color_pairs[] = {CP_TOP1, CP_TOP2, CP_ALERT};
#define TOP_ROW_COLORED 3

static int best_name_width(int total, int reverse) {
  int draw = (total < 5) ? total : 5;
  int max_w = 8;
  for (int i = 0; i < draw; ++i) {
    int idx = reverse ? (total - 1 - i) : i;
    const Item *item = data_get(idx);
    if (!item)
      continue;
    int w = utf8_display_width(item->product);
    if (w > max_w)
      max_w = w;
  }
  return (max_w < MAX_NAME_COLS) ? max_w : MAX_NAME_COLS;
}

void draw_rows_in_win_centered_safe(WINDOW *win, int start_y, int capacity,
                                    int reverse) {
  if (!win)
    return;
  int total = data_count();
  int draw = (total < 5) ? total : 5;
  int name_w = best_name_width(total, reverse);
  (void)capacity;

  for (int i = 0; i < draw; ++i) {
    int idx = reverse ? (total - 1 - i) : i;
    if (idx < 0 || idx >= total)
      break;
    const Item *item = data_get(idx);
    if (!item)
      break;

    int trend = data_trend(item->product);

    char row[PRINT_BUF_SIZE];
    fmt_row(row, sizeof row, i + 1, item->product, item->qty, item->price,
            name_w, trend);

    bool highlight = (i < TOP_ROW_COLORED) && has_colors();
    if (highlight)
      wattron(win, COLOR_PAIR(top_row_color_pairs[i]) | A_BOLD);
    mvwprintw_centered_safe(win, start_y + i, "%s", row);
    if (highlight)
      wattroff(win, COLOR_PAIR(top_row_color_pairs[i]) | A_BOLD);

    /* Overlay the trend arrow with its own color on non-highlighted rows. */
    if (!highlight && has_colors() && trend != 0) {
      int row_len = utf8_display_width(row);
      int win_cols = getmaxx(win);
      int row_x = clamp_x_to_inner((win_cols - row_len) / 2, row_len, win_cols);
      /* Arrow sits at: "NN. " (4) + name_w + " " (1) = name_w + 5 display cols
       */
      int arrow_x = row_x + 4 + name_w + 1;
      int cp = (trend > 0) ? CP_TOP2 : CP_ALERT;
      wattron(win, COLOR_PAIR(cp) | A_BOLD);
      mvwprintw(win, start_y + i, arrow_x, "%s",
                (trend > 0) ? "\xe2\x86\x91" : "\xe2\x86\x93");
      wattroff(win, COLOR_PAIR(cp) | A_BOLD);
    }
  }
}

void draw_rows_on_stdscr_centered_safe(int start_y, int rows, int cols) {
  (void)rows;
  int total = data_count();
  int draw = (total < 5) ? total : 5;
  int name_w = best_name_width(total, 0);

  for (int i = 0; i < draw; ++i) {
    const Item *item = data_get(i);
    if (!item)
      break;
    char row[PRINT_BUF_SIZE];
    fmt_row(row, sizeof row, i + 1, item->product, item->qty, item->price,
            name_w, data_trend(item->product));
    int len = utf8_display_width(row);
    int x = (cols - len) / 2;
    if (x < 0)
      x = 0;
    mvprintw(start_y + i, x, "%s", row);
  }
}

// chart
static const int line_colors[5] = {CP_LINE_1, CP_LINE_2, CP_LINE_3, CP_LINE_4,
                                   CP_LINE_5};

#define YAXIS_W 7
#define LEGEND_H 5

int draw_chart_n_slides(void) {
  int total = data_count();
  if (total <= 5)
    return 1;
  if (total <= 10)
    return 2;
  return 2 + ((total - 10) + 4) / 5;
}

static void slide_range(int slide, int total, int n_slides, int *out_start,
                        int *out_end, char *title_buf, size_t title_sz) {
  if (slide == 0) {
    *out_start = 0;
    *out_end = (total < 5) ? total : 5;
    snprintf(title_buf, title_sz, "Cumulative Sales -- Top 5");
  } else if (slide == 1) {
    *out_start = (total > 5) ? total - 5 : 0;
    *out_end = total;
    snprintf(title_buf, title_sz, "Cumulative Sales -- Bottom 5");
  } else {
    int mid_idx = slide - 2;
    int n_middle = n_slides - 2;
    *out_start = 5 + mid_idx * 5;
    *out_end = *out_start + 5;
    int max_end = (total > 5) ? total - 5 : total;
    if (*out_end > max_end)
      *out_end = max_end;
    snprintf(title_buf, title_sz, "Cumulative Sales -- Middle (%d/%d)",
             mid_idx + 1, n_middle);
  }
}

static int val_to_row(int val, int max_val, int plot_top, int plot_bot) {
  if (max_val <= 0)
    return plot_bot;
  int h = plot_bot - plot_top;
  return clamp_i(plot_bot - (int)((long)val * h / max_val), plot_top, plot_bot);
}

static int ts_to_col(long ts, long ts_first, long range_days, int plot_x0,
                     int plot_w) {
  if (range_days <= 1)
    return plot_x0;
  long day_offset = (ts - ts_first) / 86400;
  return plot_x0 + (int)((long)day_offset * (plot_w - 1) / (range_days - 1));
}

static void draw_line_segment(WINDOW *win, int prev_col, int prev_row,
                              int cur_col, int cur_row, int color_pair) {
  if (has_colors())
    wattron(win, COLOR_PAIR(color_pair) | A_BOLD);
  int dx = cur_col - prev_col;
  int dy = cur_row - prev_row;
  for (int step = 1; step <= dx; ++step) {
    int col = prev_col + step;
    int row = prev_row + (int)((long)dy * step / dx);
    int prev_interp = prev_row + (int)((long)dy * (step - 1) / dx);
    int delta = row - prev_interp;
    chtype ch;
    if (delta == 0)
      ch = '-';
    else if (delta < 0)
      ch = '/';
    else
      ch = '\\';
    if (delta < -1 || delta > 1)
      ch = '|';
    if (delta < -1)
      for (int r = prev_interp - 1; r > row; --r)
        mvwaddch(win, r, col, '|');
    else if (delta > 1)
      for (int r = prev_interp + 1; r < row; ++r)
        mvwaddch(win, r, col, '|');
    mvwaddch(win, row, col, ch);
  }
  if (has_colors())
    wattroff(win, COLOR_PAIR(color_pair) | A_BOLD);
}

void draw_chart_window(WINDOW *win, int slide, int n_slides) {
  if (!win)
    return;
  werase(win);
  wborder(win, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE, ACS_ULCORNER,
          ACS_URCORNER, ACS_LLCORNER, ACS_LRCORNER);

  int win_h = getmaxy(win);
  int win_w = getmaxx(win);
  int total = data_count();

  char title[64];
  int item_start, item_end;
  slide_range(slide, total, n_slides, &item_start, &item_end, title,
              sizeof title);
  int n_items = item_end - item_start;

  if (has_colors())
    wattron(win, COLOR_PAIR(CP_TOP2) | A_BOLD);
  mvwprintw(win, 1, 2, "%s", title);
  if (has_colors())
    wattroff(win, COLOR_PAIR(CP_TOP2) | A_BOLD);

  int days = data_daily_count();
  if (n_items <= 0 || days == 0) {
    mvwprintw_centered_safe(win, win_h / 2, "No data for this slide.");
    return;
  }

  int plot_top = 3;
  int plot_bot = win_h - 2 - LEGEND_H - 1;
  int plot_h = plot_bot - plot_top + 1;
  int plot_x0 = 1 + YAXIS_W;
  int plot_w = win_w - 2 - YAXIS_W;

  if (plot_h < 3 || plot_w < 4) {
    mvwprintw_centered_safe(win, win_h / 2, "Window too small.");
    return;
  }

  long ts_first = 0, ts_last = 0;
  data_daily_time(0, &ts_first);
  data_daily_time(days - 1, &ts_last);
  ts_first = ts_first - (ts_first % 86400);
  ts_last = ts_last - (ts_last % 86400);
  long range_days = (ts_last - ts_first) / 86400 + 1;

  int max_val = 1;
  for (int slot = 0; slot < n_items; ++slot) {
    const Item *item = data_get(item_start + slot);
    if (!item)
      continue;
    int cum = 0;
    for (int d = 0; d < days; ++d) {
      int v = data_daily_get(d, item->product);
      if (v > 0)
        cum += v;
    }
    if (cum > max_val)
      max_val = cum;
  }

  for (int row = plot_top; row <= plot_bot; ++row) {
    if (row == plot_top || row == plot_bot ||
        row == (plot_top + plot_bot) / 2) {
      int val = (int)((long)(plot_bot - row) * max_val / (plot_h - 1));
      mvwprintw(win, row, 1, "%5d |", val);
    } else {
      mvwprintw(win, row, 1 + YAXIS_W - 1, "|");
    }
  }

  static const char *month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int xlabel_row = plot_bot + 1;
  int last_label_x = plot_x0 - 99;
  int min_gap = 5;
  {
    long cur = ts_first;
    int prev_mon = -1;
    while (cur <= ts_last) {
      time_t t = (time_t)cur;
      struct tm *tm = localtime(&t);
      if (tm && tm->tm_mon != prev_mon) {
        int col = ts_to_col(cur, ts_first, range_days, plot_x0, plot_w);
        if (col - last_label_x >= min_gap && col + 3 <= plot_x0 + plot_w) {
          mvwprintw(win, xlabel_row, col, "%s", month_names[tm->tm_mon]);
          last_label_x = col;
        }
        prev_mon = tm->tm_mon;
      }
      cur += 86400;
    }
  }

  for (int slot = 0; slot < n_items; ++slot) {
    const Item *item = data_get(item_start + slot);
    if (!item)
      continue;
    int color_pair = line_colors[slot % 5];
    int prev_row = -1, prev_col = -1;
    int cumulative = 0;
    for (int d = 0; d < days; ++d) {
      int v = data_daily_get(d, item->product);
      if (v > 0)
        cumulative += v;
      if (cumulative == 0)
        continue;
      long ts = 0;
      data_daily_time(d, &ts);
      ts = ts - (ts % 86400);
      int cur_col = ts_to_col(ts, ts_first, range_days, plot_x0, plot_w);
      int cur_row = val_to_row(cumulative, max_val, plot_top, plot_bot);
      if (prev_row == -1) {
        if (has_colors())
          wattron(win, COLOR_PAIR(color_pair) | A_BOLD);
        mvwaddch(win, cur_row, cur_col, '*');
        if (has_colors())
          wattroff(win, COLOR_PAIR(color_pair) | A_BOLD);
      } else if (cur_col > prev_col) {
        draw_line_segment(win, prev_col, prev_row, cur_col, cur_row,
                          color_pair);
      } else {
        if (has_colors())
          wattron(win, COLOR_PAIR(color_pair) | A_BOLD);
        mvwaddch(win, cur_row, cur_col, '*');
        if (has_colors())
          wattroff(win, COLOR_PAIR(color_pair) | A_BOLD);
      }
      prev_row = cur_row;
      prev_col = cur_col;
    }
  }

  int legend_y = plot_bot + 2;
  for (int slot = 0; slot < n_items; ++slot) {
    const Item *item = data_get(item_start + slot);
    if (!item)
      break;
    int y = legend_y + slot;
    if (y >= win_h - 1)
      break;
    if (has_colors())
      wattron(win, COLOR_PAIR(line_colors[slot % 5]) | A_BOLD);
    if (item->price > 0) {
      char rev[24];
      fmt_revenue(rev, sizeof rev, item->qty, item->price);
      mvwprintw(win, y, 2, "-- %s  %d units  ~%s", item->product, item->qty,
                rev);
    } else {
      mvwprintw(win, y, 2, "-- %s  %d units", item->product, item->qty);
    }
    if (has_colors())
      wattroff(win, COLOR_PAIR(line_colors[slot % 5]) | A_BOLD);
  }
}

// banner
void draw_load_banners(void) {
  loaded_banner = load_banner_by_name("default");
  loaded_fame = load_banner_by_name("fame");
  loaded_lame = load_banner_by_name("lame");
}

void draw_free_banners(void) {
  if (loaded_banner) {
    free_banner(loaded_banner);
    loaded_banner = NULL;
  }
  if (loaded_fame) {
    free_banner(loaded_fame);
    loaded_fame = NULL;
  }
  if (loaded_lame) {
    free_banner(loaded_lame);
    loaded_lame = NULL;
  }
}

int draw_banner_lines(void) { return count_lines(loaded_banner); }
const char **draw_banner_ptr(void) {
  return loaded_banner ? (const char **)loaded_banner : empty_banner;
}
int draw_fame_lines(void) { return count_lines(loaded_fame); }
const char **draw_fame_ptr(void) {
  return loaded_fame ? (const char **)loaded_fame : empty_banner;
}
int draw_lame_lines(void) { return count_lines(loaded_lame); }
const char **draw_lame_ptr(void) {
  return loaded_lame ? (const char **)loaded_lame : empty_banner;
}
