/* SPDX-License-Identifier: TBD */
/*
 * types.h - Type representation and unification for Helium.
 */

#ifndef HELIUM_TYPES_H
#define HELIUM_TYPES_H

#include "ast.h"

struct helium_subst;

/* Create a fresh inference variable. */
struct helium_type *helium_type_fresh_var(int line, int col);

/* Deep copy of a type. */
struct helium_type *helium_type_copy(const struct helium_type *type);

/* Format a type into a human-readable string. */
void helium_type_to_string(const struct helium_type *type, char *buf,
			   size_t size);

/* Structural equality (ignoring line/column). */
int helium_type_eq(const struct helium_type *a, const struct helium_type *b);

/* Substitution operations. */
struct helium_subst *helium_subst_new(void);
void helium_subst_free(struct helium_subst *subst);
struct helium_type *helium_subst_get(struct helium_subst *subst,
				     const char *name);
void helium_subst_set(struct helium_subst **subst, const char *name,
		      struct helium_type *type);

/* Apply a substitution to a type, returning a newly allocated type. */
struct helium_type *helium_type_apply(struct helium_subst *subst,
				      const struct helium_type *type);

/* Unify two types, extending the substitution on success. */
int helium_type_unify(struct helium_type *t1, struct helium_type *t2,
		      struct helium_subst **subst, char **error);

/* Occurs check: test whether a variable name occurs in a type. */
int helium_type_occurs(const char *name, const struct helium_type *type);

/* Collect free type variable names. */
void helium_type_free_vars(const struct helium_type *type, char ***vars,
			   size_t *count);

/* Primitive type predicates. */
int helium_type_is_unit(const struct helium_type *type);
int helium_type_is_numeric(const struct helium_type *type);

#endif /* HELIUM_TYPES_H */
