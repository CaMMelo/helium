/* SPDX-License-Identifier: TBD */
/*
 * lock.h - Heliumfile.lock parser and writer.
 */

#ifndef HEL_LOCK_H
#define HEL_LOCK_H

#include <stddef.h>

struct hel_lock_entry {
	char *name;
	char *version;
	char *checksum;
};

struct hel_lock {
	char *name;
	char *version;
	char *edition;
	struct hel_lock_entry *deps;
	size_t dep_count;
	size_t dep_cap;
};

struct hel_lock *hel_lock_new(void);
void hel_lock_free(struct hel_lock *l);

struct hel_lock_entry *hel_lock_find_dep(const struct hel_lock *l,
					 const char *name);
int hel_lock_add_dep(struct hel_lock *l, const char *name,
		     const char *version, const char *checksum);
int hel_lock_remove_dep(struct hel_lock *l, const char *name);

struct hel_lock *hel_lock_read(const char *path, char **error);
int hel_lock_write(const struct hel_lock *l, const char *path,
		   char **error);

#endif /* HEL_LOCK_H */
