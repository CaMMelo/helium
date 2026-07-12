/* SPDX-License-Identifier: TBD */
/*
 * type_env.c - Type environment and schemes for Helium.
 */

#include "type_env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xalloc(size_t size)
{
	void *p = calloc(1, size);

	if (!p)
		abort();
	return p;
}

static char *xstrdup(const char *s)
{
	if (!s)
		return NULL;
	return strdup(s);
}

static void append_ptr(void ***items, size_t *count, size_t *cap, void *item)
{
	if (*count == *cap) {
		size_t newcap = *cap ? *cap * 2 : 4;
		void **tmp = realloc(*items, newcap * sizeof(*tmp));

		if (!tmp)
			abort();
		*items = tmp;
		*cap = newcap;
	}
	(*items)[(*count)++] = item;
}

static void append_string(char ***items, size_t *count, size_t *cap,
			  char *item)
{
	if (*count == *cap) {
		size_t newcap = *cap ? *cap * 2 : 4;
		char **tmp = realloc(*items, newcap * sizeof(*tmp));

		if (!tmp)
			abort();
		*items = tmp;
		*cap = newcap;
	}
	(*items)[(*count)++] = item;
}

static int string_in_list(const char *s, char **list, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (strcmp(list[i], s) == 0)
			return 1;
	}
	return 0;
}

struct helium_env *helium_env_new(void)
{
	return xalloc(sizeof(struct helium_env));
}

static void scheme_free_contents(struct helium_scheme *scheme)
{
	size_t i;

	if (!scheme)
		return;
	for (i = 0; i < scheme->var_count; i++)
		free(scheme->vars[i]);
	free(scheme->vars);
	helium_type_free(scheme->type);
	scheme->vars = NULL;
	scheme->var_count = 0;
	scheme->type = NULL;
}

void helium_env_free(struct helium_env *env)
{
	size_t i;
	struct helium_type_def_info *info;

	if (!env)
		return;

	for (i = 0; i < env->binding_count; i++) {
		free(env->bindings[i]->name);
		scheme_free_contents(&env->bindings[i]->scheme);
		free(env->bindings[i]);
	}
	free(env->bindings);

	for (i = 0; i < env->def_count; i++) {
		info = env->defs[i];
		if (info->param_vars) {
			size_t j;

			for (j = 0; j < info->def->param_count; j++)
				helium_type_free(info->param_vars[j]);
			free(info->param_vars);
		}
		free(info);
	}
	free(env->defs);

	for (i = 0; i < env->constructor_count; i++) {
		free(env->constructors[i]->name);
		scheme_free_contents(&env->constructors[i]->scheme);
		free(env->constructors[i]);
	}
	free(env->constructors);

	free(env);
}

static void scheme_copy(struct helium_scheme *dst,
			const struct helium_scheme *src)
{
	size_t i;

	dst->type = helium_type_copy(src->type);
	dst->var_count = src->var_count;
	dst->vars = NULL;
	if (src->var_count) {
		dst->vars = malloc(src->var_count * sizeof(*dst->vars));
		if (!dst->vars)
			abort();
		for (i = 0; i < src->var_count; i++)
			dst->vars[i] = xstrdup(src->vars[i]);
	}
}

void helium_env_add_binding(struct helium_env *env, const char *name,
			    struct helium_scheme *scheme)
{
	struct helium_env_binding *binding = xalloc(sizeof(*binding));

	binding->name = xstrdup(name);
	scheme_copy(&binding->scheme, scheme);
	append_ptr((void ***)&env->bindings, &env->binding_count,
		   &env->binding_capacity, binding);
}

int helium_env_update_binding(struct helium_env *env, const char *name,
			      struct helium_scheme *scheme)
{
	size_t i;

	for (i = 0; i < env->binding_count; i++) {
		if (strcmp(env->bindings[i]->name, name) == 0) {
			scheme_free_contents(&env->bindings[i]->scheme);
			scheme_copy(&env->bindings[i]->scheme, scheme);
			return 0;
		}
	}
	helium_env_add_binding(env, name, scheme);
	return 0;
}

