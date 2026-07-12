/* SPDX-License-Identifier: TBD */
/*
 * types.c - Type representation and unification.
 */

#include "types.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct helium_subst {
	char *name;
	struct helium_type *type;
	struct helium_subst *next;
};

static int push_subst(struct helium_subst **subst, const char *name,
		      struct helium_type *type);

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

static int string_in_list(const char *name, char **vars, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (strcmp(vars[i], name) == 0)
			return 1;
	}
	return 0;
}

struct helium_type *helium_type_fresh_var(int line, int col)
{
	static int counter;
	char buf[32];
	struct helium_type *type;

	snprintf(buf, sizeof(buf), "_t%d", counter++);
	type = helium_type_var(buf, line, col);
	return type;
}

struct helium_type *helium_type_copy(const struct helium_type *type)
{
	struct helium_type *copy;
	size_t i;

	if (!type)
		return NULL;

	copy = xalloc(sizeof(*copy));
	copy->kind = type->kind;
	copy->line = type->line;
	copy->col = type->col;
	copy->name = xstrdup(type->name);
	copy->array_size = type->array_size;

	for (i = 0; i < type->arg_count; i++)
		helium_type_add_arg(copy, helium_type_copy(type->args[i]));
	for (i = 0; i < type->param_count; i++)
		helium_type_add_param(copy, helium_type_copy(type->params[i]));
	copy->ret = helium_type_copy(type->ret);
	copy->elem_type = helium_type_copy(type->elem_type);
	return copy;
}

static void type_string_append(char **buf, size_t *len, size_t *cap,
			       const char *text)
{
	size_t n = strlen(text);

	if (*len + n + 1 > *cap) {
		size_t newcap = *cap ? *cap * 2 : 64;

		while (*len + n + 1 > newcap)
			newcap *= 2;
		*buf = realloc(*buf, newcap);
		if (!*buf)
			abort();
		*cap = newcap;
	}
	memcpy(*buf + *len, text, n + 1);
	*len += n;
}

static void type_to_string_impl(const struct helium_type *type, char **buf,
				size_t *len, size_t *cap)
{
	size_t i;
	char tmp[64];

	if (!type) {
		type_string_append(buf, len, cap, "<null>");
		return;
	}

	switch (type->kind) {
	case HELIUM_TYPE_VAR:
		type_string_append(buf, len, cap, type->name ? type->name : "_");
		break;
	case HELIUM_TYPE_NAMED:
		type_string_append(buf, len, cap, type->name ? type->name : "?");
		if (type->arg_count > 0) {
			type_string_append(buf, len, cap, "<");
			for (i = 0; i < type->arg_count; i++) {
				if (i > 0)
					type_string_append(buf, len, cap, ", ");
				type_to_string_impl(type->args[i], buf, len, cap);
			}
			type_string_append(buf, len, cap, ">");
		}
		break;
	case HELIUM_TYPE_FN:
		type_string_append(buf, len, cap, "fn(");
		for (i = 0; i < type->param_count; i++) {
			if (i > 0)
				type_string_append(buf, len, cap, ", ");
			type_to_string_impl(type->params[i], buf, len, cap);
		}
		type_string_append(buf, len, cap, ") -> ");
		type_to_string_impl(type->ret, buf, len, cap);
		break;
	case HELIUM_TYPE_ARRAY:
		type_string_append(buf, len, cap, "[");
		type_to_string_impl(type->elem_type, buf, len, cap);
		snprintf(tmp, sizeof(tmp), "; %ld", type->array_size);
		type_string_append(buf, len, cap, tmp);
		type_string_append(buf, len, cap, "]");
		break;
	}
}

void helium_type_to_string(const struct helium_type *type, char *buf,
			   size_t size)
{
	char *tmp = NULL;
	size_t len = 0;
	size_t cap = 0;

	type_to_string_impl(type, &tmp, &len, &cap);
	if (!tmp) {
		if (size > 0)
			buf[0] = '\0';
		return;
	}
	if (size > 0) {
		memcpy(buf, tmp, size - 1);
		buf[size - 1] = '\0';
	}
	free(tmp);
}

static char *type_to_string_alloc(const struct helium_type *type)
{
	char *tmp = NULL;
	size_t len = 0;
	size_t cap = 0;

	type_to_string_impl(type, &tmp, &len, &cap);
	return tmp;
}

