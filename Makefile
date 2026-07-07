CC      := gcc
CXX     := g++
PKG_LIBS := -lncursesw -lsqlite3
DB_LIBS  := -lsqlite3

# Include search paths
INCLUDES := -I./src/include -I./include/

# Build modes
CFLAGS  := -Wall -O2 $(INCLUDES)
DEBUG_CFLAGS := -g -O0
RELEASE_CFLAGS := -O2

# Object dir
OBJDIR  := obj
SRCDIR  := src
SRCS    := $(wildcard $(SRCDIR)/*.c)
OBJS    := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
DEPS    := $(OBJS:.o=.d)

# Binary
BIN     := leaderboard

# Database tool sources
DB_SRCDIR := src/backend
CREATE_SRC := $(DB_SRCDIR)/create.cpp
INSERT_SRC := $(DB_SRCDIR)/insertDelete.cpp
SEED_SRC   := $(DB_SRCDIR)/seed.cpp

CREATE_BIN := $(DB_SRCDIR)/create
INSERT_BIN := $(DB_SRCDIR)/insertDelete
SEED_BIN   := $(DB_SRCDIR)/seed

# ── Default: release build ─────────────────────────────────────────────
.PHONY: all
all: CFLAGS += $(RELEASE_CFLAGS)
all: $(BIN)

# ── Debug build ────────────────────────────────────────────────────────
.PHONY: debug
debug: CFLAGS += $(DEBUG_CFLAGS)
debug: clean $(BIN)

# ── Link ───────────────────────────────────────────────────────────────
$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(PKG_LIBS)

# ── Compile .c -> .o and generate dependency .d ────────────────────────
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# ── Ensure object dir exists ───────────────────────────────────────────
$(OBJDIR):
	mkdir -p $(OBJDIR)

# ── Database tools ─────────────────────────────────────────────────────

$(CREATE_BIN): $(CREATE_SRC)
	$(CXX) -O2 $< -o $@ $(DB_LIBS)

$(INSERT_BIN): $(INSERT_SRC)
	$(CXX) -O2 $< -o $@ $(DB_LIBS)

$(SEED_BIN): $(SEED_SRC)
	$(CXX) -O2 $< -o $@ $(DB_LIBS)

# make db  — create tables and insert all 20 products
.PHONY: db
db: $(CREATE_BIN)
	$(CREATE_BIN)
	@echo "Database ready."

# make seed  — populate SALES_HISTORY with one week of hourly data
.PHONY: seed
seed: $(SEED_BIN)
	$(SEED_BIN)

# make resetdb  — wipe and rebuild the database from scratch, then seed
.PHONY: resetdb
resetdb:
	@echo "Removing old database..."
	rm -f $(DB_SRCDIR)/example.db
	$(MAKE) db
	$(MAKE) seed
	@echo "Database reset and seeded."

# ── Run ────────────────────────────────────────────────────────────────
.PHONY: run
run: $(BIN)
	./$(BIN)

# ── Install ────────────────────────────────────────────────────────────
PREFIX ?= /usr/local
.PHONY: install
install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

# ── Strip binary for smaller release ───────────────────────────────────
.PHONY: strip
strip: $(BIN)
	strip $(BIN)

# ── Clean ──────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf $(OBJDIR) $(BIN)

# Clean including compiled db tools (but not the database itself)
.PHONY: cleanall
cleanall: clean
	rm -f $(CREATE_BIN) $(INSERT_BIN) $(SEED_BIN)

# ── Include auto-generated dependency files ────────────────────────────
-include $(DEPS)

# ── Phony convenience ──────────────────────────────────────────────────
.PHONY: rebuild
rebuild: clean all
