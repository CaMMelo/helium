/* SPDX-License-Identifier: TBD */
/*
 * ffi.c - Foreign function interface helpers for Helium.
 */

#include "ffi.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *xstrdup(const char *s)
{
	if (!s)
		return NULL;
	return strdup(s);
}

static char *trim(char *s)
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

char *helium_ffi_read_link_flags(const char *path)
{
	FILE *f;
	char line[1024];
	char *flags = NULL;
	size_t len = 0;
	size_t cap = 0;

	f = fopen(path, "r");
	if (!f)
		return NULL;

	while (fgets(line, sizeof(line), f)) {
		char *p = trim(line);
		char *value;

		if (*p == '#' || *p == '\0')
			continue;
		if (strncmp(p, "link", 4) != 0)
			continue;
		p += 4;
		while (isspace((unsigned char)*p))
			p++;
		if (*p != '=')
			continue;
		p++;
		while (isspace((unsigned char)*p))
			p++;

		value = xstrdup(p);
		{
			size_t n = strlen(value);

			if (len + n + 2 > cap) {
				size_t newcap = cap ? cap * 2 : 128;

				while (len + n + 2 > newcap)
					newcap *= 2;
				flags = realloc(flags, newcap);
				if (!flags)
					abort();
				cap = newcap;
			}
			if (len > 0)
				flags[len++] = ' ';
			memcpy(flags + len, value, n + 1);
			len += n;
		}
		free(value);
	}

	fclose(f);
	return flags;
}