struct helium_scheme *helium_env_lookup(struct helium_env *env,
					const char *name)
{
	size_t i;

	if (!env)
		return NULL;
	for (i = 0; i < env->binding_count; i++) {
		if (strcmp(env->bindings[i]->name, name) == 0)
			return &env->bindings[i]->scheme;
	}
	return NULL;
}

void helium_env_add_def(struct helium_env *env, struct helium_type_def *def,
			struct helium_type **param_vars)
{
	struct helium_type_def_info *info = xalloc(sizeof(*info));

	info->def = def;
	info->param_vars = param_vars;
	append_ptr((void ***)&env->defs, &env->def_count,
		   &env->def_capacity, info);
}

struct helium_type_def_info *helium_env_lookup_def(struct helium_env *env,
					   const char *name)
{
	size_t i;

	if (!env)
		return NULL;
	for (i = 0; i < env->def_count; i++) {
		if (strcmp(env->defs[i]->def->name, name) == 0)
			return env->defs[i];
	}
	return NULL;
}

void helium_env_add_constructor(struct helium_env *env, const char *name,
				struct helium_scheme *scheme,
				struct helium_variant *variant,
				struct helium_type_def *adt)
{
	struct helium_constructor_info *info = xalloc(sizeof(*info));

	info->name = xstrdup(name);
	scheme_copy(&info->scheme, scheme);
	info->variant = variant;
	info->adt = adt;
	append_ptr((void ***)&env->constructors, &env->constructor_count,
		   &env->constructor_capacity, info);
}

struct helium_constructor_info *helium_env_lookup_constructor(
				struct helium_env *env, const char *name)
{
	size_t i;

	if (!env)
		return NULL;
	for (i = 0; i < env->constructor_count; i++) {
		if (strcmp(env->constructors[i]->name, name) == 0)
			return env->constructors[i];
	}
	return NULL;
}

struct helium_scheme *helium_scheme_new(struct helium_type *type,
					char **vars, size_t count)
{
	struct helium_scheme *scheme = xalloc(sizeof(*scheme));
	size_t i;

	scheme->type = type;
	scheme->var_count = count;
	scheme->vars = NULL;
	if (count) {
		scheme->vars = malloc(count * sizeof(*scheme->vars));
		if (!scheme->vars)
			abort();
		for (i = 0; i < count; i++)
			scheme->vars[i] = xstrdup(vars[i]);
	}
	return scheme;
}

void helium_scheme_free(struct helium_scheme *scheme)
{
	if (!scheme)
		return;
	scheme_free_contents(scheme);
	free(scheme);
}

static char **difference(char **a, size_t a_count, char **b, size_t b_count,
			 size_t *out_count)
{
	char **res = NULL;
	size_t i;
	size_t count = 0;
	size_t cap = 0;

	for (i = 0; i < a_count; i++) {
		if (!string_in_list(a[i], b, b_count))
			append_string(&res, &count, &cap, xstrdup(a[i]));
	}
	*out_count = count;
	return res;
}

struct helium_scheme *helium_scheme_generalize(struct helium_type *type,
					       struct helium_env *env,
					       struct helium_subst *subst)
{
	struct helium_type *applied;
	char **type_vars = NULL;
	size_t type_count = 0;
	char **env_vars = NULL;
	size_t env_count = 0;
	char **bound_vars;
	size_t bound_count;
	struct helium_scheme *scheme;

	applied = helium_type_apply(subst, type);
	helium_type_free_vars(applied, &type_vars, &type_count);
	helium_env_free_vars(env, &env_vars, &env_count);

	bound_vars = difference(type_vars, type_count, env_vars, env_count,
				&bound_count);

	scheme = helium_scheme_new(applied, bound_vars, bound_count);

