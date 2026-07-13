/* SPDX-License-Identifier: TBD */
/*
 * manifest.h - Heliumfile parser and writer.
 */

#ifndef HEL_MANIFEST_H
#define HEL_MANIFEST_H

#include <stddef.h>

struct hel_dep {
	char *name;
	char *version;
	char *registry;
};

struct hel_manifest {
	char *name;
	char *version;
	char *edition;
	struct hel_dep *deps;
	size_t dep_count;
	size_t dep_cap;
};

struct hel_manifest *hel_manifest_new(void);
void hel_manifest_free(struct hel_manifest *m);

struct hel_dep *hel_manifest_find_dep(const struct hel_manifest *m,
				      const char *name);
int hel_manifest_add_dep(struct hel_manifest *m, const char *name,
			 const char *version, const char *registry);
int hel_manifest_remove_dep(struct hel_manifest *m, const char *name);

struct hel_manifest *hel_manifest_read(const char *path, char **error);
int hel_manifest_write(const struct hel_manifest *m, const char *path,
		       char **error);

#endif /* HEL_MANIFEST_H */