int helium_type_eq(const struct helium_type *a, const struct helium_type *b)
{
	size_t i;

	if (!a || !b)
		return a == b;
	if (a->kind != b->kind)
		return 0;
	if (a->arg_count != b->arg_count)
		return 0;
	if (a->param_count != b->param_count)
		return 0;
	if (a->array_size != b->array_size)
		return 0;

	if (a->name && b->name && strcmp(a->name, b->name) != 0)
		return 0;
	if ((a->name && !b->name) || (!a->name && b->name))
		return 0;

	for (i = 0; i < a->arg_count; i++) {
		if (!helium_type_eq(a->args[i], b->args[i]))
			return 0;
	}
	for (i = 0; i < a->param_count; i++) {
		if (!helium_type_eq(a->params[i], b->params[i]))
			return 0;
	}
	if (!helium_type_eq(a->ret, b->ret))
		return 0;
	if (!helium_type_eq(a->elem_type, b->elem_type))
		return 0;
	return 1;
}

struct helium_subst *helium_subst_new(void)
{
	return NULL;
}

void helium_subst_free(struct helium_subst *subst)
{
	struct helium_subst *next;

	while (subst) {
		next = subst->next;
		free(subst->name);
		/* Types in substitutions are owned by the inference process. */
		free(subst);
		subst = next;
	}
}

struct helium_type *helium_subst_get(struct helium_subst *subst,
				     const char *name)
{
	while (subst) {
		if (strcmp(subst->name, name) == 0)
			return subst->type;
		subst = subst->next;
	}
	return NULL;
}

void helium_subst_set(struct helium_subst **subst, const char *name,
		      struct helium_type *type)
{
	push_subst(subst, name, type);
}

struct helium_type *helium_type_apply(struct helium_subst *subst,
				      const struct helium_type *type)
{
	struct helium_type *mapping;
	struct helium_type *copy;
	size_t i;

	if (!type)
		return NULL;

	if (type->kind == HELIUM_TYPE_VAR) {
		mapping = helium_subst_get(subst, type->name);
		if (mapping)
			return helium_type_apply(subst, mapping);
		return helium_type_copy(type);
	}

	copy = xalloc(sizeof(*copy));
	copy->kind = type->kind;
	copy->line = type->line;
	copy->col = type->col;
	copy->name = xstrdup(type->name);
	copy->array_size = type->array_size;

	for (i = 0; i < type->arg_count; i++)
		helium_type_add_arg(copy, helium_type_apply(subst, type->args[i]));
	for (i = 0; i < type->param_count; i++)
		helium_type_add_param(copy,
				      helium_type_apply(subst, type->params[i]));
	copy->ret = helium_type_apply(subst, type->ret);
	copy->elem_type = helium_type_apply(subst, type->elem_type);
	return copy;
}

int helium_type_occurs(const char *name, const struct helium_type *type)
{
	size_t i;

	if (!type)
		return 0;

	if (type->kind == HELIUM_TYPE_VAR) {
		if (type->name && strcmp(type->name, name) == 0)
			return 1;
		return 0;
	}

	for (i = 0; i < type->arg_count; i++) {
		if (helium_type_occurs(name, type->args[i]))
			return 1;
	}
	for (i = 0; i < type->param_count; i++) {
		if (helium_type_occurs(name, type->params[i]))
			return 1;
	}
	if (helium_type_occurs(name, type->ret))
		return 1;
	if (helium_type_occurs(name, type->elem_type))
		return 1;
	return 0;
}

static void free_vars_impl(const struct helium_type *type, char ***vars,
			   size_t *count, size_t *cap)
{
	size_t i;

	if (!type)
		return;

	if (type->kind == HELIUM_TYPE_VAR) {
		if (type->name && !string_in_list(type->name, *vars, *count))
			append_string(vars, count, cap, xstrdup(type->name));
		return;
	}

	for (i = 0; i < type->arg_count; i++)
		free_vars_impl(type->args[i], vars, count, cap);
	for (i = 0; i < type->param_count; i++)
		free_vars_impl(type->params[i], vars, count, cap);
	free_vars_impl(type->ret, vars, count, cap);
	free_vars_impl(type->elem_type, vars, count, cap);
}

void helium_type_free_vars(const struct helium_type *type, char ***vars,
			   size_t *count)
{
	size_t cap = 0;

	*vars = NULL;
	*count = 0;
	free_vars_impl(type, vars, count, &cap);
}

int helium_type_is_unit(const struct helium_type *type)
{
	return type && type->kind == HELIUM_TYPE_NAMED &&
	       type->name && strcmp(type->name, "()") == 0;
}

int helium_type_is_numeric(const struct helium_type *type)
{
	if (!type || type->kind != HELIUM_TYPE_NAMED || !type->name)
		return 0;
	return strcmp(type->name, "i8") == 0 ||
	       strcmp(type->name, "i16") == 0 ||
	       strcmp(type->name, "i32") == 0 ||
	       strcmp(type->name, "i64") == 0 ||
	       strcmp(type->name, "u8") == 0 ||
	       strcmp(type->name, "u16") == 0 ||
	       strcmp(type->name, "u32") == 0 ||
	       strcmp(type->name, "u64") == 0 ||
	       strcmp(type->name, "f32") == 0 ||
	       strcmp(type->name, "f64") == 0;
}

