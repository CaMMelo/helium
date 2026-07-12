/* SPDX-License-Identifier: TBD */
/*
 * inference.c - Type inference engine for Helium.
 */

#include "inference.h"

#include "parser.tab.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "type_env.h"

struct tscope {
	char **names;
	size_t count;
	struct tscope *parent;
};

struct infer_ctx {
	struct helium_env *env;
	struct helium_subst *subst;
	struct tscope *tscope;
	struct helium_type **loop_types;
	size_t loop_count;
};

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

/* -------------------------------------------------------------------------- */
/* Type helpers                                                               */
/* -------------------------------------------------------------------------- */

static struct helium_type *type_named(const char *name)
{
	return helium_type_named(name, 0, 0);
}

/* Substitute type parameters (as named types) with concrete args. */
static struct helium_type *subst_type_params(struct helium_type *type,
					     char **params,
					     struct helium_type **args,
					     size_t count)
{
	struct helium_type *copy;
	size_t i;

	if (!type)
		return NULL;

	if (type->kind == HELIUM_TYPE_NAMED) {
		for (i = 0; i < count; i++) {
			if (strcmp(type->name, params[i]) == 0)
				return helium_type_apply(NULL, args[i]);
		}
		copy = helium_type_named(type->name, type->line, type->col);
		for (i = 0; i < type->arg_count; i++) {
			helium_type_add_arg(copy,
					    subst_type_params(type->args[i],
							      params,
							      args,
							      count));
		}
		return copy;
	}

	if (type->kind == HELIUM_TYPE_VAR)
		return helium_type_copy(type);

	if (type->kind == HELIUM_TYPE_FN) {
		copy = helium_type_fn(type->line, type->col);
		for (i = 0; i < type->param_count; i++) {
			helium_type_add_param(copy,
					      subst_type_params(type->params[i],
								params,
								args,
								count));
		}
		copy->ret = subst_type_params(type->ret, params, args, count);
		return copy;
	}

	if (type->kind == HELIUM_TYPE_ARRAY) {
		return helium_type_array(subst_type_params(type->elem_type,
							   params,
							   args,
							   count),
					 type->array_size, type->line, type->col);
	}

	format_error(NULL, "internal error: subst_type_params unknown type kind");
	abort();
}

static struct helium_type *make_unit_type(void)
{
	return type_named("()");
}

static struct helium_type *make_bool_type(void)
{
	return type_named("bool");
}

static struct helium_type *make_i32_type(void)
{
	return type_named("i32");
}

static struct helium_type *make_f64_type(void)
{
	return type_named("f64");
}

static struct helium_type *make_str_type(void)
{
	return type_named("str");
}

static struct helium_type *make_fn_type(struct helium_type **params,
					size_t param_count,
					struct helium_type *ret)
{
	struct helium_type *fn = helium_type_fn(0, 0);
	size_t i;

	for (i = 0; i < param_count; i++)
		helium_type_add_param(fn, helium_type_copy(params[i]));
	helium_type_set_ret(fn, ret ? helium_type_copy(ret) : make_unit_type());
	return fn;
}

static struct helium_type *make_io_type(struct helium_type *arg)
{
	struct helium_type *io = type_named("IO");

	helium_type_add_arg(io, arg ? helium_type_copy(arg) : make_unit_type());
	return io;
}

static int type_is_integer(const struct helium_type *type)
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
	       strcmp(type->name, "u64") == 0;
}

static int type_is_float(const struct helium_type *type)
{
	if (!type || type->kind != HELIUM_TYPE_NAMED || !type->name)
		return 0;
	return strcmp(type->name, "f32") == 0 ||
	       strcmp(type->name, "f64") == 0;
}

/* -------------------------------------------------------------------------- */
/* Inferred type recording                                                    */
/* -------------------------------------------------------------------------- */

static void record_expr_type(struct helium_expr *expr, struct helium_type *type)
{
	if (!expr)
		return;
	helium_type_free(expr->inferred_type);
	expr->inferred_type = type ? helium_type_copy(type) : NULL;
}

/* -------------------------------------------------------------------------- */
/* Type parameter scope                                                       */
/* -------------------------------------------------------------------------- */

static int tscope_contains(struct tscope *scope, const char *name)
{
	while (scope) {
		size_t i;

		for (i = 0; i < scope->count; i++) {
			if (strcmp(scope->names[i], name) == 0)
				return 1;
		}
		scope = scope->parent;
	}
	return 0;
}

static struct tscope *tscope_push(struct tscope *parent, char **names,
				  size_t count)
{
	struct tscope *scope = xalloc(sizeof(*scope));
	size_t i;

	scope->parent = parent;
	scope->count = count;
	if (count) {
		scope->names = malloc(count * sizeof(*scope->names));
		if (!scope->names)
			abort();
		for (i = 0; i < count; i++)
			scope->names[i] = xstrdup(names[i]);
	}
	return scope;
}

static void tscope_free(struct tscope *scope)
{
	size_t i;

	if (!scope)
		return;
	for (i = 0; i < scope->count; i++)
		free(scope->names[i]);
	free(scope->names);
	free(scope);
}

/* -------------------------------------------------------------------------- */
/* Type annotation resolution                                                 */
/* -------------------------------------------------------------------------- */

static int resolve_type(struct helium_type *type, struct tscope *scope,
			struct helium_type **out, char **error)
{
	struct helium_type *resolved;
	size_t i;

	if (!type) {
		*out = NULL;
		return 0;
	}

	if (type->kind == HELIUM_TYPE_VAR) {
		*out = helium_type_copy(type);
		return 0;
	}

	if (type->kind == HELIUM_TYPE_NAMED) {
		if (tscope_contains(scope, type->name)) {
			*out = helium_type_var(type->name, type->line, type->col);
			return 0;
		}
		resolved = helium_type_named(type->name, type->line, type->col);
		for (i = 0; i < type->arg_count; i++) {
			struct helium_type *arg;

			if (resolve_type(type->args[i], scope, &arg, error) < 0)
				return -1;
			helium_type_add_arg(resolved, arg);
		}
		*out = resolved;
		return 0;
	}

	if (type->kind == HELIUM_TYPE_FN) {
		resolved = helium_type_fn(type->line, type->col);
		for (i = 0; i < type->param_count; i++) {
			struct helium_type *param;

			if (resolve_type(type->params[i], scope, &param, error) < 0)
				return -1;
			helium_type_add_param(resolved, param);
		}
		if (type->ret) {
			struct helium_type *ret;

			if (resolve_type(type->ret, scope, &ret, error) < 0)
				return -1;
			helium_type_set_ret(resolved, ret);
		}
		*out = resolved;
		return 0;
	}

	if (type->kind == HELIUM_TYPE_ARRAY) {
		struct helium_type *elem;

		if (resolve_type(type->elem_type, scope, &elem, error) < 0)
			return -1;
		resolved = helium_type_array(elem, type->array_size,
					     type->line, type->col);
		*out = resolved;
		return 0;
	}

	format_error(error, "internal error: unknown type kind");
	return -1;
}

/* -------------------------------------------------------------------------- */
/* Type definition registration                                               */
/* -------------------------------------------------------------------------- */

static struct helium_type **make_param_vars(struct helium_type_def *def)
{
	struct helium_type **vars;
	size_t i;

	if (def->param_count == 0)
		return NULL;
	vars = malloc(def->param_count * sizeof(*vars));
	if (!vars)
		abort();
	for (i = 0; i < def->param_count; i++)
		vars[i] = helium_type_var(def->params[i], 0, 0);
	return vars;
}

