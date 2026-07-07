#ifndef UTF8_H
#define UTF8_H
#include <stddef.h>

int utf8_display_width(const char *s);
int utf8_truncate_to_width(const char *src, char *dst, size_t dstlen,
                           int maxcols);
#endif
