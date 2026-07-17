#define _XOPEN_SOURCE 700
#include "data.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ITEMS 256
#define MAX_BUCKETS 10000
#define MAX_DAYS 365
#define DB_PATH "src/backend/example.db"

static Item items[MAX_ITEMS];
static int items_count = 0;
static int loaded = 0;

static long hist_times[MAX_BUCKETS];
static int hist_amounts[MAX_BUCKETS][MAX_ITEMS];
static char hist_names[MAX_ITEMS][PRODUCT_NAME_MAX];
static int hist_bucket_count = 0;
static int hist_item_count = 0;
static int hist_loaded = 0;

static int hist_daily[MAX_DAYS][MAX_ITEMS];
static long hist_day_ts[MAX_DAYS];
static int hist_day_count = 0;
static int daily_computed = 0;

static sqlite3 *open_db(void) {
  sqlite3 *db = NULL;
  if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
    fprintf(stderr, "data: cannot open %s: %s\n", DB_PATH, sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }
  sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
  return db;
}

static int row_cb(void *unused, int cols, char **vals, char **names) {
  (void)unused;
  (void)cols;
  (void)names;
  if (items_count >= MAX_ITEMS)
    return 0;
  strncpy(items[items_count].product, vals[0] ? vals[0] : "",
          sizeof items[0].product - 1);
  items[items_count].qty = vals[1] ? atoi(vals[1]) : 0;
  items[items_count].price = vals[2] ? atoi(vals[2]) : 0;
  ++items_count;
  return 0;
}

static void load_from_db(void) {
  if (loaded)
    return;
  loaded = 1;
  items_count = 0;
  sqlite3 *db = open_db();
  if (!db)
    return;
  char *err = NULL;
  int rc = sqlite3_exec(db, "SELECT NAME, AMOUNT, PRICE FROM PRODUCT;", row_cb,
                        NULL, &err);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "data: query failed: %s\n", err);
    sqlite3_free(err);
  }
  sqlite3_close(db);
}

static void load_history(void) {
  if (hist_loaded)
    return;
  hist_loaded = 1;

  sqlite3 *db = open_db();
  if (!db)
    return;

  sqlite3_stmt *stmt = NULL;
  int rc;

  rc = sqlite3_prepare_v2(db,
                          "SELECT DISTINCT snapshot_time FROM SALES_HISTORY "
                          "ORDER BY snapshot_time ASC;",
                          -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_close(db);
    return;
  }
  hist_bucket_count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && hist_bucket_count < MAX_BUCKETS)
    hist_times[hist_bucket_count++] = (long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);

  rc = sqlite3_prepare_v2(
      db, "SELECT DISTINCT name FROM SALES_HISTORY ORDER BY rowid ASC;", -1,
      &stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_close(db);
    return;
  }
  hist_item_count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && hist_item_count < MAX_ITEMS) {
    const char *n = (const char *)sqlite3_column_text(stmt, 0);
    strncpy(hist_names[hist_item_count], n ? n : "", PRODUCT_NAME_MAX - 1);
    hist_item_count++;
  }
  sqlite3_finalize(stmt);

  for (int b = 0; b < hist_bucket_count; ++b)
    for (int i = 0; i < hist_item_count; ++i)
      hist_amounts[b][i] = -1;

  rc = sqlite3_prepare_v2(
      db, "SELECT snapshot_time, name, amount FROM SALES_HISTORY;", -1, &stmt,
      NULL);
  if (rc != SQLITE_OK) {
    sqlite3_close(db);
    return;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    long ts = (long)sqlite3_column_int64(stmt, 0);
    const char *n = (const char *)sqlite3_column_text(stmt, 1);
    int amount = sqlite3_column_int(stmt, 2);
    int bi = -1;
    for (int b = 0; b < hist_bucket_count; ++b)
      if (hist_times[b] == ts) {
        bi = b;
        break;
      }
    int ii = -1;
    for (int i = 0; i < hist_item_count; ++i)
      if (n && strncmp(hist_names[i], n, PRODUCT_NAME_MAX) == 0) {
        ii = i;
        break;
      }
    if (bi >= 0 && ii >= 0)
      hist_amounts[bi][ii] = amount;
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

static void compute_daily(void) {
  if (daily_computed)
    return;
  daily_computed = 1;
  load_history();
  if (hist_bucket_count == 0)
    return;

  long day_keys[MAX_DAYS];
  int day_last_bucket[MAX_DAYS];
  hist_day_count = 0;

  for (int b = 0; b < hist_bucket_count; ++b) {
    long day_ts = hist_times[b] - (hist_times[b] % 86400);
    int di = -1;
    for (int d = 0; d < hist_day_count; ++d)
      if (day_keys[d] == day_ts) {
        di = d;
        break;
      }
    if (di == -1) {
      if (hist_day_count >= MAX_DAYS)
        break;
      di = hist_day_count++;
      day_keys[di] = day_ts;
      hist_day_ts[di] = day_ts;
      day_last_bucket[di] = b;
    } else {
      day_last_bucket[di] = b;
    }
  }

  for (int i = 0; i < hist_item_count; ++i) {
    int prev_cum = 0;
    for (int d = 0; d < hist_day_count; ++d) {
      int b = day_last_bucket[d];
      int cum = hist_amounts[b][i];
      if (cum < 0)
        cum = prev_cum;
      hist_daily[d][i] = cum - prev_cum;
      if (hist_daily[d][i] < 0)
        hist_daily[d][i] = 0;
      prev_cum = cum;
    }
  }
}

int data_count(void) {
  load_from_db();
  return items_count;
}

const Item *data_get(int idx) {
  load_from_db();
  if (idx < 0 || idx >= items_count)
    return NULL;
  return &items[idx];
}

void data_sort_by_qty_desc(void) {
  load_from_db();
  for (int i = 0; i < items_count; ++i)
    for (int j = i + 1; j < items_count; ++j)
      if (items[j].qty > items[i].qty) {
        Item tmp = items[i];
        items[i] = items[j];
        items[j] = tmp;
      }
}

void data_reload(void) {
  loaded = 0;
  hist_loaded = 0;
  daily_computed = 0;
  items_count = 0;
  hist_bucket_count = 0;
  hist_item_count = 0;
  hist_day_count = 0;
}

void data_snapshot(void) {
  load_from_db();
  if (items_count == 0)
    return;
  sqlite3 *db = open_db();
  if (!db)
    return;
  time_t now = time(NULL);
  long slot = (long)(now - now % 3600);
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(
      db,
      "INSERT OR IGNORE INTO SALES_HISTORY(snapshot_time, name, amount) "
      "VALUES (?, ?, ?);",
      -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_close(db);
    return;
  }
  sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);
  for (int i = 0; i < items_count; ++i) {
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)slot);
    sqlite3_bind_text(stmt, 2, items[i].product, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, items[i].qty);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  hist_loaded = 0;
  daily_computed = 0;
}