static struct helium_type *make_adt_type(struct helium_type_def *def,
					 struct helium_type **param_vars)
{
	struct helium_type *type = helium_type_named(def->name, 0, 0);
	size_t i;

	for (i = 0; i < def->param_count; i++)
		helium_type_add_arg(type, helium_type_copy(param_vars[i]));
	return type;
}

static int register_type_def(struct helium_env *env,
			     struct helium_type_def *def,
			     char **error)
{
	struct tscope *scope;
	struct helium_type **param_vars;
	struct helium_type *adt_type;
	size_t i;

	(void)error;

	param_vars = make_param_vars(def);
	scope = tscope_push(NULL, def->params, def->param_count);
	helium_env_add_def(env, def, param_vars);

	if (def->kind == HELIUM_TYPE_DEF_RECORD) {
		struct helium_type **fields;
		struct helium_type *record_type;
		struct helium_type *fn_type;
		struct helium_scheme *scheme;
		char **bound = NULL;
		size_t bound_count = 0;

		fields = malloc(def->u.record.field_count * sizeof(*fields));
		if (!fields)
			abort();
		for (i = 0; i < def->u.record.field_count; i++) {
			if (resolve_type(def->u.record.fields[i]->type, scope,
					 &fields[i], error) < 0)
				return -1;
		}

		record_type = helium_type_named(def->name, 0, 0);
		for (i = 0; i < def->param_count; i++)
			helium_type_add_arg(record_type,
					    helium_type_copy(param_vars[i]));

		fn_type = make_fn_type(fields, def->u.record.field_count,
				       helium_type_copy(record_type));
		if (def->param_count > 0) {
			bound = malloc(def->param_count * sizeof(*bound));
			if (!bound)
				abort();
			for (i = 0; i < def->param_count; i++)
				bound[i] = xstrdup(def->params[i]);
			bound_count = def->param_count;
		}
		scheme = helium_scheme_new(fn_type, bound, bound_count);
		helium_env_add_constructor(env, def->name, scheme, NULL, def);
		helium_scheme_free(scheme);

		for (i = 0; i < def->u.record.field_count; i++)
			helium_type_free(fields[i]);
		free(fields);
		helium_type_free(record_type);
	} else {
		adt_type = make_adt_type(def, param_vars);
		for (i = 0; i < def->u.adt.variant_count; i++) {
			struct helium_variant *variant = def->u.adt.variants[i];
			struct helium_type *ret;
			struct helium_type *fn_type;
			struct helium_type **payload;
			struct helium_scheme *scheme;
			char **bound = NULL;
			size_t bound_count = 0;
			size_t j;
			size_t payload_count;

			ret = helium_type_copy(adt_type);
			payload_count = variant->type_arg_count;
			if (variant->field_count > 0)
				payload_count = variant->field_count;

			payload = malloc(payload_count * sizeof(*payload));
			if (!payload)
				abort();

			if (variant->field_count > 0) {
				for (j = 0; j < variant->field_count; j++) {
					if (resolve_type(variant->fields[j]->type,
							 scope, &payload[j],
							 error) < 0)
						return -1;
				}
			} else {
				for (j = 0; j < variant->type_arg_count; j++) {
					if (resolve_type(variant->type_args[j],
							 scope, &payload[j],
							 error) < 0)
						return -1;
				}
			}

			if (payload_count > 0) {
				fn_type = make_fn_type(payload, payload_count,
						       ret);
			} else {
				fn_type = ret;
			}

			if (def->param_count > 0) {
				bound = malloc(def->param_count * sizeof(*bound));
				if (!bound)
					abort();
				for (j = 0; j < def->param_count; j++)
					bound[j] = xstrdup(def->params[j]);
				bound_count = def->param_count;
			}

			scheme = helium_scheme_new(fn_type, bound, bound_count);
			helium_env_add_constructor(env, variant->name, scheme,
						 variant, def);
			helium_scheme_free(scheme);

			for (j = 0; j < payload_count; j++)
				helium_type_free(payload[j]);
			free(payload);
			if (payload_count > 0)
				helium_type_free(ret);
		}
		helium_type_free(adt_type);
	}

	tscope_free(scope);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Forward declarations                                                       */
/* -------------------------------------------------------------------------- */

static int infer_expr(struct helium_expr *expr, struct infer_ctx *ctx,
		      struct helium_type **out, char **error);
static int infer_pattern(struct helium_pattern *pat, struct infer_ctx *ctx,
			 struct helium_type **out, char **error);
static int infer_binding(struct helium_binding *binding, struct infer_ctx *ctx,
			 struct helium_scheme **out, char **error);

/* -------------------------------------------------------------------------- */
/* Environment snapshot helpers                                               */
/* -------------------------------------------------------------------------- */

static size_t env_snapshot(struct helium_env *env)
{
	return env->binding_count;
}

static void env_restore(struct helium_env *env, size_t snapshot)
{
	size_t i;

	for (i = snapshot; i < env->binding_count; i++) {
		struct helium_env_binding *binding = env->bindings[i];
		size_t j;

		free(binding->name);
		for (j = 0; j < binding->scheme.var_count; j++)
			free(binding->scheme.vars[j]);
		free(binding->scheme.vars);
		helium_type_free(binding->scheme.type);
		free(binding);
	}
	env->binding_count = snapshot;
}

/* -------------------------------------------------------------------------- */
/* Record/constructor literal type checking                                   */
/* -------------------------------------------------------------------------- */

static int check_record_literal(struct helium_expr *expr,
				struct infer_ctx *ctx,
				struct helium_type **out, char **error)
{
	const char *name = expr->u.record_lit.name;
	struct helium_constructor_info *info;
	struct helium_type *inst;
	struct helium_type *ret;
	struct helium_type **field_types;
	struct helium_record_field **fields;
	size_t field_count;
	size_t i;
	size_t seen_count;
	int *seen;

	info = helium_env_lookup_constructor(ctx->env, name);
	if (!info) {
		format_error(error,
			     "%d:%d: unknown record or constructor '%s'",
			     expr->line, expr->col, name);
		return -1;
	}

	inst = helium_scheme_instantiate(&info->scheme, ctx->subst);
	if (inst->kind != HELIUM_TYPE_FN) {
		format_error(error,
			     "%d:%d: constructor '%s' does not take record arguments",
			     expr->line, expr->col, name);
		helium_type_free(inst);
		return -1;
	}

	field_count = inst->param_count;
	field_types = inst->params;

	if (info->variant) {
		fields = info->variant->fields;
	} else if (info->adt && info->adt->kind == HELIUM_TYPE_DEF_RECORD) {
		fields = info->adt->u.record.fields;
	} else {
		format_error(error,
			     "%d:%d: constructor '%s' has no record fields",
			     expr->line, expr->col, name);
		helium_type_free(inst);
		return -1;
	}

	if (field_count != expr->u.record_lit.field_count) {
		format_error(error,
			     "%d:%d: constructor '%s' expects %zu fields, got %zu",
			     expr->line, expr->col, name, field_count,
			     expr->u.record_lit.field_count);
		helium_type_free(inst);
		return -1;
	}

	seen = calloc(field_count, sizeof(*seen));
	if (!seen)
		abort();
	seen_count = 0;

	for (i = 0; i < expr->u.record_lit.field_count; i++) {
		struct helium_field_init *init = expr->u.record_lit.fields[i];
		struct helium_type *val_type;
		size_t j;
		int found = 0;

		for (j = 0; j < field_count; j++) {
			if (strcmp(fields[j]->name, init->name) == 0) {
				found = 1;
				if (seen[j]) {
					format_error(error,
						     "%d:%d: duplicate field '%s' in '%s'",
						     init->line, init->col,
						     init->name, name);
					free(seen);
					helium_type_free(inst);
					return -1;
				}
				seen[j] = 1;
				seen_count++;
				if (infer_expr(init->value, ctx, &val_type,
					       error) < 0) {
					free(seen);
					helium_type_free(inst);
					return -1;
				}
				if (helium_type_unify(val_type, field_types[j],
						      &ctx->subst,
						      error) < 0) {
					helium_type_free(val_type);
					free(seen);
					helium_type_free(inst);
					return -1;
				}
				helium_type_free(val_type);
				break;
			}
		}
		if (!found) {
			format_error(error,
				     "%d:%d: unknown field '%s' in '%s'",
				     init->line, init->col, init->name, name);
			free(seen);
			helium_type_free(inst);
			return -1;
		}
	}

	if (seen_count != field_count) {
		format_error(error,
			     "%d:%d: missing fields in constructor '%s'",
			     expr->line, expr->col, name);
		free(seen);
		helium_type_free(inst);
		return -1;
	}

	free(seen);
	ret = helium_type_copy(inst->ret);
	helium_type_free(inst);
	*out = ret;
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Expression inference                                                       */
/* -------------------------------------------------------------------------- */

static int infer_literal(struct helium_expr *expr, struct helium_type **out)
{
	struct helium_literal *lit = expr->u.lit;

	switch (lit->kind) {
	case HELIUM_LIT_INT:
		*out = make_i32_type();
		break;
	case HELIUM_LIT_FLOAT:
		*out = make_f64_type();
		break;
	case HELIUM_LIT_BOOL:
		*out = make_bool_type();
		break;
	case HELIUM_LIT_STRING:
		*out = make_str_type();
		break;
	case HELIUM_LIT_UNIT:
		*out = make_unit_type();
		break;
	default:
		*out = helium_type_fresh_var(expr->line, expr->col);
		break;
	}
	return 0;
}

static int infer_expr(struct helium_expr *expr, struct infer_ctx *ctx,
		      struct helium_type **out, char **error)
{
	struct helium_type *v;
	size_t i;

	if (!expr) {
		*out = make_unit_type();
		goto done;
	}

	switch (expr->kind) {
	case HELIUM_EXPR_LITERAL: {
		int rc = infer_literal(expr, out);

		if (rc < 0)
			return rc;
		goto done;
	}

	case HELIUM_EXPR_IDENT: {
		struct helium_scheme *scheme;
		struct helium_constructor_info *constructor;

		scheme = helium_env_lookup(ctx->env, expr->u.ident.name);
		if (scheme) {
			*out = helium_scheme_instantiate(scheme, ctx->subst);
			goto done;
		}

		constructor = helium_env_lookup_constructor(ctx->env,
							    expr->u.ident.name);
		if (constructor) {
			*out = helium_scheme_instantiate(&constructor->scheme,
							 ctx->subst);
			goto done;
		}

		format_error(error,
			     "%d:%d: unbound identifier '%s'",
			     expr->line, expr->col,
			     expr->u.ident.name);
		return -1;
	}

	case HELIUM_EXPR_BINARY: {
		struct helium_type *left;
		struct helium_type *right;

		if (infer_expr(expr->u.binary.left, ctx, &left, error) < 0)
			return -1;
		if (infer_expr(expr->u.binary.right, ctx, &right, error) < 0) {
			helium_type_free(left);
			return -1;
		}

		switch (expr->u.binary.op) {
		case PLUS:
		case MINUS:
		case STAR:
		case SLASH:
		case PERCENT:
			if (helium_type_unify(left, right, &ctx->subst,
					      error) < 0) {
				helium_type_free(left);
				helium_type_free(right);
				return -1;
			}
			*out = helium_type_apply(ctx->subst, left);
			helium_type_free(left);
			helium_type_free(right);
			goto done;

		case EQEQ:
		case NEQ:
		case LT:
		case LE:
		case GT:
		case GE:
			if (helium_type_unify(left, right, &ctx->subst,
					      error) < 0) {
				helium_type_free(left);
				helium_type_free(right);
				return -1;
			}
			*out = make_bool_type();
			helium_type_free(left);
			helium_type_free(right);
			goto done;

		case AND:
		case OR:
			v = make_bool_type();
			if (helium_type_unify(left, v, &ctx->subst, error) < 0 ||
			    helium_type_unify(right, v, &ctx->subst, error) < 0) {
				helium_type_free(left);
				helium_type_free(right);
				helium_type_free(v);
				return -1;
			}
			*out = v;
			helium_type_free(left);
			helium_type_free(right);
			goto done;

		case BIND: {
			struct helium_type *arg_var;
			struct helium_type *ret_var;
			struct helium_type *expected_left;
			struct helium_type *expected_right;

			arg_var = helium_type_fresh_var(expr->line, expr->col);
			ret_var = helium_type_fresh_var(expr->line, expr->col);
			expected_left = make_io_type(arg_var);
			expected_right = make_fn_type(&arg_var, 1,
						      make_io_type(ret_var));

			if (helium_type_unify(left, expected_left, &ctx->subst,
					      error) < 0) {
				helium_type_free(left);
				helium_type_free(right);
				helium_type_free(expected_left);
				helium_type_free(expected_right);
				helium_type_free(arg_var);
				helium_type_free(ret_var);
				return -1;
			}
			if (helium_type_unify(right, expected_right, &ctx->subst,
					      error) < 0) {
				helium_type_free(left);
				helium_type_free(right);
				helium_type_free(expected_left);
				helium_type_free(expected_right);
				helium_type_free(arg_var);
				helium_type_free(ret_var);
				return -1;
			}
			*out = helium_type_apply(ctx->subst, make_io_type(ret_var));
			helium_type_free(left);
			helium_type_free(right);
			helium_type_free(expected_left);
			helium_type_free(expected_right);
			helium_type_free(arg_var);
			helium_type_free(ret_var);
			goto done;
		}

		default:
			format_error(error,
				     "%d:%d: unknown binary operator",
				     expr->line, expr->col);
			helium_type_free(left);
			helium_type_free(right);
			return -1;
		}
	}

	case HELIUM_EXPR_UNARY: {
		struct helium_type *operand;

		if (infer_expr(expr->u.unary.operand, ctx, &operand, error) < 0)
			return -1;

		if (expr->u.unary.op == NOT) {
			v = make_bool_type();
			if (helium_type_unify(operand, v, &ctx->subst, error) < 0) {
				helium_type_free(operand);
				helium_type_free(v);
				return -1;
			}
			*out = v;
			helium_type_free(operand);
			goto done;
		}

		/* PLUS and MINUS preserve the operand type. */
		*out = helium_type_apply(ctx->subst, operand);
		helium_type_free(operand);
		goto done;
	}

	case HELIUM_EXPR_CALL: {
		struct helium_type *func;
		struct helium_type **arg_types;
		struct helium_type *ret_var;
		struct helium_type *expected;
		struct helium_type *applied_func;

		if (infer_expr(expr->u.call.func, ctx, &func, error) < 0)
			return -1;

		arg_types = malloc(expr->u.call.arg_count * sizeof(*arg_types));
		if (!arg_types)
			abort();
		for (i = 0; i < expr->u.call.arg_count; i++) {
			if (infer_expr(expr->u.call.args[i], ctx, &arg_types[i],
				       error) < 0) {
				while (i > 0)
					helium_type_free(arg_types[--i]);
				free(arg_types);
				helium_type_free(func);
				return -1;
			}
		}

		ret_var = helium_type_fresh_var(expr->line, expr->col);
		expected = make_fn_type(arg_types, expr->u.call.arg_count,
					ret_var);

		applied_func = helium_type_apply(ctx->subst, func);
		if (applied_func->kind == HELIUM_TYPE_FN &&
		    applied_func->param_count != expr->u.call.arg_count) {
			format_error(error,
				     "%d:%d: wrong number of arguments: expected %zu, got %zu",
				     expr->line, expr->col,
				     applied_func->param_count,
				     expr->u.call.arg_count);
			helium_type_free(applied_func);
			helium_type_free(expected);
			helium_type_free(ret_var);
			for (i = 0; i < expr->u.call.arg_count; i++)
				helium_type_free(arg_types[i]);
			free(arg_types);
			helium_type_free(func);
			return -1;
		}
		helium_type_free(applied_func);

		if (helium_type_unify(func, expected, &ctx->subst, error) < 0) {
			helium_type_free(expected);
			helium_type_free(ret_var);
			for (i = 0; i < expr->u.call.arg_count; i++)
				helium_type_free(arg_types[i]);
			free(arg_types);
			helium_type_free(func);
			return -1;
		}

		*out = helium_type_apply(ctx->subst, ret_var);
		helium_type_free(expected);
		helium_type_free(ret_var);
		for (i = 0; i < expr->u.call.arg_count; i++)
			helium_type_free(arg_types[i]);
		free(arg_types);
		helium_type_free(func);
		goto done;
	}

	case HELIUM_EXPR_BLOCK: {
		size_t snapshot = env_snapshot(ctx->env);
		struct helium_type *last = make_unit_type();
		int has_expr = 0;

		for (i = 0; i < expr->u.block.binding_count; i++) {
			struct helium_scheme *sch;

			if (infer_binding(expr->u.block.bindings[i], ctx, &sch,
					  error) < 0) {
				helium_type_free(last);
				env_restore(ctx->env, snapshot);
				return -1;
			}
			helium_env_add_binding(ctx->env,
					       expr->u.block.bindings[i]->name,
					       sch);
			helium_scheme_free(sch);
		}

		for (i = 0; i < expr->u.block.expr_count; i++) {
			struct helium_type *et;

			helium_type_free(last);
			if (infer_expr(expr->u.block.exprs[i], ctx, &et,
				       error) < 0) {
				env_restore(ctx->env, snapshot);
				return -1;
			}
			last = et;
			has_expr = 1;
		}

		env_restore(ctx->env, snapshot);
		if (!has_expr) {
			helium_type_free(last);
			*out = make_unit_type();
		} else {
			*out = last;
		}
		goto done;
	}

	case HELIUM_EXPR_IF: {
		struct helium_type *cond;
		struct helium_type *then_t;
		struct helium_type *else_t;

		if (infer_expr(expr->u.if_expr.cond, ctx, &cond, error) < 0)
			return -1;
		v = make_bool_type();
		if (helium_type_unify(cond, v, &ctx->subst, error) < 0) {
			helium_type_free(cond);
			helium_type_free(v);
			return -1;
		}
		helium_type_free(cond);
		helium_type_free(v);

		if (infer_expr(expr->u.if_expr.then_branch, ctx, &then_t,
			       error) < 0)
			return -1;
		if (infer_expr(expr->u.if_expr.else_branch, ctx, &else_t,
			       error) < 0) {
			helium_type_free(then_t);
			return -1;
		}
		if (helium_type_unify(then_t, else_t, &ctx->subst, error) < 0) {
			format_error(error,
				     "%d:%d: if branches have incompatible types",
				     expr->line, expr->col);
			helium_type_free(then_t);
			helium_type_free(else_t);
			return -1;
		}
		*out = helium_type_apply(ctx->subst, then_t);
		helium_type_free(then_t);
		helium_type_free(else_t);
		goto done;
	}

	case HELIUM_EXPR_MATCH: {
		struct helium_type *value;
		struct helium_type *value_applied;
		struct helium_type_def_info *adt_info;
		struct helium_type *result = NULL;
		int has_wildcard = 0;

		if (infer_expr(expr->u.match.value, ctx, &value, error) < 0)
			return -1;

		for (i = 0; i < expr->u.match.arm_count; i++) {
			struct helium_match_arm *arm = expr->u.match.arms[i];
			struct helium_type *pat_type;
			struct helium_type *arm_type;
			size_t snapshot = env_snapshot(ctx->env);

			if (arm->pattern->kind == HELIUM_PATTERN_WILD ||
			    arm->pattern->kind == HELIUM_PATTERN_IDENT)
				has_wildcard = 1;

			if (infer_pattern(arm->pattern, ctx, &pat_type,
					  error) < 0) {
				helium_type_free(value);
				if (result)
					helium_type_free(result);
				return -1;
			}
			if (helium_type_unify(pat_type, value, &ctx->subst,
					      error) < 0) {
				format_error(error,
					     "%d:%d: pattern type does not match match value",
					     arm->line, arm->col);
				helium_type_free(pat_type);
				helium_type_free(value);
				if (result)
					helium_type_free(result);
				env_restore(ctx->env, snapshot);
				return -1;
			}
			helium_type_free(pat_type);

			if (infer_expr(arm->expr, ctx, &arm_type, error) < 0) {
				helium_type_free(value);
				if (result)
					helium_type_free(result);
				env_restore(ctx->env, snapshot);
				return -1;
			}

			if (!result) {
				result = arm_type;
			} else {
				if (helium_type_unify(result, arm_type, &ctx->subst,
						      error) < 0) {
					format_error(error,
						     "%d:%d: match arm has incompatible type",
						     arm->line, arm->col);
					helium_type_free(arm_type);
					helium_type_free(value);
					helium_type_free(result);
					env_restore(ctx->env, snapshot);
					return -1;
				}
				helium_type_free(arm_type);
			}
			env_restore(ctx->env, snapshot);
		}

		if (!has_wildcard) {
			char **seen = NULL;
			size_t seen_count = 0;
			size_t seen_cap = 0;
			size_t j;

			for (j = 0; j < expr->u.match.arm_count; j++) {
				struct helium_pattern *pat =
					expr->u.match.arms[j]->pattern;

				if (pat->kind == HELIUM_PATTERN_CONSTRUCTOR)
					append_string(&seen, &seen_count,
						      &seen_cap,
						      xstrdup(pat->name));
			}

			value_applied = helium_type_apply(ctx->subst, value);
			adt_info = NULL;
			if (value_applied->kind == HELIUM_TYPE_NAMED)
				adt_info = helium_env_lookup_def(ctx->env,
								 value_applied->name);
			if (adt_info &&
			    adt_info->def->kind == HELIUM_TYPE_DEF_ADT) {
				for (j = 0; j < adt_info->def->u.adt.variant_count;
				     j++) {
					struct helium_variant *variant =
						adt_info->def->u.adt.variants[j];
					int found = 0;
					size_t k;

					for (k = 0; k < seen_count; k++) {
						if (strcmp(seen[k],
							   variant->name) == 0) {
							found = 1;
							break;
						}
					}
					if (!found) {
						format_error(error,
							     "%d:%d: missing variant '%s' in match",
							     expr->line, expr->col,
							     variant->name);
						helium_type_free(value_applied);
						helium_type_free(value);
						helium_type_free(result);
						for (k = 0; k < seen_count; k++)
							free(seen[k]);
						free(seen);
						return -1;
					}
				}
			}
			helium_type_free(value_applied);
			for (j = 0; j < seen_count; j++)
				free(seen[j]);
			free(seen);
		}

		helium_type_free(value);
		*out = result ? result : make_unit_type();
		goto done;
	}

	case HELIUM_EXPR_LOOP: {
		struct helium_type **loop_types;
		size_t loop_count = expr->u.loop.binding_count;
		size_t snapshot = env_snapshot(ctx->env);
		struct helium_type *body_type;
		struct helium_type **prev_loop;
		size_t prev_count;

		loop_types = malloc(loop_count * sizeof(*loop_types));
		if (!loop_types)
			abort();

		for (i = 0; i < loop_count; i++) {
			struct helium_loop_binding *lb = expr->u.loop.bindings[i];
			struct helium_type *init_type;

			if (infer_expr(lb->init, ctx, &init_type, error) < 0) {
				while (i > 0)
					helium_type_free(loop_types[--i]);
				free(loop_types);
				env_restore(ctx->env, snapshot);
				return -1;
			}

			if (lb->type) {
				struct helium_type *ann;

				if (resolve_type(lb->type, ctx->tscope, &ann,
						 error) < 0) {
					helium_type_free(init_type);
					while (i > 0)
						helium_type_free(loop_types[--i]);
					free(loop_types);
					env_restore(ctx->env, snapshot);
					return -1;
				}
				if (helium_type_unify(init_type, ann, &ctx->subst,
						      error) < 0) {
					helium_type_free(init_type);
					helium_type_free(ann);
					while (i > 0)
						helium_type_free(loop_types[--i]);
					free(loop_types);
					env_restore(ctx->env, snapshot);
					return -1;
				}
				helium_type_free(ann);
				loop_types[i] = helium_type_apply(ctx->subst, init_type);
			} else {
				loop_types[i] = helium_type_apply(ctx->subst, init_type);
			}
			helium_type_free(init_type);

			helium_env_add_binding(ctx->env, lb->name,
					       &(struct helium_scheme){
						.type = loop_types[i],
						.vars = NULL,
						.var_count = 0
					       });
		}

		prev_loop = ctx->loop_types;
		prev_count = ctx->loop_count;
		ctx->loop_types = loop_types;
		ctx->loop_count = loop_count;

		if (infer_expr(expr->u.loop.body, ctx, &body_type, error) < 0) {
			ctx->loop_types = prev_loop;
			ctx->loop_count = prev_count;
			for (i = 0; i < loop_count; i++)
				helium_type_free(loop_types[i]);
			free(loop_types);
			env_restore(ctx->env, snapshot);
			return -1;
		}

		ctx->loop_types = prev_loop;
		ctx->loop_count = prev_count;
		for (i = 0; i < loop_count; i++)
			helium_type_free(loop_types[i]);
		free(loop_types);
		env_restore(ctx->env, snapshot);
		*out = body_type;
		goto done;
	}

	case HELIUM_EXPR_RECUR: {
		struct helium_type *ret;

		if (ctx->loop_count == 0) {
			format_error(error,
				     "%d:%d: recur outside of a loop",
				     expr->line, expr->col);
			return -1;
		}
		if (expr->u.recur.arg_count != ctx->loop_count) {
			format_error(error,
				     "%d:%d: recur expects %zu arguments, got %zu",
				     expr->line, expr->col, ctx->loop_count,
				     expr->u.recur.arg_count);
			return -1;
		}
		for (i = 0; i < expr->u.recur.arg_count; i++) {
			struct helium_type *arg;

			if (infer_expr(expr->u.recur.args[i], ctx, &arg,
				       error) < 0)
				return -1;
			if (helium_type_unify(arg, ctx->loop_types[i], &ctx->subst,
					      error) < 0) {
				format_error(error,
					     "%d:%d: recur argument type mismatch",
					     expr->line, expr->col);
				helium_type_free(arg);
				return -1;
			}
			helium_type_free(arg);
		}
		ret = helium_type_fresh_var(expr->line, expr->col);
		*out = ret;
		goto done;
	}

	case HELIUM_EXPR_LAMBDA: {
		struct tscope *new_scope;
		struct helium_type **param_types;
		struct helium_type *body_type;
		struct helium_type *fn;
		size_t snapshot = env_snapshot(ctx->env);

		new_scope = tscope_push(ctx->tscope, expr->u.lambda.type_params,
					expr->u.lambda.type_param_count);
		ctx->tscope = new_scope;

		param_types = malloc(expr->u.lambda.param_count *
				     sizeof(*param_types));
		if (!param_types)
			abort();

		for (i = 0; i < expr->u.lambda.param_count; i++) {
			struct helium_param *param = expr->u.lambda.params[i];

			if (param->type) {
				if (resolve_type(param->type, ctx->tscope,
						 &param_types[i], error) < 0) {
					ctx->tscope = new_scope->parent;
					tscope_free(new_scope);
					free(param_types);
					env_restore(ctx->env, snapshot);
					return -1;
				}
			} else {
				param_types[i] = helium_type_fresh_var(param->line,
								       param->col);
			}
			helium_env_add_binding(ctx->env, param->name,
					       &(struct helium_scheme){
						.type = param_types[i],
						.vars = NULL,
						.var_count = 0
					       });
		}

		if (infer_expr(expr->u.lambda.body, ctx, &body_type, error) < 0) {
			ctx->tscope = new_scope->parent;
			tscope_free(new_scope);
			free(param_types);
			env_restore(ctx->env, snapshot);
			return -1;
		}

		if (expr->u.lambda.ret_type) {
			struct helium_type *ret;

			if (resolve_type(expr->u.lambda.ret_type, ctx->tscope,
					 &ret, error) < 0) {
				ctx->tscope = new_scope->parent;
				tscope_free(new_scope);
				helium_type_free(body_type);
				free(param_types);
				env_restore(ctx->env, snapshot);
				return -1;
			}
			if (helium_type_unify(body_type, ret, &ctx->subst, error) < 0) {
				ctx->tscope = new_scope->parent;
				tscope_free(new_scope);
				helium_type_free(body_type);
				helium_type_free(ret);
				free(param_types);
				env_restore(ctx->env, snapshot);
				return -1;
			}
			helium_type_free(ret);
		}

		fn = make_fn_type(param_types, expr->u.lambda.param_count,
				  body_type);
		helium_type_free(body_type);
		for (i = 0; i < expr->u.lambda.param_count; i++)
			helium_type_free(param_types[i]);
		free(param_types);

		ctx->tscope = new_scope->parent;
		tscope_free(new_scope);
		env_restore(ctx->env, snapshot);
		*out = fn;
		goto done;
	}

	case HELIUM_EXPR_RECORD_LIT: {
		int rc = check_record_literal(expr, ctx, out, error);

		if (rc < 0)
			return rc;
		goto done;
	}

	case HELIUM_EXPR_ARRAY_LIT: {
		struct helium_type *elem = NULL;

		for (i = 0; i < expr->u.array_lit.item_count; i++) {
			struct helium_type *item;

			if (infer_expr(expr->u.array_lit.items[i], ctx, &item,
				       error) < 0) {
				if (elem)
					helium_type_free(elem);
				return -1;
			}
			if (!elem) {
				elem = item;
			} else {
				if (helium_type_unify(elem, item, &ctx->subst,
						      error) < 0) {
					helium_type_free(item);
					helium_type_free(elem);
					return -1;
				}
				helium_type_free(item);
			}
		}
		if (!elem)
			elem = helium_type_fresh_var(expr->line, expr->col);
		*out = helium_type_array(helium_type_apply(ctx->subst, elem),
					 (long)expr->u.array_lit.item_count,
					 expr->line, expr->col);
		helium_type_free(elem);
		goto done;
	}

	case HELIUM_EXPR_FIELD: {
		struct helium_type *obj;
		struct helium_type_def_info *info;
		struct helium_type *obj_applied;

		if (infer_expr(expr->u.field.object, ctx, &obj, error) < 0)
			return -1;
		obj_applied = helium_type_apply(ctx->subst, obj);
		if (obj_applied->kind != HELIUM_TYPE_NAMED ||
		    !(info = helium_env_lookup_def(ctx->env, obj_applied->name))) {
			format_error(error,
				     "%d:%d: field access on non-record type",
				     expr->line, expr->col);
			helium_type_free(obj);
			helium_type_free(obj_applied);
			return -1;
		}
		if (info->def->kind != HELIUM_TYPE_DEF_RECORD) {
			format_error(error,
				     "%d:%d: field access on non-record type",
				     expr->line, expr->col);
			helium_type_free(obj);
			helium_type_free(obj_applied);
			return -1;
		}
		for (i = 0; i < info->def->u.record.field_count; i++) {
			struct helium_record_field *field =
				info->def->u.record.fields[i];
			struct helium_type *field_type;
			struct helium_type *field_subst;

			if (strcmp(field->name, expr->u.field.name) != 0)
				continue;
			field_type = subst_type_params(field->type,
						       info->def->params,
						       obj_applied->args,
						       obj_applied->arg_count);
			field_subst = helium_type_apply(ctx->subst, field_type);
			helium_type_free(field_type);
			*out = field_subst;
			helium_type_free(obj);
			helium_type_free(obj_applied);
			goto done;
		}
		format_error(error,
			     "%d:%d: unknown field '%s'",
			     expr->line, expr->col, expr->u.field.name);
		helium_type_free(obj);
		helium_type_free(obj_applied);
		return -1;
	}

	case HELIUM_EXPR_ANNOT: {
		struct helium_type *inner;
		struct helium_type *ann;

		if (resolve_type(expr->u.annot.type, ctx->tscope, &ann,
				 error) < 0)
			return -1;

		if (expr->u.annot.expr->kind == HELIUM_EXPR_LITERAL) {
			struct helium_literal *lit = expr->u.annot.expr->u.lit;

			if ((lit->kind == HELIUM_LIT_INT && type_is_integer(ann)) ||
			    (lit->kind == HELIUM_LIT_FLOAT && type_is_float(ann))) {
				*out = helium_type_apply(ctx->subst, ann);
				helium_type_free(ann);
				goto done;
			}
		}

		if (infer_expr(expr->u.annot.expr, ctx, &inner, error) < 0) {
			helium_type_free(ann);
			return -1;
		}
		if (helium_type_unify(inner, ann, &ctx->subst, error) < 0) {
			helium_type_free(inner);
			helium_type_free(ann);
			return -1;
		}
		helium_type_free(inner);
		*out = helium_type_apply(ctx->subst, ann);
		helium_type_free(ann);
		goto done;
	}

	case HELIUM_EXPR_BIND: {
		struct helium_type *left;
		struct helium_type *right;
		struct helium_type *arg_var;
		struct helium_type *ret_var;
		struct helium_type *expected_left;
		struct helium_type *expected_right;

		if (infer_expr(expr->u.bind.left, ctx, &left, error) < 0)
			return -1;
		if (infer_expr(expr->u.bind.right, ctx, &right, error) < 0) {
			helium_type_free(left);
			return -1;
		}

		arg_var = helium_type_fresh_var(expr->line, expr->col);
		ret_var = helium_type_fresh_var(expr->line, expr->col);
		expected_left = make_io_type(arg_var);
		expected_right = make_fn_type(&arg_var, 1,
					      make_io_type(ret_var));

		if (helium_type_unify(left, expected_left, &ctx->subst,
				      error) < 0) {
			helium_type_free(left);
			helium_type_free(right);
			helium_type_free(expected_left);
			helium_type_free(expected_right);
			helium_type_free(arg_var);
			helium_type_free(ret_var);
			return -1;
		}
		if (helium_type_unify(right, expected_right, &ctx->subst,
				      error) < 0) {
			helium_type_free(left);
			helium_type_free(right);
			helium_type_free(expected_left);
			helium_type_free(expected_right);
			helium_type_free(arg_var);
			helium_type_free(ret_var);
			return -1;
		}
		*out = helium_type_apply(ctx->subst, make_io_type(ret_var));
		helium_type_free(left);
		helium_type_free(right);
		helium_type_free(expected_left);
		helium_type_free(expected_right);
		helium_type_free(arg_var);
		helium_type_free(ret_var);
		goto done;
	}

	case HELIUM_EXPR_RETURN: {
		int rc = infer_expr(expr->u.ret.expr, ctx, out, error);

		if (rc < 0)
			return rc;
		goto done;
	}

	case HELIUM_EXPR_FSTRING:
		for (i = 0; i < expr->u.fstring.part_count; i++) {
			struct helium_fstring_part *part =
				expr->u.fstring.parts[i];
			struct helium_type *pt;

			if (!part->is_expr)
				continue;
			if (infer_expr(part->u.expr, ctx, &pt, error) < 0)
				return -1;
			helium_type_free(pt);
		}
		*out = make_str_type();
		goto done;

	default:
		format_error(error,
			     "%d:%d: unsupported expression kind %d",
			     expr->line, expr->col, expr->kind);
		return -1;
	}


	done:
		record_expr_type(expr, *out);
		return 0;
}


/* -------------------------------------------------------------------------- */
/* Pattern inference                                                          */
/* -------------------------------------------------------------------------- */

static int infer_pattern(struct helium_pattern *pat, struct infer_ctx *ctx,
			 struct helium_type **out, char **error)
{
	size_t i;

	if (!pat) {
		*out = helium_type_fresh_var(0, 0);
		return 0;
	}

	switch (pat->kind) {
	case HELIUM_PATTERN_WILD:
		*out = helium_type_fresh_var(pat->line, pat->col);
		return 0;

	case HELIUM_PATTERN_LITERAL: {
		struct helium_expr lit;

		memset(&lit, 0, sizeof(lit));
		lit.kind = HELIUM_EXPR_LITERAL;
		lit.line = pat->line;
		lit.col = pat->col;
		lit.u.lit = pat->lit;
		return infer_expr(&lit, ctx, out, error);
	}

	case HELIUM_PATTERN_IDENT:
		*out = helium_type_fresh_var(pat->line, pat->col);
		helium_env_add_binding(ctx->env, pat->name,
				       &(struct helium_scheme){
					.type = *out,
					.vars = NULL,
					.var_count = 0
				       });
		return 0;

	case HELIUM_PATTERN_CONSTRUCTOR: {
		struct helium_constructor_info *info;
		struct helium_type *inst;
		struct helium_type *adt_type;

		info = helium_env_lookup_constructor(ctx->env, pat->name);
		if (!info) {
			format_error(error,
				     "%d:%d: unknown constructor '%s'",
				     pat->line, pat->col, pat->name);
			return -1;
		}

		inst = helium_scheme_instantiate(&info->scheme, ctx->subst);
		if (pat->field_count == 0) {
			if (inst->kind == HELIUM_TYPE_FN) {
				format_error(error,
					     "%d:%d: constructor '%s' expects arguments",
					     pat->line, pat->col, pat->name);
				helium_type_free(inst);
				return -1;
			}
			*out = inst;
			return 0;
		}

		if (inst->kind != HELIUM_TYPE_FN) {
			format_error(error,
				     "%d:%d: constructor '%s' does not take record arguments",
				     pat->line, pat->col, pat->name);
			helium_type_free(inst);
			return -1;
		}

		if (inst->param_count < pat->field_count) {
			format_error(error,
				     "%d:%d: constructor '%s' expects at most %zu fields, got %zu",
				     pat->line, pat->col, pat->name,
				     inst->param_count, pat->field_count);
			helium_type_free(inst);
			return -1;
		}

		for (i = 0; i < pat->field_count; i++) {
			struct helium_type *field_type;
			struct helium_type *sub_pat_type;
			const char *field_name;
			size_t j;
			int found = 0;

			field_name = pat->field_names[i];
			if (info->variant) {
				for (j = 0; j < info->variant->field_count; j++) {
					if (strcmp(info->variant->fields[j]->name,
						   field_name) == 0) {
						field_type = inst->params[j];
						found = 1;
						break;
					}
				}
			} else if (info->adt &&
				   info->adt->kind == HELIUM_TYPE_DEF_RECORD) {
				for (j = 0; j < info->adt->u.record.field_count; j++) {
					if (strcmp(info->adt->u.record.fields[j]->name,
						   field_name) == 0) {
						field_type = inst->params[j];
						found = 1;
						break;
					}
				}
			}

			if (!found) {
				format_error(error,
					     "%d:%d: unknown field '%s' in '%s'",
					     pat->line, pat->col, field_name,
					     pat->name);
				helium_type_free(inst);
				return -1;
			}

			if (infer_pattern(pat->fields[i], ctx, &sub_pat_type,
					  error) < 0) {
				helium_type_free(inst);
				return -1;
			}
			if (helium_type_unify(sub_pat_type, field_type,
					      &ctx->subst, error) < 0) {
				format_error(error,
					     "%d:%d: field '%s' pattern type mismatch",
					     pat->line, pat->col, field_name);
				helium_type_free(sub_pat_type);
				helium_type_free(inst);
				return -1;
			}
			helium_type_free(sub_pat_type);
		}

		adt_type = helium_type_copy(inst->ret);
		helium_type_free(inst);
		*out = adt_type;
		return 0;
	}

	default:
		format_error(error,
			     "%d:%d: unsupported pattern kind %d",
			     pat->line, pat->col, pat->kind);
		return -1;
	}
}

/* -------------------------------------------------------------------------- */
/* Binding inference                                                          */
/* -------------------------------------------------------------------------- */

static int infer_binding(struct helium_binding *binding, struct infer_ctx *ctx,
			 struct helium_scheme **out, char **error)
{
	struct helium_type *value_type;
	struct helium_type *ann = NULL;

	if (binding->type && binding->value->kind == HELIUM_EXPR_LITERAL) {
		struct helium_literal *lit = binding->value->u.lit;

		if (resolve_type(binding->type, ctx->tscope, &ann, error) < 0)
			return -1;
		if ((lit->kind == HELIUM_LIT_INT && type_is_integer(ann)) ||
		    (lit->kind == HELIUM_LIT_FLOAT && type_is_float(ann))) {
			/* Numeric literals take their explicit annotation type. */
			*out = helium_scheme_generalize(ann, ctx->env, ctx->subst);
			helium_type_free(ann);
			return 0;
		}
		helium_type_free(ann);
		ann = NULL;
	}

	if (infer_expr(binding->value, ctx, &value_type, error) < 0)
		return -1;

	if (binding->type) {
		if (resolve_type(binding->type, ctx->tscope, &ann, error) < 0) {
			helium_type_free(value_type);
			return -1;
		}
		if (helium_type_unify(value_type, ann, &ctx->subst, error) < 0) {
			helium_type_free(value_type);
			helium_type_free(ann);
			return -1;
		}
		helium_type_free(ann);
	}

	*out = helium_scheme_generalize(value_type, ctx->env, ctx->subst);
	helium_type_free(value_type);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Module checking                                                            */
/* -------------------------------------------------------------------------- */

static int check_main(struct helium_env *env, struct helium_subst **subst,
		      char **error)
{
	struct helium_scheme *scheme;
	struct helium_type *main_type;
	struct helium_type *expected;
	int rc;

	scheme = helium_env_lookup(env, "main");
	if (!scheme) {
		format_error(error, "module has no 'main' binding");
		return -1;
	}

	main_type = helium_scheme_instantiate(scheme, *subst);
	expected = make_io_type(make_unit_type());

	/* Allow either IO<()> or a nullary function returning IO<()>. */
	if (main_type->kind == HELIUM_TYPE_FN &&
	    main_type->param_count == 0 &&
	    main_type->ret &&
	    helium_type_unify(main_type->ret, expected, subst, error) == 0) {
		rc = 0;
	} else {
		rc = helium_type_unify(main_type, expected, subst, error);
	}

	helium_type_free(main_type);
	helium_type_free(expected);
	return rc;
}

static int scheme_has_escaping_vars(struct helium_scheme *scheme,
				    char **error)
{
	char **fv = NULL;
	size_t fv_count = 0;
	size_t i;

	helium_type_free_vars(scheme->type, &fv, &fv_count);
	for (i = 0; i < fv_count; i++) {
		size_t j;
		int bound = 0;

		for (j = 0; j < scheme->var_count; j++) {
			if (strcmp(fv[i], scheme->vars[j]) == 0) {
				bound = 1;
				break;
			}
		}
		if (!bound) {
			format_error(error,
				     "type variable '%s' escapes its scope",
				     fv[i]);
			for (j = 0; j < fv_count; j++)
				free(fv[j]);
			free(fv);
			return 1;
		}
	}
	for (i = 0; i < fv_count; i++)
		free(fv[i]);
	free(fv);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Apply final substitution to inferred types stored on the AST               */
/* -------------------------------------------------------------------------- */

static void apply_subst_to_expr(struct helium_expr *expr,
				struct helium_subst *subst);

static void apply_subst_to_exprs(struct helium_expr **exprs, size_t count,
				 struct helium_subst *subst)
{
	size_t i;

	for (i = 0; i < count; i++)
		apply_subst_to_expr(exprs[i], subst);
}

static void apply_subst_to_type(struct helium_type **type,
				struct helium_subst *subst)
{
	struct helium_type *applied;

	if (!type || !*type)
		return;
	applied = helium_type_apply(subst, *type);
	helium_type_free(*type);
	*type = applied;
}

static void apply_subst_to_pattern(struct helium_pattern *pat,
				   struct helium_subst *subst)
{
	size_t i;

	if (!pat)
		return;
	for (i = 0; i < pat->field_count; i++)
		apply_subst_to_pattern(pat->fields[i], subst);
}

static void apply_subst_to_expr(struct helium_expr *expr,
				struct helium_subst *subst)
{
	size_t i;

	if (!expr)
		return;

	apply_subst_to_type(&expr->inferred_type, subst);

	switch (expr->kind) {
	case HELIUM_EXPR_LITERAL:
		break;
	case HELIUM_EXPR_IDENT:
		break;
	case HELIUM_EXPR_BINARY:
		apply_subst_to_expr(expr->u.binary.left, subst);
		apply_subst_to_expr(expr->u.binary.right, subst);
		break;
	case HELIUM_EXPR_UNARY:
		apply_subst_to_expr(expr->u.unary.operand, subst);
		break;
	case HELIUM_EXPR_CALL:
		apply_subst_to_expr(expr->u.call.func, subst);
		apply_subst_to_exprs(expr->u.call.args, expr->u.call.arg_count,
				     subst);
		break;
	case HELIUM_EXPR_BLOCK: {
		for (i = 0; i < expr->u.block.binding_count; i++) {
			struct helium_binding *b = expr->u.block.bindings[i];

			apply_subst_to_type(&b->type, subst);
			apply_subst_to_expr(b->value, subst);
		}
		apply_subst_to_exprs(expr->u.block.exprs, expr->u.block.expr_count,
				     subst);
		break;
	}
	case HELIUM_EXPR_IF:
		apply_subst_to_expr(expr->u.if_expr.cond, subst);
		apply_subst_to_expr(expr->u.if_expr.then_branch, subst);
		apply_subst_to_expr(expr->u.if_expr.else_branch, subst);
		break;
	case HELIUM_EXPR_MATCH:
		apply_subst_to_expr(expr->u.match.value, subst);
		for (i = 0; i < expr->u.match.arm_count; i++) {
			apply_subst_to_pattern(expr->u.match.arms[i]->pattern,
					       subst);
			apply_subst_to_expr(expr->u.match.arms[i]->expr, subst);
		}
		break;
	case HELIUM_EXPR_LOOP:
		for (i = 0; i < expr->u.loop.binding_count; i++) {
			struct helium_loop_binding *lb = expr->u.loop.bindings[i];

			apply_subst_to_type(&lb->type, subst);
			apply_subst_to_expr(lb->init, subst);
		}
		apply_subst_to_expr(expr->u.loop.body, subst);
		break;
	case HELIUM_EXPR_RECUR:
		apply_subst_to_exprs(expr->u.recur.args, expr->u.recur.arg_count,
				     subst);
		break;
	case HELIUM_EXPR_LAMBDA: {
		for (i = 0; i < expr->u.lambda.param_count; i++)
			apply_subst_to_type(&expr->u.lambda.params[i]->type,
					    subst);
		apply_subst_to_type(&expr->u.lambda.ret_type, subst);
		apply_subst_to_expr(expr->u.lambda.body, subst);
		break;
	}
	case HELIUM_EXPR_RECORD_LIT:
		for (i = 0; i < expr->u.record_lit.field_count; i++)
			apply_subst_to_expr(expr->u.record_lit.fields[i]->value,
					    subst);
		break;
	case HELIUM_EXPR_ARRAY_LIT:
		apply_subst_to_exprs(expr->u.array_lit.items,
				     expr->u.array_lit.item_count, subst);
		break;
	case HELIUM_EXPR_FIELD:
		apply_subst_to_expr(expr->u.field.object, subst);
		break;
	case HELIUM_EXPR_ANNOT:
		apply_subst_to_type(&expr->u.annot.type, subst);
		apply_subst_to_expr(expr->u.annot.expr, subst);
		break;
	case HELIUM_EXPR_BIND:
		apply_subst_to_expr(expr->u.bind.left, subst);
		apply_subst_to_expr(expr->u.bind.right, subst);
		break;
	case HELIUM_EXPR_RETURN:
		apply_subst_to_expr(expr->u.ret.expr, subst);
		break;
	case HELIUM_EXPR_FSTRING:
		for (i = 0; i < expr->u.fstring.part_count; i++) {
			struct helium_fstring_part *part =
				expr->u.fstring.parts[i];

			if (part->is_expr)
				apply_subst_to_expr(part->u.expr, subst);
		}
		break;
	}
}

static void apply_subst_to_binding(struct helium_binding *binding,
				   struct helium_subst *subst)
{
	if (!binding)
		return;
	apply_subst_to_type(&binding->type, subst);
	apply_subst_to_expr(binding->value, subst);
}

static void apply_subst_to_decl(struct helium_top_decl *decl,
				struct helium_subst *subst)
{
	if (!decl)
		return;
	switch (decl->kind) {
	case HELIUM_DECL_BINDING:
		apply_subst_to_binding(decl->u.binding, subst);
		break;
	case HELIUM_DECL_TYPE:
	case HELIUM_DECL_IMPORT:
	case HELIUM_DECL_FOREIGN:
		break;
	}
}

static void apply_subst_to_module(struct helium_module *module,
				  struct helium_subst *subst)
{
	size_t i;

	if (!module)
		return;
	for (i = 0; i < module->decl_count; i++)
		apply_subst_to_decl(module->decls[i], subst);
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

void helium_typed_module_free(struct helium_typed_module *typed)
{
	if (!typed)
		return;
	helium_env_free(typed->env);
	helium_subst_free(typed->subst);
	free(typed);
}

int helium_infer_module(struct helium_module *module,
			struct helium_typed_module **out, char **error)
{
	struct helium_typed_module *typed = NULL;
	struct helium_env *env;
	struct infer_ctx ctx;
	struct tscope *scope;
	size_t i;
	int rc = 0;

	if (!module) {
		format_error(error, "internal error: null module");
		return -1;
	}

	if (out)
		*out = NULL;

	env = helium_env_new();
	scope = tscope_push(NULL, NULL, 0);
	memset(&ctx, 0, sizeof(ctx));
	ctx.env = env;
	ctx.subst = helium_subst_new();
	ctx.tscope = scope;

	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];

		switch (decl->kind) {
		case HELIUM_DECL_IMPORT:
			/* Imports are not resolved during type checking. */
			break;
		case HELIUM_DECL_TYPE:
			if (register_type_def(env, decl->u.type_def, error) < 0) {
				rc = -1;
				goto out;
			}
			break;
		case HELIUM_DECL_BINDING: {
			struct helium_binding *binding = decl->u.binding;
			struct helium_scheme *placeholder;
			struct helium_scheme *sch;
			struct helium_type *fresh;

			fresh = helium_type_fresh_var(binding->line, binding->col);
			placeholder = helium_scheme_new(fresh, NULL, 0);
			helium_env_add_binding(env, binding->name, placeholder);
			helium_scheme_free(placeholder);

			if (infer_binding(binding, &ctx, &sch, error) < 0) {
				rc = -1;
				goto out;
			}

			if (scheme_has_escaping_vars(sch, error)) {
				helium_scheme_free(sch);
				rc = -1;
				goto out;
			}

			placeholder = helium_env_lookup(env, binding->name);
			if (helium_type_unify(placeholder->type, sch->type,
					      &ctx.subst, error) < 0) {
				helium_scheme_free(sch);
				rc = -1;
				goto out;
			}
			helium_env_update_binding(env, binding->name, sch);
			helium_scheme_free(sch);
			break;
		}
		case HELIUM_DECL_FOREIGN: {
			struct helium_type *resolved;
			struct helium_scheme *sch;

			if (resolve_type(decl->u.foreign.type, scope, &resolved,
					 error) < 0) {
				rc = -1;
				goto out;
			}
			sch = helium_scheme_new(resolved, NULL, 0);
			helium_env_add_binding(env, decl->u.foreign.name, sch);
			helium_scheme_free(sch);
			break;
		}
		default:
			format_error(error,
				     "%d:%d: unknown declaration kind",
				     decl->line, decl->col);
			rc = -1;
			goto out;
		}
	}

	if (check_main(env, &ctx.subst, error) < 0) {
		rc = -1;
		goto out;
	}

	apply_subst_to_module(module, ctx.subst);

	if (out) {
		typed = xalloc(sizeof(*typed));
		typed->module = module;
		typed->env = env;
		typed->subst = ctx.subst;
		ctx.subst = NULL;
		env = NULL;
		*out = typed;
	}

out:
	tscope_free(scope);
	helium_env_free(env);
	helium_subst_free(ctx.subst);
	return rc;
}

int helium_check_module(struct helium_module *module, char **error)
{
	struct helium_typed_module *typed = NULL;
	int rc;

	rc = helium_infer_module(module, &typed, error);
	helium_typed_module_free(typed);
	return rc;
}
