/* SPDX-License-Identifier: TBD */
/*
 * lock.c - Heliumfile.lock parser and writer.
 *
 * Lock file format:
 *   [package]
 *   name = "..."
 *   version = "..."
 *   edition = "..."
 *
 *   [[dependencies]]
 *   name = "..."
 *   version = "..."
 *   checksum = "..."
 */

#define _GNU_SOURCE

#include "lock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

struct hel_lock *hel_lock_new(void)
{
	return hel_xalloc(sizeof(struct hel_lock));
}

void hel_lock_free(struct hel_lock *l)
{
	size_t i;

	if (!l)
		return;
	free(l->name);
	free(l->version);
	free(l->edition);
	for (i = 0; i < l->dep_count; i++) {
		free(l->deps[i].name);
		free(l->deps[i].version);
		free(l->deps[i].checksum);
	}
	free(l->deps);
	free(l);
}

struct hel_lock_entry *hel_lock_find_dep(const struct hel_lock *l,
					 const char *name)
{
	size_t i;

	for (i = 0; i < l->dep_count; i++) {
		if (strcmp(l->deps[i].name, name) == 0)
			return &l->deps[i];
	}
	return NULL;
}

int hel_lock_add_dep(struct hel_lock *l, const char *name,
		     const char *version, const char *checksum)
{
	struct hel_lock_entry *e;

	if (hel_lock_find_dep(l, name)) {
		e = hel_lock_find_dep(l, name);
		free(e->version);
		free(e->checksum);
		e->version = hel_xstrdup(version);
		e->checksum = hel_xstrdup(checksum);
		return 0;
	}
	if (l->dep_count == l->dep_cap) {
		size_t newcap = l->dep_cap ? l->dep_cap * 2 : 4;

		l->deps = hel_xrealloc(l->deps,
				       newcap * sizeof(struct hel_lock_entry));
		l->dep_cap = newcap;
	}
	e = &l->deps[l->dep_count++];
	e->name = hel_xstrdup(name);
	e->version = hel_xstrdup(version);
	e->checksum = hel_xstrdup(checksum);
	return 0;
}

int hel_lock_remove_dep(struct hel_lock *l, const char *name)
{
	size_t i;

	for (i = 0; i < l->dep_count; i++) {
		if (strcmp(l->deps[i].name, name) != 0)
			continue;
		free(l->deps[i].name);
		free(l->deps[i].version);
		free(l->deps[i].checksum);
		memmove(&l->deps[i], &l->deps[i + 1],
			(l->dep_count - i - 1) *
			sizeof(struct hel_lock_entry));
		l->dep_count--;
		return 0;
	}
	return -1;
}

static int unquote_value(const char *src, char *out, size_t out_size)
{
	if (hel_unquote(src, out, out_size) < 0) {
		if (strlen(src) >= out_size)
			return -1;
		strcpy(out, src);
	}
	return 0;
}

struct hel_lock *hel_lock_read(const char *path, char **error)
{
	struct hel_lock *l = hel_lock_new();
	char *text = hel_read_file(path);
	char *line;
	char *saveptr = NULL;
	char *section = NULL;
	struct hel_lock_entry *current = NULL;

	if (!text) {
		l->name = hel_xstrdup("");
		l->version = hel_xstrdup("0.1.0");
		l->edition = hel_xstrdup("2025");
		return l;
	}
	line = strtok_r(text, "\n", &saveptr);
	while (line) {
		char *p = hel_trim(line);
		char *eq;
		char key[128];
		char buf[1024];
		char *raw;

		if (*p == '\0' || *p == '#') {
			line = strtok_r(NULL, "\n", &saveptr);
			continue;
		}
		if (p[0] == '[') {
			char *end;
			char *start = p + 1;

			if (p[1] == '[')
				start = p + 2;
			end = strchr(start, ']');
			if (!end) {
				if (error)
					*error = hel_xstrdup("invalid lock file syntax");
				hel_lock_free(l);
				free(section);
				free(text);
				return NULL;
			}
			*end = '\0';
			free(section);
			section = hel_xstrdup(start);
			if (strcmp(section, "dependencies") == 0)
				current = NULL;
			line = strtok_r(NULL, "\n", &saveptr);
			continue;
		}
		eq = strchr(p, '=');
		if (!eq) {
			if (error)
				*error = hel_xstrdup("invalid lock file syntax");
			hel_lock_free(l);
			free(section);
			free(text);
			return NULL;
		}
		*eq = '\0';
		if (strlen(hel_trim(p)) >= sizeof(key)) {
			if (error)
				*error = hel_xstrdup("invalid lock file syntax");
			hel_lock_free(l);
			free(section);
			free(text);
			return NULL;
		}
		strncpy(key, hel_trim(p), sizeof(key) - 1);
		key[sizeof(key) - 1] = '\0';
		raw = hel_trim(eq + 1);
		if (unquote_value(raw, buf, sizeof(buf)) != 0) {
			if (error)
				*error = hel_xstrdup("invalid lock file syntax");
			hel_lock_free(l);
			free(section);
			free(text);
			return NULL;
		}
		if (strcmp(section, "package") == 0) {
			if (strcmp(key, "name") == 0) {
				free(l->name);
				l->name = hel_xstrdup(buf);
			} else if (strcmp(key, "version") == 0) {
				free(l->version);
				l->version = hel_xstrdup(buf);
			} else if (strcmp(key, "edition") == 0) {
				free(l->edition);
				l->edition = hel_xstrdup(buf);
			}
		} else if (strcmp(section, "dependencies") == 0) {
			if (!current || strcmp(key, "name") == 0) {
				hel_lock_add_dep(l, "", "", "");
				current = &l->deps[l->dep_count - 1];
			}
			if (strcmp(key, "name") == 0) {
				free(current->name);
				current->name = hel_xstrdup(buf);
			} else if (strcmp(key, "version") == 0) {
				free(current->version);
				current->version = hel_xstrdup(buf);
			} else if (strcmp(key, "checksum") == 0) {
				free(current->checksum);
				current->checksum = hel_xstrdup(buf);
			}
		}
		line = strtok_r(NULL, "\n", &saveptr);
	}
	free(section);
	free(text);
	if (!l->name)
		l->name = hel_xstrdup("");
	if (!l->version)
		l->version = hel_xstrdup("0.1.0");
	if (!l->edition)
		l->edition = hel_xstrdup("2025");
	return l;
}

int hel_lock_write(const struct hel_lock *l, const char *path,
		   char **error)
{
	FILE *f;
	size_t i;

	f = fopen(path, "w");
	if (!f) {
		if (error)
			*error = hel_xstrdup("cannot write Heliumfile.lock");
		return -1;
	}
	fprintf(f, "[package]\n");
	fprintf(f, "name = \"%s\"\n", l->name);
	fprintf(f, "version = \"%s\"\n", l->version);
	fprintf(f, "edition = \"%s\"\n", l->edition);
	fprintf(f, "\n");
	for (i = 0; i < l->dep_count; i++) {
		const struct hel_lock_entry *e = &l->deps[i];

		fprintf(f, "[[dependencies]]\n");
		fprintf(f, "name = \"%s\"\n", e->name);
		fprintf(f, "version = \"%s\"\n", e->version);
		fprintf(f, "checksum = \"%s\"\n", e->checksum);
		fprintf(f, "\n");
	}
	fclose(f);
	return 0;
}
