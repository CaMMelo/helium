/* SPDX-License-Identifier: TBD */
/*
 * manifest.c - Heliumfile parser and writer.
 *
 * Supports the subset of TOML used by Helium manifests:
 *   [package]
 *   name = "..."
 *   version = "..."
 *   edition = "..."
 *
 *   [dependencies]
 *   foo = "1.0"
 *   bar = { version = "1.0", registry = "..." }
 */

#define _GNU_SOURCE

#include "manifest.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

struct hel_manifest *hel_manifest_new(void)
{
	return hel_xalloc(sizeof(struct hel_manifest));
}

void hel_manifest_free(struct hel_manifest *m)
{
	size_t i;

	if (!m)
		return;
	free(m->name);
	free(m->version);
	free(m->edition);
	for (i = 0; i < m->dep_count; i++) {
		free(m->deps[i].name);
		free(m->deps[i].version);
		free(m->deps[i].registry);
	}
	free(m->deps);
	free(m);
}

struct hel_dep *hel_manifest_find_dep(const struct hel_manifest *m,
				      const char *name)
{
	size_t i;

	for (i = 0; i < m->dep_count; i++) {
		if (strcmp(m->deps[i].name, name) == 0)
			return &m->deps[i];
	}
	return NULL;
}

int hel_manifest_add_dep(struct hel_manifest *m, const char *name,
			 const char *version, const char *registry)
{
	struct hel_dep *d;

	if (hel_manifest_find_dep(m, name)) {
		d = hel_manifest_find_dep(m, name);
		free(d->version);
		free(d->registry);
		d->version = hel_xstrdup(version);
		d->registry = registry ? hel_xstrdup(registry) : NULL;
		return 0;
	}
	if (m->dep_count == m->dep_cap) {
		size_t newcap = m->dep_cap ? m->dep_cap * 2 : 4;

		m->deps = hel_xrealloc(m->deps,
				       newcap * sizeof(struct hel_dep));
		m->dep_cap = newcap;
	}
	d = &m->deps[m->dep_count++];
	d->name = hel_xstrdup(name);
	d->version = hel_xstrdup(version);
	d->registry = registry ? hel_xstrdup(registry) : NULL;
	return 0;
}

int hel_manifest_remove_dep(struct hel_manifest *m, const char *name)
{
	size_t i;

	for (i = 0; i < m->dep_count; i++) {
		if (strcmp(m->deps[i].name, name) != 0)
			continue;
		free(m->deps[i].name);
		free(m->deps[i].version);
		free(m->deps[i].registry);
		memmove(&m->deps[i], &m->deps[i + 1],
			(m->dep_count - i - 1) * sizeof(struct hel_dep));
		m->dep_count--;
		return 0;
	}
	return -1;
}

static char *copy_value(const char *s)
{
	char buf[1024];

	if (hel_unquote(s, buf, sizeof(buf)) < 0)
		return hel_xstrdup(s);
	return hel_xstrdup(buf);
}

static int parse_inline_table(const char *value, char **version,
			      char **registry)
{
	const char *p = hel_skip_ws(value);

	*version = NULL;
	*registry = NULL;
	if (*p != '{')
		return -1;
	p++;
	while (*p) {
		const char *key_start;
		const char *key_end;
		char key[128];
		const char *val_start;
		const char *val_end;
		char val[1024];

		p = hel_skip_ws(p);
		if (*p == '}')
			break;
		key_start = p;
		while (*p && (isalnum((unsigned char)*p) || *p == '_' ||
			      *p == '-'))
			p++;
		key_end = p;
		if (key_end == key_start)
			return -1;
		if ((size_t)(key_end - key_start) >= sizeof(key))
			return -1;
		memcpy(key, key_start, key_end - key_start);
		key[key_end - key_start] = '\0';
		p = hel_skip_ws(p);
		if (*p != '=')
			return -1;
		p++;
		p = hel_skip_ws(p);
		val_start = p;
		if (*p == '"') {
			p++;
			while (*p && *p != '"')
				p++;
			if (*p != '"')
				return -1;
			p++;
			val_end = p;
		} else {
			while (*p && *p != ',' && *p != '}')
				p++;
			val_end = p;
		}
		while (val_end > val_start &&
		       isspace((unsigned char)*(val_end - 1)))
			val_end--;
		if ((size_t)(val_end - val_start) >= sizeof(val))
			return -1;
		memcpy(val, val_start, val_end - val_start);
		val[val_end - val_start] = '\0';
		if (strcmp(key, "version") == 0) {
			free(*version);
			*version = copy_value(val);
		} else if (strcmp(key, "registry") == 0) {
			free(*registry);
			*registry = copy_value(val);
		}
		p = hel_skip_ws(p);
		if (*p == ',') {
			p++;
			continue;
		}
		if (*p == '}')
			break;
		return -1;
	}
	p = hel_skip_ws(p);
	if (*p != '}')
		return -1;
	return 0;
}

