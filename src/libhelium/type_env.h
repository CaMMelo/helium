/* SPDX-License-Identifier: TBD */
/*
 * type_env.h - Type environment and module interfaces for Helium.
 */

#ifndef HELIUM_TYPE_ENV_H
#define HELIUM_TYPE_ENV_H

#include "ast.h"
#include "types.h"

struct helium_scheme {
	char **vars;
	size_t var_count;
	struct helium_type *type;
};

struct helium_env_binding {
	char *name;
	struct helium_scheme scheme;
};

struct helium_type_def_info {
	struct helium_type_def *def;
	struct helium_type **param_vars;
};

struct helium_constructor_info {
	char *name;
	struct helium_scheme scheme;
	struct helium_variant *variant;
	struct helium_type_def *adt;
};

struct helium_env {
	struct helium_env_binding **bindings;
	size_t binding_count;
	size_t binding_capacity;

	struct helium_type_def_info **defs;
	size_t def_count;
	size_t def_capacity;

	struct helium_constructor_info **constructors;
	size_t constructor_count;
	size_t constructor_capacity;
};

struct helium_env *helium_env_new(void);
void helium_env_free(struct helium_env *env);

void helium_env_add_binding(struct helium_env *env, const char *name,
			    struct helium_scheme *scheme);
int helium_env_update_binding(struct helium_env *env, const char *name,
			      struct helium_scheme *scheme);
struct helium_scheme *helium_env_lookup(struct helium_env *env,
					const char *name);

void helium_env_add_def(struct helium_env *env, struct helium_type_def *def,
			struct helium_type **param_vars);
struct helium_type_def_info *helium_env_lookup_def(struct helium_env *env,
					   const char *name);

void helium_env_add_constructor(struct helium_env *env, const char *name,
				struct helium_scheme *scheme,
				struct helium_variant *variant,
				struct helium_type_def *adt);
struct helium_constructor_info *helium_env_lookup_constructor(
				struct helium_env *env, const char *name);

struct helium_scheme *helium_scheme_new(struct helium_type *type,
					char **vars, size_t count);
void helium_scheme_free(struct helium_scheme *scheme);
struct helium_scheme *helium_scheme_generalize(struct helium_type *type,
					       struct helium_env *env,
					       struct helium_subst *subst);
struct helium_type *helium_scheme_instantiate(struct helium_scheme *scheme,
					      struct helium_subst *subst);

void helium_env_free_vars(struct helium_env *env, char ***vars,
			  size_t *count);

#endif /* HELIUM_TYPE_ENV_H */
