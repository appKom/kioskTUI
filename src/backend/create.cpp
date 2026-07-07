#include <iostream>
#include <sqlite3.h>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  sqlite3 *DB;
  int exit = 0;

  exit = sqlite3_open("src/backend/example.db", &DB);
  if (exit != SQLITE_OK) {
    std::cerr << "Error opening database: " << sqlite3_errmsg(DB) << std::endl;
    sqlite3_close(DB);
    return 1;
  }

  char *errMsg = NULL;

  const char *sql_product = "CREATE TABLE IF NOT EXISTS PRODUCT("
                            "  ID     INT  PRIMARY KEY NOT NULL,"
                            "  NAME   TEXT             NOT NULL,"
                            "  AMOUNT INT              NOT NULL"
                            ");";

  exit = sqlite3_exec(DB, sql_product, NULL, 0, &errMsg);
  if (exit != SQLITE_OK) {
    std::cerr << "Error creating PRODUCT table: " << errMsg << std::endl;
    sqlite3_free(errMsg);
    sqlite3_close(DB);
    return 1;
  }
  std::cout << "PRODUCT table ready." << std::endl;

  const char *sql_history = "CREATE TABLE IF NOT EXISTS SALES_HISTORY("
                            "  snapshot_time INTEGER NOT NULL,"
                            "  name          TEXT    NOT NULL,"
                            "  amount        INTEGER NOT NULL,"
                            "  PRIMARY KEY (snapshot_time, name)"
                            ");";

  exit = sqlite3_exec(DB, sql_history, NULL, 0, &errMsg);
  if (exit != SQLITE_OK) {
    std::cerr << "Error creating SALES_HISTORY table: " << errMsg << std::endl;
    sqlite3_free(errMsg);
    sqlite3_close(DB);
    return 1;
  }
  std::cout << "SALES_HISTORY table ready." << std::endl;

  const char *sql_purchases =
      "CREATE TABLE IF NOT EXISTS PURCHASES("
      "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  purchased_at INTEGER NOT NULL,"
      "  name         TEXT    NOT NULL,"
      "  units        INTEGER NOT NULL"
      ");";
  const char *sql_idx =
      "CREATE UNIQUE INDEX IF NOT EXISTS idx_product_name ON PRODUCT(NAME);";

  exit = sqlite3_exec(DB, sql_idx, NULL, 0, &errMsg);
  if (exit != SQLITE_OK) {
    std::cerr << "Error creating product name index: " << errMsg << std::endl;
    sqlite3_free(errMsg);
    sqlite3_close(DB);
    return 1;
  }
  std::cout << "Product name index ready." << std::endl;
  exit = sqlite3_exec(DB, sql_purchases, NULL, 0, &errMsg);
  if (exit != SQLITE_OK) {
    std::cerr << "Error creating PURCHASES table: " << errMsg << std::endl;
    sqlite3_free(errMsg);
    sqlite3_close(DB);
    return 1;
  }
  std::cout << "PURCHASES table ready." << std::endl;

  sqlite3_close(DB);
  return 0;
}