static int parse_line(struct hel_manifest *m, char *line,
		      char **section)
{
	char *p = hel_trim(line);
	char *eq;
	char *key;
	char *value;
	char buf[1024];

	if (*p == '\0' || *p == '#')
		return 0;
	if (p[0] == '[') {
		char *end = strchr(p, ']');

		if (!end)
			return -1;
		*end = '\0';
		free(*section);
		*section = hel_xstrdup(p + 1);
		return 0;
	}
	eq = strchr(p, '=');
	if (!eq)
		return -1;
	*eq = '\0';
	key = hel_trim(p);
	value = hel_trim(eq + 1);
	if (strcmp(*section, "package") == 0) {
		if (hel_unquote(value, buf, sizeof(buf)) < 0)
			buf[0] = '\0';
		if (strcmp(key, "name") == 0) {
			free(m->name);
			m->name = hel_xstrdup(buf[0] ? buf : value);
		} else if (strcmp(key, "version") == 0) {
			free(m->version);
			m->version = hel_xstrdup(buf[0] ? buf : value);
		} else if (strcmp(key, "edition") == 0) {
			free(m->edition);
			m->edition = hel_xstrdup(buf[0] ? buf : value);
		}
	} else if (strcmp(*section, "dependencies") == 0) {
		char *v = NULL;
		char *r = NULL;

		if (value[0] == '{') {
			if (parse_inline_table(value, &v, &r) < 0 || !v) {
				free(v);
				free(r);
				return -1;
			}
		} else {
			v = copy_value(value);
		}
		hel_manifest_add_dep(m, key, v, r);
		free(v);
		free(r);
	}
	return 0;
}

struct hel_manifest *hel_manifest_read(const char *path, char **error)
{
	struct hel_manifest *m = hel_manifest_new();
	char *text = hel_read_file(path);
	char *line;
	char *saveptr = NULL;
	char *section = NULL;

	if (!text) {
		if (error)
			*error = hel_xstrdup("Heliumfile not found");
		return NULL;
	}
	section = hel_xstrdup("");
	line = strtok_r(text, "\n", &saveptr);
	while (line) {
		if (parse_line(m, line, &section) < 0) {
			if (error)
				*error = hel_xstrdup("invalid Heliumfile syntax");
			hel_manifest_free(m);
			free(section);
			free(text);
			return NULL;
		}
		line = strtok_r(NULL, "\n", &saveptr);
	}
	free(section);
	free(text);
	if (!m->name)
		m->name = hel_xstrdup("");
	if (!m->version)
		m->version = hel_xstrdup("0.1.0");
	if (!m->edition)
		m->edition = hel_xstrdup("2025");
	return m;
}

int hel_manifest_write(const struct hel_manifest *m, const char *path,
		       char **error)
{
	FILE *f;
	size_t i;

	f = fopen(path, "w");
	if (!f) {
		if (error)
			*error = hel_xstrdup("cannot write Heliumfile");
		return -1;
	}
	fprintf(f, "[package]\n");
	fprintf(f, "name = \"%s\"\n", m->name);
	fprintf(f, "version = \"%s\"\n", m->version);
	fprintf(f, "edition = \"%s\"\n", m->edition);
	fprintf(f, "\n[dependencies]\n");
	for (i = 0; i < m->dep_count; i++) {
		const struct hel_dep *d = &m->deps[i];

		if (d->registry) {
			fprintf(f, "%s = { version = \"%s\", registry = \"%s\" }\n",
				d->name, d->version, d->registry);
		} else {
			fprintf(f, "%s = \"%s\"\n", d->name, d->version);
		}
	}
	fclose(f);
	return 0;
}
