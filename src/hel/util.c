/* SPDX-License-Identifier: TBD */
/*
 * util.c - Small helpers for the hel package manager.
 */

#define _GNU_SOURCE

#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void *hel_xalloc(size_t size)
{
	void *p = calloc(1, size);

	if (!p) {
		fprintf(stderr, "error: out of memory\n");
		abort();
	}
	return p;
}

void *hel_xrealloc(void *p, size_t size)
{
	void *q = realloc(p, size);

	if (!q) {
		fprintf(stderr, "error: out of memory\n");
		abort();
	}
	return q;
}

char *hel_xstrdup(const char *s)
{
	char *p;

	if (!s)
		return NULL;
	p = strdup(s);
	if (!p) {
		fprintf(stderr, "error: out of memory\n");
		abort();
	}
	return p;
}

char *hel_xstrndup(const char *s, size_t n)
{
	char *p = hel_xalloc(n + 1);

	memcpy(p, s, n);
	p[n] = '\0';
	return p;
}

int hel_xasprintf(char **out, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vasprintf(out, fmt, ap);
	va_end(ap);
	if (n < 0) {
		fprintf(stderr, "error: out of memory\n");
		abort();
	}
	return n;
}

char *hel_dirname(const char *path)
{
	const char *slash = strrchr(path, '/');

	if (!slash)
		return hel_xstrdup(".");
	if (slash == path)
		return hel_xstrdup("/");
	return hel_xstrndup(path, slash - path);
}

char *hel_path_join(const char *a, const char *b)
{
	char *out;
	size_t len_a = strlen(a);
	int need_sep = len_a > 0 && a[len_a - 1] != '/';

	if (hel_xasprintf(&out, "%s%s%s", a, need_sep ? "/" : "", b) < 0)
		abort();
	return out;
}

int hel_path_exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

int hel_is_dir(const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return 0;
	return S_ISDIR(st.st_mode);
}

int hel_mkdir_p(const char *path)
{
	char *copy = hel_xstrdup(path);
	char *p = copy;
	int rc = 0;

	if (p[0] == '/')
		p++;
	for (;;) {
		p = strchr(p, '/');
		if (p)
			*p = '\0';
		if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
			rc = -1;
			break;
		}
		if (!p)
			break;
		*p++ = '/';
	}
	free(copy);
	return rc;
}

char *hel_get_executable_dir(void)
{
	char buf[4096];
	ssize_t n;

	n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n < 0)
		return hel_xstrdup(".");
	buf[n] = '\0';
	return hel_dirname(buf);
}

char *hel_read_file(const char *path)
{
	FILE *f;
	long size;
	char *buf;
	size_t n;

	f = fopen(path, "rb");
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	size = ftell(f);
	if (size < 0) {
		fclose(f);
		return NULL;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	buf = hel_xalloc((size_t)size + 1);
	n = fread(buf, 1, (size_t)size, f);
	fclose(f);
	if (n != (size_t)size) {
		free(buf);
		return NULL;
	}
	buf[n] = '\0';
	return buf;
}

int hel_write_file(const char *path, const char *contents)
{
	FILE *f;
	size_t len = strlen(contents);
	size_t n;

	f = fopen(path, "wb");
	if (!f)
		return -1;
	n = fwrite(contents, 1, len, f);
	fclose(f);
	if (n != len)
		return -1;
	return 0;
}

int hel_copy_file(const char *src, const char *dst)
{
	char *contents = hel_read_file(src);

	if (!contents)
		return -1;
	if (hel_write_file(dst, contents) != 0) {
		free(contents);
		return -1;
	}
	free(contents);
	return 0;
}

int hel_copy_file_binary(const char *src, const char *dst)
{
	FILE *fs;
	FILE *fd;
	char buf[4096];
	size_t n;
	int rc = -1;

	fs = fopen(src, "rb");
	if (!fs)
		return -1;
	fd = fopen(dst, "wb");
	if (!fd) {
		fclose(fs);
		return -1;
	}
	while ((n = fread(buf, 1, sizeof(buf), fs)) > 0) {
		if (fwrite(buf, 1, n, fd) != n)
			goto out;
	}
	if (ferror(fs))
		goto out;
	rc = 0;
out:
	fclose(fs);
	fclose(fd);
	return rc;
}

char *hel_trim(char *s)
{
	char *end;

	while (isspace((unsigned char)*s))
		s++;
	if (*s == '\0')
		return s;
	end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end))
		*end-- = '\0';
	return s;
}

const char *hel_skip_ws(const char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	return s;
}

int hel_unquote(const char *src, char *out, size_t out_size)
{
	size_t len = strlen(src);

	if (len < 2 || src[0] != '"' || src[len - 1] != '"')
		return -1;
	if (len - 2 >= out_size)
		return -1;
	memcpy(out, src + 1, len - 2);
	out[len - 2] = '\0';
	return 0;
}