	/* Free temporary variable name arrays. */
	{
		size_t i;

		for (i = 0; i < type_count; i++)
			free(type_vars[i]);
		free(type_vars);
		for (i = 0; i < env_count; i++)
			free(env_vars[i]);
		free(env_vars);
		for (i = 0; i < bound_count; i++)
			free(bound_vars[i]);
		free(bound_vars);
	}

	return scheme;
}

static struct helium_type *copy_with_renames(const struct helium_type *type,
					     char **from, char **to,
					     size_t count)
{
	struct helium_type *copy;
	size_t i;

	if (!type)
		return NULL;

	if (type->kind == HELIUM_TYPE_VAR) {
		for (i = 0; i < count; i++) {
			if (strcmp(type->name, from[i]) == 0)
				return helium_type_var(to[i],
						       type->line,
						       type->col);
		}
		return helium_type_copy(type);
	}

	copy = xalloc(sizeof(*copy));
	copy->kind = type->kind;
	copy->line = type->line;
	copy->col = type->col;
	copy->name = xstrdup(type->name);
	copy->array_size = type->array_size;

	for (i = 0; i < type->arg_count; i++)
		helium_type_add_arg(copy, copy_with_renames(type->args[i],
							    from, to,
							    count));
	for (i = 0; i < type->param_count; i++)
		helium_type_add_param(copy,
				      copy_with_renames(type->params[i],
							from, to,
							count));
	copy->ret = copy_with_renames(type->ret, from, to, count);
	copy->elem_type = copy_with_renames(type->elem_type, from, to, count);
	return copy;
}

struct helium_type *helium_scheme_instantiate(struct helium_scheme *scheme,
					      struct helium_subst *subst)
{
	char **to;
	size_t i;
	struct helium_type *type;

	(void)subst;

	if (scheme->var_count == 0)
		return helium_type_copy(scheme->type);

	to = malloc(scheme->var_count * sizeof(*to));
	if (!to)
		abort();
	for (i = 0; i < scheme->var_count; i++) {
		struct helium_type *v = helium_type_fresh_var(0, 0);

		to[i] = xstrdup(v->name);
		helium_type_free(v);
	}

	type = copy_with_renames(scheme->type, scheme->vars, to,
				 scheme->var_count);
	for (i = 0; i < scheme->var_count; i++)
		free(to[i]);
	free(to);
	return type;
}

void helium_env_free_vars(struct helium_env *env, char ***vars,
			  size_t *count)
{
	char **res = NULL;
	size_t i;
	size_t total = 0;
	size_t cap = 0;

	*vars = NULL;
	*count = 0;
	if (!env)
		return;

	for (i = 0; i < env->binding_count; i++) {
		struct helium_scheme *scheme = &env->bindings[i]->scheme;
		char **fv = NULL;
		size_t fv_count = 0;
		size_t j;
		struct helium_type *applied;

		applied = helium_type_apply(NULL, scheme->type);
		helium_type_free_vars(applied, &fv, &fv_count);
		helium_type_free(applied);
		for (j = 0; j < fv_count; j++) {
			if (!string_in_list(fv[j], scheme->vars, scheme->var_count) &&
			    !string_in_list(fv[j], res, total))
				append_string(&res, &total, &cap, xstrdup(fv[j]));
			free(fv[j]);
		}
		free(fv);
	}

	for (i = 0; i < env->constructor_count; i++) {
		struct helium_scheme *scheme = &env->constructors[i]->scheme;
		char **fv = NULL;
		size_t fv_count = 0;
		size_t j;
		struct helium_type *applied;

		applied = helium_type_apply(NULL, scheme->type);
		helium_type_free_vars(applied, &fv, &fv_count);
		helium_type_free(applied);
		for (j = 0; j < fv_count; j++) {
			if (!string_in_list(fv[j], scheme->vars, scheme->var_count) &&
			    !string_in_list(fv[j], res, total))
				append_string(&res, &total, &cap, xstrdup(fv[j]));
			free(fv[j]);
		}
		free(fv);
	}

	*vars = res;
	*count = total;
}
