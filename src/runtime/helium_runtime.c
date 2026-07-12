/* SPDX-License-Identifier: TBD */
/*
 * helium_runtime.c - reference-counting runtime (bootstrap placeholder).
 *
 * The real implementation is tracked in specs/SPEC-005-runtime.md.
 */

#include <stddef.h>

struct helium_object {
	unsigned long refcount;
};

void helium_retain(struct helium_object *obj)
{
	if (obj)
		obj->refcount++;
}

void helium_release(struct helium_object *obj)
{
	if (obj && --obj->refcount == 0) {
		/* TODO: free object */
	}
}