int data_history_count(void) {
  load_history();
  return hist_bucket_count;
}

int data_history_get(int bucket_idx, const char *name) {
  load_history();
  if (bucket_idx < 0 || bucket_idx >= hist_bucket_count || !name)
    return -1;
  for (int i = 0; i < hist_item_count; ++i)
    if (strncmp(hist_names[i], name, PRODUCT_NAME_MAX) == 0)
      return hist_amounts[bucket_idx][i];
  return -1;
}

int data_history_time(int bucket_idx, long *out_time) {
  load_history();
  if (bucket_idx < 0 || bucket_idx >= hist_bucket_count || !out_time)
    return -1;
  *out_time = hist_times[bucket_idx];
  return 0;
}

int data_daily_count(void) {
  compute_daily();
  return hist_day_count;
}

int data_daily_get(int day_idx, const char *name) {
  compute_daily();
  if (day_idx < 0 || day_idx >= hist_day_count || !name)
    return -1;
  for (int i = 0; i < hist_item_count; ++i)
    if (strncmp(hist_names[i], name, PRODUCT_NAME_MAX) == 0)
      return hist_daily[day_idx][i];
  return -1;
}

int data_daily_time(int day_idx, long *out_time) {
  compute_daily();
  if (day_idx < 0 || day_idx >= hist_day_count || !out_time)
    return -1;
  *out_time = hist_day_ts[day_idx];
  return 0;
}

int data_latest_purchase(long *ts_out, PurchaseItem *items_out, int max_items) {
  sqlite3 *db = open_db();
  if (!db)
    return 0;

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(
      db,
      "SELECT purchased_at, name, units FROM PURCHASES "
      "WHERE purchased_at = (SELECT MAX(purchased_at) FROM PURCHASES) "
      "ORDER BY units DESC;",
      -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_close(db);
    return 0;
  }

  int count = 0;
  long ts = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && count < max_items) {
    if (count == 0)
      ts = (long)sqlite3_column_int64(stmt, 0);
    const char *name = (const char *)sqlite3_column_text(stmt, 1);
    strncpy(items_out[count].name, name ? name : "", PRODUCT_NAME_MAX - 1);
    items_out[count].name[PRODUCT_NAME_MAX - 1] = '\0';
    items_out[count].units = sqlite3_column_int(stmt, 2);
    count++;
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  if (ts_out)
    *ts_out = ts;
  return count;
}

int data_trend(const char *name) {
  compute_daily();
  if (!name || hist_day_count < 2)
    return 0;

  int days = hist_day_count;
  int this_start = (days >= 7) ? days - 7 : 0;
  int last_start = (days >= 14) ? days - 14 : 0;
  int last_end = this_start;

  int this_week = 0, last_week = 0;
  for (int d = this_start; d < days; d++) {
    int v = data_daily_get(d, name);
    if (v > 0)
      this_week += v;
  }
  for (int d = last_start; d < last_end; d++) {
    int v = data_daily_get(d, name);
    if (v > 0)
      last_week += v;
  }

  if (this_week > last_week)
    return 1;
  if (this_week < last_week)
    return -1;
  return 0;
}

int data_purchase_count(void) {
  sqlite3 *db = open_db();
  if (!db)
    return 0;

  sqlite3_stmt *stmt = NULL;
  int count = 0;

  int rc = sqlite3_prepare_v2(
      db, "SELECT COUNT(DISTINCT purchased_at) FROM PURCHASES;", -1, &stmt,
      NULL);

  if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
    count = sqlite3_column_int(stmt, 0);

  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return count;
}
