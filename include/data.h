#pragma once
#include <stddef.h>

enum {
PRODUCT_NAME_MAX = 64
};

typedef struct {
  char product[PRODUCT_NAME_MAX];
  int qty;
  int price; /* unit price in øre (1/100 NOK) */
} Item;

typedef struct {
  char name[PRODUCT_NAME_MAX];
  int units;
} PurchaseItem;

int data_count(void);
const Item *data_get(int idx);
void data_sort_by_qty_desc(void);

void data_snapshot(void);
int data_history_count(void);
int data_history_get(int bucket_idx, const char *name);
int data_history_time(int bucket_idx, long *out_time);

int data_daily_count(void);
int data_daily_get(int day_idx, const char *name);
int data_daily_time(int day_idx, long *out_time);

int data_trend(const char *name);
int data_latest_purchase(long *ts_out, PurchaseItem *items_out, int max_items);
int data_purchase_count(void);

/* Invalidates all in-memory caches, forcing a fresh DB read on next access. */
void data_reload(void);