static void format_error(char **error, const char *fmt, ...)
{
	va_list ap;
	char *msg;
	int n;

	va_start(ap, fmt);
	n = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (n < 0) {
		*error = strdup("type error");
		return;
	}
	*error = msg;
}

static int push_subst(struct helium_subst **subst, const char *name,
		      struct helium_type *type);
static int unify_applied(struct helium_type *a, struct helium_type *b,
			 struct helium_subst **subst, char **error);

int helium_type_unify(struct helium_type *t1, struct helium_type *t2,
		      struct helium_subst **subst, char **error)
{
	struct helium_type *a = helium_type_apply(*subst, t1);
	struct helium_type *b = helium_type_apply(*subst, t2);
	int rc;

	rc = unify_applied(a, b, subst, error);
	helium_type_free(a);
	helium_type_free(b);
	return rc;
}

static int push_subst(struct helium_subst **subst, const char *name,
		      struct helium_type *type)
{
	struct helium_subst *entry = xalloc(sizeof(*entry));

	entry->name = xstrdup(name);
	entry->type = type;
	entry->next = *subst;
	*subst = entry;
	return 0;
}

static int unify_applied(struct helium_type *a, struct helium_type *b,
			 struct helium_subst **subst, char **error)
{
	struct helium_type *type;
	char *sa;
	char *sb;
	size_t i;

	if (a->kind == HELIUM_TYPE_VAR && b->kind == HELIUM_TYPE_VAR &&
	    a->name && b->name && strcmp(a->name, b->name) == 0)
		return 0;

	if (a->kind == HELIUM_TYPE_VAR) {
		if (helium_type_occurs(a->name, b)) {
			format_error(error,
				     "type error at %d:%d: occurs check failed for %s",
				     a->line, a->col, a->name);
			return -1;
		}
		type = helium_type_copy(b);
		return push_subst(subst, a->name, type);
	}

	if (b->kind == HELIUM_TYPE_VAR) {
		if (helium_type_occurs(b->name, a)) {
			format_error(error,
				     "type error at %d:%d: occurs check failed for %s",
				     b->line, b->col, b->name);
			return -1;
		}
		type = helium_type_copy(a);
		return push_subst(subst, b->name, type);
	}

	if (a->kind == HELIUM_TYPE_NAMED && b->kind == HELIUM_TYPE_NAMED) {
		if (!a->name || !b->name ||
		    strcmp(a->name, b->name) != 0 ||
		    a->arg_count != b->arg_count) {
			sa = type_to_string_alloc(a);
			sb = type_to_string_alloc(b);
			format_error(error,
				     "type error at %d:%d: cannot unify %s with %s",
				     a->line, a->col, sa, sb);
			free(sa);
			free(sb);
			return -1;
		}
		for (i = 0; i < a->arg_count; i++) {
			if (unify_applied(a->args[i], b->args[i], subst, error) < 0)
				return -1;
		}
		return 0;
	}

	if (a->kind == HELIUM_TYPE_FN && b->kind == HELIUM_TYPE_FN) {
		if (a->param_count != b->param_count) {
			sa = type_to_string_alloc(a);
			sb = type_to_string_alloc(b);
			format_error(error,
				     "type error at %d:%d: function arity mismatch: %s vs %s",
				     a->line, a->col, sa, sb);
			free(sa);
			free(sb);
			return -1;
		}
		for (i = 0; i < a->param_count; i++) {
			if (unify_applied(a->params[i], b->params[i], subst, error) < 0)
				return -1;
		}
		return unify_applied(a->ret, b->ret, subst, error);
	}

	if (a->kind == HELIUM_TYPE_ARRAY && b->kind == HELIUM_TYPE_ARRAY) {
		if (a->array_size != b->array_size) {
			sa = type_to_string_alloc(a);
			sb = type_to_string_alloc(b);
			format_error(error,
				     "type error at %d:%d: array size mismatch: %s vs %s",
				     a->line, a->col, sa, sb);
			free(sa);
			free(sb);
			return -1;
		}
		return unify_applied(a->elem_type, b->elem_type, subst, error);
	}

	sa = type_to_string_alloc(a);
	sb = type_to_string_alloc(b);
	format_error(error,
		     "type error at %d:%d: cannot unify %s with %s",
		     a->line, a->col, sa, sb);
	free(sa);
	free(sb);
	return -1;
}
