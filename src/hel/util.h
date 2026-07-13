/* SPDX-License-Identifier: TBD */
/*
 * util.h - Small helpers for the hel package manager.
 */

#ifndef HEL_UTIL_H
#define HEL_UTIL_H

#include <stddef.h>

/*
 * Allocation helpers that abort on failure.
 */
void *hel_xalloc(size_t size);
void *hel_xrealloc(void *p, size_t size);
char *hel_xstrdup(const char *s);
char *hel_xstrndup(const char *s, size_t n);
int hel_xasprintf(char **out, const char *fmt, ...);

/*
 * Path helpers.
 */
char *hel_dirname(const char *path);
char *hel_path_join(const char *a, const char *b);
int hel_path_exists(const char *path);
int hel_is_dir(const char *path);
int hel_mkdir_p(const char *path);
char *hel_get_executable_dir(void);

/*
 * File helpers.
 */
char *hel_read_file(const char *path);
int hel_write_file(const char *path, const char *contents);
int hel_copy_file(const char *src, const char *dst);

/*
 * String helpers.
 */
char *hel_trim(char *s);
const char *hel_skip_ws(const char *s);
int hel_unquote(const char *src, char *out, size_t out_size);

#endif /* HEL_UTIL_H */
