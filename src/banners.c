#define _XOPEN_SOURCE 700
#include "banners.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_LINE_CAPACITY 16
#define PATH_BUFFER_SIZE 512
#define LOCALE_BUFFER_SIZE 64
#define LINE_BUFFER_SIZE 1024

static char *safe_strdup(const char *src) {
  if (!src)
    return NULL;
  size_t len = strlen(src) + 1;
  char *copy = malloc(len);
  if (copy)
    memcpy(copy, src, len);
  return copy;
}

static void strip_trailing_newline(char *s) {
  size_t len = strlen(s);
  if (len && s[len - 1] == '\n')
    s[len - 1] = '\0';
}

static char **grow_lines(char **lines, size_t *capacity) {
  size_t new_cap = *capacity * 2;
  char **resized = realloc(lines, (new_cap + 1) * sizeof(char *));
  if (!resized)
    return NULL;
  *capacity = new_cap;
  return resized;
}

char **load_banner_file(const char *path) {
  FILE *file = fopen(path, "r");
  if (!file)
    return NULL;

  size_t capacity = INITIAL_LINE_CAPACITY;
  size_t count = 0;
  char **lines = malloc((capacity + 1) * sizeof(char *));
  if (!lines) {
    fclose(file);
    return NULL;
  }

  char buf[LINE_BUFFER_SIZE];
  while (fgets(buf, sizeof buf, file)) {
    strip_trailing_newline(buf);
    if (count >= capacity) {
      char **grown = grow_lines(lines, &capacity);
      if (!grown)
        break;
      lines = grown;
    }
    lines[count++] = safe_strdup(buf);
  }
  fclose(file);
  lines[count] = NULL;
  return lines;
}

void free_banner(char **lines) {
  if (!lines)
    return;
  for (char **p = lines; *p; ++p)
    free(*p);
  free(lines);
}

char **load_banner_by_name(const char *name) {
  char path[PATH_BUFFER_SIZE];
  const char *locale = setlocale(LC_ALL, NULL);

  if (locale && strchr(locale, '.')) {
    char locale_base[LOCALE_BUFFER_SIZE];
    strncpy(locale_base, locale, sizeof locale_base - 1);
    locale_base[sizeof locale_base - 1] = '\0';
    char *dot = strchr(locale_base, '.');
    if (dot)
      *dot = '\0';
    snprintf(path, sizeof path, "res/banners/%s/%s.txt", locale_base, name);
    char **lines = load_banner_file(path);
    if (lines)
      return lines;
  }

  snprintf(path, sizeof path, "res/banners/%s.txt", name);
  return load_banner_file(path);
}
