#include "utf8.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h> //#include <wctype.h>

static ptrdiff_t decode_next(const char *p, wchar_t *wc, mbstate_t *st) {
  size_t len = mbrtowc(wc, p, MB_CUR_MAX, st);
  if (len == 0) {
    return 0;
  }
  if (len == (size_t)-1 || len == (size_t)-2) {
    memset(st, 0, sizeof *st);
    return -1;
  }
  return (ptrdiff_t)len;
}

static int char_display_width(wchar_t wc) {
  int w = wcwidth(wc);
  return (w >= 0) ? w : 0;
}

int utf8_display_width(const char *s) {
  if (!s) {
    return 0;
  }
  mbstate_t st;
  memset(&st, 0, sizeof st);
  const char *p = s;
  int cols = 0;
  while (*p) {
    wchar_t wc;
    ptrdiff_t len = decode_next(p, &wc, &st);
    if (len == 0) {
      break;
    }
    if (len == -1) {
      cols += 1;
      p += 1;
      continue;
    }
    cols += char_display_width(wc);
    p += len;
  }
  return cols;
}

int utf8_truncate_to_width(const char *src, char *dst, size_t dstlen,
                           int maxcols) {
  if (dst && dstlen > 0) {
    dst[0] = '\0';
  }
  if (!src || !dst || dstlen == 0 || maxcols <= 0) {
    return 0;
  }

  mbstate_t st;
  memset(&st, 0, sizeof st);
  const char *p = src;
  char *q = dst;
  size_t remaining = dstlen - 1;
  int cols = 0;

  while (*p) {
    wchar_t wc;
    ptrdiff_t len = decode_next(p, &wc, &st);
    if (len == 0) {
      break;
    }
    if (len == -1) {
      if (remaining < 1 || cols + 1 > maxcols) {
        break;
      }
      *q++ = *p++;
      remaining--;
      cols++;
      continue;
    }
    int add = char_display_width(wc);
    if (cols + add > maxcols) {
      break;
    }
    if ((size_t)len > remaining) {
      break;
    }
    memcpy(q, p, (size_t)len);
    q += len;
    p += len;
    remaining -= (size_t)len;
    cols += add;
  }
  *q = '\0';
  return (int)(q - dst);
}
