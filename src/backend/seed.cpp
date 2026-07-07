#include <cstdlib>
#include <ctime>
#include <iostream>
#include <sqlite3.h>
#include <string>
#include <vector>

#define DB_PATH "src/backend/example.db"
#define DAYS 7
#define HOURS 24

/*
 * Each product gets a random base daily rate in [min_rate, max_rate].
 * Hourly sales are randomized around (base_rate / 24) with small variance.
 * Totals are accumulated and written back to PRODUCT.AMOUNT.
 */
#define MIN_DAILY_RATE 2
#define MAX_DAILY_RATE 130

/*
 * Purchase simulation:
 * Between MIN_PURCHASES_PER_DAY and MAX_PURCHASES_PER_DAY customer
 * transactions are generated per day, weighted toward business hours
 * (08:00–20:00).  Each transaction picks 1–MAX_ITEMS_PER_PURCHASE
 * distinct products at 1–3 units each and shares a single
 * second-precision timestamp so the TUI can group them correctly.
 */
#define MIN_PURCHASES_PER_DAY 15
#define MAX_PURCHASES_PER_DAY 60
#define MAX_ITEMS_PER_PURCHASE 4

int main(void) {
  sqlite3 *db = NULL;
  if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
    std::cerr << "Cannot open " << DB_PATH << ": " << sqlite3_errmsg(db)
              << "\n";
    sqlite3_close(db);
    return 1;
  }

  /* ── fetch products ── */
  struct Product {
    std::string name;
    int total;
  };
  std::vector<Product> products;

  sqlite3_stmt *sel = NULL;
  int rc = sqlite3_prepare_v2(db, "SELECT NAME FROM PRODUCT ORDER BY ID;", -1,
                              &sel, NULL);
  if (rc != SQLITE_OK) {
    std::cerr << "Query failed: " << sqlite3_errmsg(db) << "\n";
    sqlite3_close(db);
    return 1;
  }
  while (sqlite3_step(sel) == SQLITE_ROW) {
    Product p;
    p.name = reinterpret_cast<const char *>(sqlite3_column_text(sel, 0));
    p.total = 0;
    products.push_back(p);
  }
  sqlite3_finalize(sel);

  if (products.empty()) {
    std::cerr << "No products found. Run make db first.\n";
    sqlite3_close(db);
    return 1;
  }

  /* ── prepare SALES_HISTORY insert ── */
  sqlite3_stmt *ins = NULL;
  rc = sqlite3_prepare_v2(
      db,
      "INSERT OR IGNORE INTO SALES_HISTORY(snapshot_time, name, amount) "
      "VALUES (?, ?, ?);",
      -1, &ins, NULL);
  if (rc != SQLITE_OK) {
    std::cerr << "Prepare history insert failed: " << sqlite3_errmsg(db)
              << "\n";
    sqlite3_close(db);
    return 1;
  }

  time_t now = time(NULL);
  long now_slot = (long)(now - now % 3600);
  long start = now_slot - (long)(DAYS * HOURS - 1) * 3600;

  sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

  unsigned int global_seed = (unsigned int)now;

  for (size_t p = 0; p < products.size(); ++p) {
    unsigned int seed =
        (unsigned int)(p * 6364136223846793005ULL + 1442695040888963407ULL) ^
        global_seed;

    int range = MAX_DAILY_RATE - MIN_DAILY_RATE + 1;
    int daily_rate = MIN_DAILY_RATE + (int)(rand_r(&seed) % range);
    int cumulative = 0;

    for (int d = 0; d < DAYS; ++d) {
      for (int h = 0; h < HOURS; ++h) {
        long slot = start + (long)(d * HOURS + h) * 3600;

        int hourly_base = daily_rate / HOURS;
        if (hourly_base < 1)
          hourly_base = 1;
        int variance = hourly_base / 2;
        if (variance < 1)
          variance = 1;
        int hourly_sold =
            hourly_base + (int)(rand_r(&seed) % (variance * 2 + 1)) - variance;
        if (hourly_sold < 0)
          hourly_sold = 0;

        cumulative += hourly_sold;

        sqlite3_bind_int64(ins, 1, (sqlite3_int64)slot);
        sqlite3_bind_text(ins, 2, products[p].name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(ins, 3, cumulative);
        sqlite3_step(ins);
        sqlite3_reset(ins);
      }
    }

    products[p].total = cumulative;
  }

  sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
  sqlite3_finalize(ins);

  /* ── update PRODUCT.AMOUNT ── */
  sqlite3_stmt *upd = NULL;
  rc = sqlite3_prepare_v2(db, "UPDATE PRODUCT SET AMOUNT = ? WHERE NAME = ?;",
                          -1, &upd, NULL);
  if (rc != SQLITE_OK) {
    std::cerr << "Prepare update failed: " << sqlite3_errmsg(db) << "\n";
    sqlite3_close(db);
    return 1;
  }

  sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);
  for (size_t p = 0; p < products.size(); ++p) {
    sqlite3_bind_int(upd, 1, products[p].total);
    sqlite3_bind_text(upd, 2, products[p].name.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(upd);
    sqlite3_reset(upd);
    std::cout << products[p].name << ": " << products[p].total
              << " total units sold\n";
  }
  sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
  sqlite3_finalize(upd);

  /* ── seed PURCHASES ──────────────────────────────────────────────────
   *
   * For each day generate a random number of customer transactions.
   * Each transaction:
   *   - gets a second-precision timestamp biased toward business hours
   *   - picks 1..MAX_ITEMS_PER_PURCHASE distinct products
   *   - buys 1..3 units of each
   *
   * All rows in one transaction share the exact same purchased_at so
   * the TUI groups them into one purchase event.
   * ──────────────────────────────────────────────────────────────────── */

  /* Clear existing purchase seed data before re-seeding. */
  sqlite3_exec(db, "DELETE FROM PURCHASES;", NULL, NULL, NULL);

  sqlite3_stmt *pins = NULL;
  rc = sqlite3_prepare_v2(
      db, "INSERT INTO PURCHASES(purchased_at, name, units) VALUES (?, ?, ?);",
      -1, &pins, NULL);
  if (rc != SQLITE_OK) {
    std::cerr << "Prepare purchases insert failed: " << sqlite3_errmsg(db)
              << "\n";
    sqlite3_close(db);
    return 1;
  }

  unsigned int pseed = global_seed ^ 0xDEADBEEF;
  int n_products = (int)products.size();
  int total_purchases = 0;

  sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

  /* Day 0 is 7 days ago; day DAYS-1 ends at now_slot. */
  long day_start = start - (start % 86400); /* UTC midnight of first day */

  for (int d = 0; d < DAYS; ++d) {
    int n_purchases = MIN_PURCHASES_PER_DAY +
                      (int)(rand_r(&pseed) % (MAX_PURCHASES_PER_DAY -
                                              MIN_PURCHASES_PER_DAY + 1));

    for (int t = 0; t < n_purchases; ++t) {
      /*
       * Timestamp: pick a second within the day, biased toward
       * 08:00–20:00 (business hours = 43200 seconds wide, offset 28800).
       */
      long ts;
      if (rand_r(&pseed) % 4 != 0) {
        /* 75 % of purchases in business hours */
        ts = day_start + (long)d * 86400 + 28800L /* 08:00 offset */
             + (long)(rand_r(&pseed) % 43200);    /* span 12 h     */
      } else {
        ts = day_start + (long)d * 86400 + (long)(rand_r(&pseed) % 86400);
      }

      /* Don't place purchases in the future. */
      if (ts > (long)now)
        continue;

      /* Number of distinct items in this purchase: 1..MAX_ITEMS_PER_PURCHASE */
      int n_items = 1 + (int)(rand_r(&pseed) % MAX_ITEMS_PER_PURCHASE);
      if (n_items > n_products)
        n_items = n_products;

      /* Pick n_items distinct products (Fisher-Yates partial shuffle). */
      std::vector<int> indices(n_products);
      for (int i = 0; i < n_products; ++i)
        indices[i] = i;
      for (int i = 0; i < n_items; ++i) {
        int j = i + (int)(rand_r(&pseed) % (n_products - i));
        std::swap(indices[i], indices[j]);
      }

      for (int i = 0; i < n_items; ++i) {
        int units = 1 + (int)(rand_r(&pseed) % 3); /* 1..3 */
        const std::string &name = products[indices[i]].name;

        sqlite3_bind_int64(pins, 1, (sqlite3_int64)ts);
        sqlite3_bind_text(pins, 2, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(pins, 3, units);
        sqlite3_step(pins);
        sqlite3_reset(pins);
      }

      ++total_purchases;
    }
  }

  sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
  sqlite3_finalize(pins);
  sqlite3_close(db);

  std::cout << "\nDone. PRODUCT.AMOUNT updated from sales history.\n";
  std::cout << total_purchases
            << " purchase transactions seeded into PURCHASES.\n";
  return 0;
}
