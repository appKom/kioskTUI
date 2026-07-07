#ifndef BANNERS_H
#define BANNERS_H

char **load_banner_file(const char *path);
char **load_banner_by_name(const char *name);
void free_banner(char **lines);

#endif
