/* SPDX-License-Identifier: TBD */
/*
 * mono.c - Monomorphization: typed AST -> monomorphic IR.
 */

#include "mono.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "type_env.h"

struct mono_ctx {
	struct helium_typed_module *typed;
	struct helium_ir_program *prog;
	struct {
		const char *generic_name;
		struct helium_type **type_args;
		size_t type_arg_count;
		const char *spec_name;
	} current;
	char **locals;
	size_t local_count;
	size_t local_capacity;
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

static struct helium_literal *copy_literal(struct helium_literal *lit)
{
	if (!lit)
		return NULL;
	return helium_literal(lit->kind, lit->text, lit->line, lit->col);
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
		*error = strdup("monomorphization error");
		return;
	}
	*error = msg;
}

/* -------------------------------------------------------------------------- */
/* Name mangling                                                              */
/* -------------------------------------------------------------------------- */

static char *type_to_mangled_name(struct helium_type *type)
{
	char *raw;
	char *out;
	size_t i;
	size_t len;

	raw = NULL;
	if (type) {
		helium_type_to_string(type, NULL, 0);
		/* helium_type_to_string with NULL returns needed size indirectly; */
		/* use a large buffer instead. */
		{
			char buf[256];

			helium_type_to_string(type, buf, sizeof(buf));
			raw = xstrdup(buf);
		}
	} else {
		raw = xstrdup("void");
	}

	len = strlen(raw);
	out = xalloc(len + 1);
	for (i = 0; i < len; i++) {
		char c = raw[i];

		if (isalnum((unsigned char)c))
			out[i] = c;
		else
			out[i] = '_';
	}
	out[len] = '\0';
	free(raw);
	return out;
}

static char *specialized_name(const char *base, struct helium_type **args,
			      size_t count)
{
	char *name;
	size_t i;
	size_t len;

	len = strlen(base) + 1;
	for (i = 0; i < count; i++) {
		char *m = type_to_mangled_name(args[i]);

		len += strlen(m) + 1;
		free(m);
	}

	name = xalloc(len + 1);
	strcpy(name, base);
	for (i = 0; i < count; i++) {
		char *m = type_to_mangled_name(args[i]);

		strcat(name, "_");
		strcat(name, m);
		free(m);
	}
	return name;
}

/* -------------------------------------------------------------------------- */
/* Type substitution                                                          */
/* -------------------------------------------------------------------------- */

static int name_in_list(const char *name, char **names, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (strcmp(names[i], name) == 0)
			return (int)i;
	}
	return -1;
}

static struct helium_type *subst_type(struct helium_type *type,
				      char **names, struct helium_type **args,
				      size_t count)
{
	struct helium_type *copy;
	size_t i;
	int idx;

	if (!type)
		return NULL;

	if (type->kind == HELIUM_TYPE_VAR) {
		idx = name_in_list(type->name, names, count);
		if (idx >= 0)
			return helium_type_copy(args[idx]);
		return helium_type_copy(type);
	}

	if (type->kind == HELIUM_TYPE_NAMED) {
		idx = name_in_list(type->name, names, count);
		if (idx >= 0) {
			struct helium_type *substituted = helium_type_copy(args[idx]);

			for (i = 0; i < type->arg_count; i++) {
				helium_type_add_arg(substituted,
						    subst_type(type->args[i],
							       names,
							       args,
							       count));
			}
			return substituted;
		}
	}

	copy = xalloc(sizeof(*copy));
	copy->kind = type->kind;
	copy->line = type->line;
	copy->col = type->col;
	copy->name = xstrdup(type->name);
	copy->array_size = type->array_size;

	for (i = 0; i < type->arg_count; i++)
		helium_type_add_arg(copy, subst_type(type->args[i], names, args,
					     count));
	for (i = 0; i < type->param_count; i++)
		helium_type_add_param(copy, subst_type(type->params[i], names, args,
					       count));
	copy->ret = subst_type(type->ret, names, args, count);
	copy->elem_type = subst_type(type->elem_type, names, args, count);
	return copy;
}

/* Return the lambda's annotated return type if present, otherwise the return
 * type inferred for the lambda expression.  This matters because a lambda with
 * no `: <type>` annotation leaves u.lambda.ret_type NULL even though inference
 * has computed the correct function type.
 */
static struct helium_type *lambda_ret_type(struct helium_expr *lambda)
{
	if (lambda->u.lambda.ret_type)
		return lambda->u.lambda.ret_type;
	if (lambda->inferred_type &&
	    lambda->inferred_type->kind == HELIUM_TYPE_FN)
		return lambda->inferred_type->ret;
	return NULL;
}

/* Apply the current monomorphization substitution followed by the inference
 * substitution to obtain a concrete type for a call argument or return value.
 */
static struct helium_type *resolve_type(struct mono_ctx *ctx,
					struct helium_type *type,
					char **names,
					struct helium_type **args,
					size_t count)
{
	struct helium_type *mono;
	struct helium_type *full;

	if (!type)
		return NULL;

	mono = subst_type(type, names, args, count);
	full = helium_type_apply(ctx->typed->subst, mono);
	helium_type_free(mono);
	return full;
}

/* -------------------------------------------------------------------------- */
/* Type argument extraction                                                   */
/* -------------------------------------------------------------------------- */

static void subst_type_args(struct helium_type **type_args,
			    size_t type_arg_count,
			    char **names,
			    struct helium_type **args,
			    size_t name_count)
{
	size_t i;

	for (i = 0; i < type_arg_count; i++) {
		struct helium_type *substituted;

		substituted = subst_type(type_args[i], names, args, name_count);
		helium_type_free(type_args[i]);
		type_args[i] = substituted;
	}
}

static int match_type_args(struct helium_type *template,
			   struct helium_type *concrete,
			   char **names, size_t count,
			   struct helium_type **args,
			   char **error)
{
	size_t i;
	int idx;

	if (!template || !concrete) {
		format_error(error, "internal error: null type in match");
		return -1;
	}

	if (template->kind == HELIUM_TYPE_VAR) {
		idx = name_in_list(template->name, names, count);
		if (idx < 0) {
			format_error(error,
				     "unbound type variable '%s' in generic template",
				     template->name);
			return -1;
		}
		args[idx] = helium_type_copy(concrete);
		return 0;
	}

	if (template->kind != concrete->kind) {
		format_error(error, "type shape mismatch in generic instantiation");
		return -1;
	}

	if (template->kind == HELIUM_TYPE_NAMED) {
		if (!template->name || !concrete->name ||
		    strcmp(template->name, concrete->name) != 0 ||
		    template->arg_count != concrete->arg_count) {
			format_error(error, "named type mismatch in generic instantiation");
			return -1;
		}
		for (i = 0; i < template->arg_count; i++) {
			if (match_type_args(template->args[i], concrete->args[i],
					    names, count, args, error) < 0)
				return -1;
		}
		return 0;
	}

	if (template->kind == HELIUM_TYPE_FN) {
		if (template->param_count != concrete->param_count) {
			format_error(error,
				     "function arity mismatch in generic instantiation");
			return -1;
		}
		for (i = 0; i < template->param_count; i++) {
			if (match_type_args(template->params[i], concrete->params[i],
					    names, count, args, error) < 0)
				return -1;
		}
		if (match_type_args(template->ret, concrete->ret, names, count,
				    args, error) < 0)
			return -1;
		return 0;
	}

	if (template->kind == HELIUM_TYPE_ARRAY) {
		if (template->array_size != concrete->array_size) {
			format_error(error,
				     "array size mismatch in generic instantiation");
			return -1;
		}
		if (match_type_args(template->elem_type, concrete->elem_type,
				    names, count, args, error) < 0)
			return -1;
		return 0;
	}

	format_error(error, "unsupported type kind in generic instantiation");
	return -1;
}

/* -------------------------------------------------------------------------- */
/* Lookup helpers                                                             */
/* -------------------------------------------------------------------------- */

static struct helium_ir_function *find_function(struct mono_ctx *ctx,
						const char *name)
{
	size_t i;

	for (i = 0; i < ctx->prog->function_count; i++) {
		if (strcmp(ctx->prog->functions[i]->name, name) == 0)
			return ctx->prog->functions[i];
	}
	return NULL;
}

static struct helium_ir_type *find_type(struct mono_ctx *ctx, const char *name)
{
	size_t i;

	for (i = 0; i < ctx->prog->type_count; i++) {
		if (strcmp(ctx->prog->types[i]->name, name) == 0)
			return ctx->prog->types[i];
	}
	return NULL;
}

static struct helium_binding *find_top_level_binding(struct mono_ctx *ctx,
						     const char *name)
{
	size_t i;

	for (i = 0; i < ctx->typed->module->decl_count; i++) {
		struct helium_top_decl *decl = ctx->typed->module->decls[i];

		if (decl->kind == HELIUM_DECL_BINDING &&
		    strcmp(decl->u.binding->name, name) == 0)
			return decl->u.binding;
	}
	return NULL;
}

static int name_in_locals(struct mono_ctx *ctx, const char *name)
{
	size_t i;

	for (i = 0; i < ctx->local_count; i++) {
		if (strcmp(ctx->locals[i], name) == 0)
			return 1;
	}
	return 0;
}

static void push_local(struct mono_ctx *ctx, const char *name)
{
	if (ctx->local_count == ctx->local_capacity) {
		size_t newcap = ctx->local_capacity ? ctx->local_capacity * 2 : 16;
		char **tmp = realloc(ctx->locals, newcap * sizeof(*tmp));

		if (!tmp)
			abort();
		ctx->locals = tmp;
		ctx->local_capacity = newcap;
	}
	ctx->locals[ctx->local_count++] = xstrdup(name);
}

static size_t mark_locals(struct mono_ctx *ctx)
{
	return ctx->local_count;
}

static void pop_locals_to(struct mono_ctx *ctx, size_t mark)
{
	while (ctx->local_count > mark)
		free(ctx->locals[--ctx->local_count]);
}

/* -------------------------------------------------------------------------- */
/* Generic type specialization                                                */
/* -------------------------------------------------------------------------- */

static char *request_generic_type(struct mono_ctx *ctx,
				  struct helium_type_def_info *info,
				  struct helium_type **args,
				  size_t count, char **error)
{
	struct helium_ir_type *ir_type;

	(void)error;
	struct helium_type_def *def;
	char *name;
	size_t i;

	struct helium_ir_type *existing;

	def = info->def;
	name = specialized_name(def->name, args, count);
	existing = find_type(ctx, name);
	if (existing) {
		char *copy = xstrdup(existing->name);

		free(name);
		return copy;
	}

	ir_type = helium_ir_type_new(name,
				     def->kind == HELIUM_TYPE_DEF_RECORD ?
				     HELIUM_IR_TYPE_RECORD : HELIUM_IR_TYPE_ADT,
				     def->line, def->col);

	if (def->kind == HELIUM_TYPE_DEF_RECORD) {
		for (i = 0; i < def->u.record.field_count; i++) {
			struct helium_record_field *f = def->u.record.fields[i];
			struct helium_type *ft;
			struct helium_ir_field *irf;

			ft = subst_type(f->type, def->params, args, count);
			irf = helium_ir_field_new(f->name, ft, f->line, f->col);
			helium_ir_type_add_field(ir_type, irf);
		}
	} else {
		for (i = 0; i < def->u.adt.variant_count; i++) {
			struct helium_variant *v = def->u.adt.variants[i];
			struct helium_ir_variant *irv;
			size_t j;

			irv = helium_ir_variant_new(v->name, v->line, v->col);
			for (j = 0; j < v->field_count; j++) {
				struct helium_record_field *f = v->fields[j];
				struct helium_type *ft;
				struct helium_ir_field *irf;

				ft = subst_type(f->type, def->params, args,
						count);
				irf = helium_ir_field_new(f->name, ft,
							  f->line, f->col);
				helium_ir_variant_add_field(irv, irf);
			}
			helium_ir_type_add_variant(ir_type, irv);
		}
	}

	helium_ir_program_add_type(ctx->prog, ir_type);
	return name;
}

static char *request_type_by_name(struct mono_ctx *ctx,
				  struct helium_type *type,
				  char **error)
{
	struct helium_type_def_info *info;

	if (!type || type->kind != HELIUM_TYPE_NAMED || !type->name)
		return NULL;

	info = helium_env_lookup_def(ctx->typed->env, type->name);
	if (!info || info->def->param_count == 0)
		return xstrdup(type->name);

	return request_generic_type(ctx, info, type->args, type->arg_count,
				    error);
}

/* -------------------------------------------------------------------------- */
/* Forward declarations                                                       */
/* -------------------------------------------------------------------------- */

static struct helium_ir_instr *translate_expr(struct mono_ctx *ctx,
					      struct helium_expr *expr,
					      char **names,
					      struct helium_type **args,
					      size_t count,
					      char **error);

static char *request_function(struct mono_ctx *ctx,
			      const char *generic_name,
			      struct helium_type **type_args,
			      size_t type_arg_count,
			      char **error);

/* -------------------------------------------------------------------------- */
/* Free-variable analysis for closures                                        */
/* -------------------------------------------------------------------------- */

static int name_in_string_list(const char *name, char **names, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (strcmp(names[i], name) == 0)
			return 1;
	}
	return 0;
}

static void collect_captures(struct mono_ctx *ctx,
			     struct helium_expr *expr,
			     char **bound, size_t bound_count,
			     char **captures,
			     struct helium_type **capture_types,
			     size_t *capture_count,
			     size_t capture_capacity)
{
	size_t i;
	char **inner_bound;
	size_t inner_count;
	size_t inner_cap;

	if (!expr)
		return;

	switch (expr->kind) {
	case HELIUM_EXPR_IDENT: {
		const char *name = expr->u.ident.name;

		if (name_in_string_list(name, bound, bound_count))
			break;
		if (find_top_level_binding(ctx, name))
			break;
		if (helium_env_lookup_constructor(ctx->typed->env, name))
			break;
		if (!name_in_locals(ctx, name))
			break;
		if (name_in_string_list(name, captures, *capture_count))
			break;
		if (*capture_count < capture_capacity) {
			captures[(*capture_count)] = xstrdup(name);
			capture_types[(*capture_count)] =
				helium_type_copy(expr->inferred_type);
			(*capture_count)++;
		}
		break;
	}

	case HELIUM_EXPR_LAMBDA: {
		inner_cap = bound_count + expr->u.lambda.param_count;
		inner_bound = xalloc(inner_cap * sizeof(*inner_bound));
		for (i = 0; i < bound_count; i++)
			inner_bound[i] = bound[i];
		inner_count = bound_count;
		for (i = 0; i < expr->u.lambda.param_count; i++)
			inner_bound[inner_count++] = expr->u.lambda.params[i]->name;
		collect_captures(ctx, expr->u.lambda.body, inner_bound, inner_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		free(inner_bound);
		break;
	}

	case HELIUM_EXPR_BLOCK: {
		inner_cap = bound_count + expr->u.block.binding_count;
		inner_bound = xalloc(inner_cap * sizeof(*inner_bound));
		for (i = 0; i < bound_count; i++)
			inner_bound[i] = bound[i];
		inner_count = bound_count;
		for (i = 0; i < expr->u.block.binding_count; i++) {
			collect_captures(ctx, expr->u.block.bindings[i]->value,
					 inner_bound, inner_count,
					 captures, capture_types, capture_count,
					 capture_capacity);
			inner_bound[inner_count++] =
				expr->u.block.bindings[i]->name;
		}
		for (i = 0; i < expr->u.block.expr_count; i++)
			collect_captures(ctx, expr->u.block.exprs[i],
					 inner_bound, inner_count,
					 captures, capture_types, capture_count,
					 capture_capacity);
		free(inner_bound);
		break;
	}

	case HELIUM_EXPR_BINARY:
		collect_captures(ctx, expr->u.binary.left, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		collect_captures(ctx, expr->u.binary.right, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		break;

	case HELIUM_EXPR_UNARY:
		collect_captures(ctx, expr->u.unary.operand, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		break;

	case HELIUM_EXPR_CALL:
		collect_captures(ctx, expr->u.call.func, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		for (i = 0; i < expr->u.call.arg_count; i++)
			collect_captures(ctx, expr->u.call.args[i],
					 bound, bound_count,
					 captures, capture_types, capture_count,
					 capture_capacity);
		break;

	case HELIUM_EXPR_IF:
		collect_captures(ctx, expr->u.if_expr.cond, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		collect_captures(ctx, expr->u.if_expr.then_branch, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		collect_captures(ctx, expr->u.if_expr.else_branch, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		break;

	case HELIUM_EXPR_RECORD_LIT:
		for (i = 0; i < expr->u.record_lit.field_count; i++)
			collect_captures(ctx, expr->u.record_lit.fields[i]->value,
					 bound, bound_count,
					 captures, capture_types, capture_count,
					 capture_capacity);
		break;

	case HELIUM_EXPR_ARRAY_LIT:
		for (i = 0; i < expr->u.array_lit.item_count; i++)
			collect_captures(ctx, expr->u.array_lit.items[i],
					 bound, bound_count,
					 captures, capture_types, capture_count,
					 capture_capacity);
		break;

	case HELIUM_EXPR_FIELD:
		collect_captures(ctx, expr->u.field.object, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		break;

	case HELIUM_EXPR_RETURN:
		collect_captures(ctx, expr->u.ret.expr, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		break;

	case HELIUM_EXPR_ANNOT:
		collect_captures(ctx, expr->u.annot.expr, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		break;

	case HELIUM_EXPR_BIND:
		collect_captures(ctx, expr->u.bind.left, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		collect_captures(ctx, expr->u.bind.right, bound, bound_count,
				 captures, capture_types, capture_count,
				 capture_capacity);
		break;

	case HELIUM_EXPR_FSTRING:
		for (i = 0; i < expr->u.fstring.part_count; i++) {
			struct helium_fstring_part *p = expr->u.fstring.parts[i];

			if (p->is_expr)
				collect_captures(ctx, p->u.expr, bound, bound_count,
						 captures, capture_types,
						 capture_count, capture_capacity);
		}
		break;

	default:
		break;
	}
}

/* -------------------------------------------------------------------------- */
/* Expression translation                                                     */
/* -------------------------------------------------------------------------- */

static struct helium_ir_instr *translate_block(struct mono_ctx *ctx,
					       struct helium_expr *expr,
					       char **names,
					       struct helium_type **args,
					       size_t count,
					       char **error);

static struct helium_ir_instr *translate_expr(struct mono_ctx *ctx,
					      struct helium_expr *expr,
					      char **names,
					      struct helium_type **args,
					      size_t count,
					      char **error)
{
	struct helium_ir_instr *instr;
	struct helium_ir_instr *left;
	struct helium_ir_instr *right;
	struct helium_ir_instr *operand;
	size_t i;

	if (!expr) {
		instr = helium_ir_instr_literal(helium_literal(HELIUM_LIT_UNIT,
							       "()", 0, 0),
						0, 0);
		return instr;
	}

	switch (expr->kind) {
	case HELIUM_EXPR_LITERAL:
		instr = helium_ir_instr_literal(copy_literal(expr->u.lit),
						expr->line, expr->col);
		instr->type = subst_type(expr->inferred_type, names, args, count);
		return instr;

	case HELIUM_EXPR_IDENT: {
		const char *name = expr->u.ident.name;
		struct helium_scheme *scheme;
		struct helium_constructor_info *constructor;
		struct helium_binding *binding;

		binding = find_top_level_binding(ctx, name);
		if (binding) {
			scheme = helium_env_lookup(ctx->typed->env, name);
			if (scheme && scheme->var_count > 0) {
				format_error(error,
					     "%d:%d: generic function '%s' used as a value",
					     expr->line, expr->col, name);
				return NULL;
			}
			instr = helium_ir_instr_ident(name, expr->line,
					      expr->col);
			instr->type = subst_type(expr->inferred_type, names,
					       args, count);
			return instr;
		}

		constructor = helium_env_lookup_constructor(ctx->typed->env,
							    name);
		if (constructor) {
			if (constructor->scheme.var_count > 0) {
				format_error(error,
					     "%d:%d: generic constructor '%s' used as a value",
					     expr->line, expr->col, name);
				return NULL;
			}
			instr = helium_ir_instr_ident(name, expr->line,
					      expr->col);
			instr->type = subst_type(expr->inferred_type, names,
					       args, count);
			return instr;
		}

		instr = helium_ir_instr_ident(name, expr->line, expr->col);
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		return instr;
	}

	case HELIUM_EXPR_BINARY:
		left = translate_expr(ctx, expr->u.binary.left, names, args,
				    count, error);
		if (!left)
			return NULL;
		right = translate_expr(ctx, expr->u.binary.right, names, args,
				     count, error);
		if (!right)
			return NULL;
		instr = helium_ir_instr_binary(expr->u.binary.op, left, right,
					       expr->line, expr->col);
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		return instr;

	case HELIUM_EXPR_UNARY:
		operand = translate_expr(ctx, expr->u.unary.operand, names, args,
				     count, error);
		if (!operand)
			return NULL;
		instr = helium_ir_instr_unary(expr->u.unary.op, operand,
				      expr->line, expr->col);
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		return instr;

	case HELIUM_EXPR_CALL: {
		struct helium_expr *func_expr = expr->u.call.func;
		const char *target_name = NULL;
		struct helium_scheme *scheme = NULL;
		struct helium_constructor_info *constructor = NULL;
		struct helium_binding *binding = NULL;
		struct helium_ir_instr **arg_instrs;
		int is_foreign = 0;
		int is_tail = 0;

		arg_instrs = xalloc(expr->u.call.arg_count * sizeof(*arg_instrs));
		for (i = 0; i < expr->u.call.arg_count; i++) {
			arg_instrs[i] = translate_expr(ctx, expr->u.call.args[i],
					       names, args, count, error);
			if (!arg_instrs[i]) {
				while (i > 0)
					helium_ir_instr_free(arg_instrs[--i]);
				free(arg_instrs);
				return NULL;
			}
		}

		if (func_expr->kind == HELIUM_EXPR_IDENT) {
			const char *name = func_expr->u.ident.name;

			binding = find_top_level_binding(ctx, name);
			if (binding)
				scheme = helium_env_lookup(ctx->typed->env,
							 name);
			if (!scheme) {
				constructor =
					helium_env_lookup_constructor(
							ctx->typed->env, name);
				if (constructor)
					scheme = &constructor->scheme;
			}

			if (binding) {
				if (ctx->current.generic_name &&
				    strcmp(name, ctx->current.generic_name) == 0) {
					target_name = xstrdup(ctx->current.spec_name);
				} else {
					struct helium_type **type_args = NULL;
					char *spec_name;

					if (scheme && scheme->var_count > 0) {
						size_t j;

						type_args = xalloc(scheme->var_count *
							   sizeof(*type_args));
						if (!scheme->type ||
						    scheme->type->kind != HELIUM_TYPE_FN ||
						    scheme->type->param_count !=
						    expr->u.call.arg_count) {
							format_error(error,
								     "%d:%d: generic function '%s' arity mismatch",
								     expr->line, expr->col, name);
							for (i = 0;
							     i < expr->u.call.arg_count;
							     i++)
								helium_ir_instr_free(
									arg_instrs[i]);
							free(type_args);
							free(arg_instrs);
							return NULL;
						}
						for (j = 0;
						     j < expr->u.call.arg_count;
						     j++) {
							struct helium_type *arg_type;

							arg_type = resolve_type(
								ctx,
								expr->u.call.args[j]->inferred_type,
								names, args, count);
							if (match_type_args(
								    scheme->type->params[j],
								    arg_type,
								    scheme->vars,
								    scheme->var_count,
								    type_args,
								    error) < 0) {
								helium_type_free(arg_type);
								for (i = 0;
								     i < expr->u.call.arg_count;
								     i++)
									helium_ir_instr_free(
										arg_instrs[i]);
								for (i = 0;
								     i < scheme->var_count;
								     i++)
									helium_type_free(
										type_args[i]);
								free(type_args);
								free(arg_instrs);
								return NULL;
							}
							helium_type_free(arg_type);
						}
					}
					spec_name = request_function(ctx, name, type_args,
							     scheme ? scheme->var_count : 0,
							     error);
					if (type_args) {
						for (i = 0; i < scheme->var_count; i++)
							helium_type_free(type_args[i]);
						free(type_args);
					}
					if (!spec_name) {
						for (i = 0;
						     i < expr->u.call.arg_count; i++)
							helium_ir_instr_free(
								arg_instrs[i]);
						free(arg_instrs);
						return NULL;
					}
					target_name = spec_name;
				}
			} else if (constructor) {
				struct helium_type **type_args = NULL;
				struct helium_type *ret_type;
				char *type_name;
				int is_record;

				if (scheme && scheme->var_count > 0) {
					type_args = xalloc(scheme->var_count *
						   sizeof(*type_args));
					if (match_type_args(scheme->type,
							    func_expr->inferred_type,
							    scheme->vars,
							    scheme->var_count,
							    type_args, error) < 0) {
						for (i = 0;
						     i < expr->u.call.arg_count;
						     i++)
							helium_ir_instr_free(
								arg_instrs[i]);
						free(type_args);
						free(arg_instrs);
						return NULL;
					}
					subst_type_args(type_args, scheme->var_count,
							names, args, count);
				}

				if (func_expr->inferred_type &&
				    func_expr->inferred_type->kind ==
				    HELIUM_TYPE_FN)
					ret_type = func_expr->inferred_type->ret;
				else
					ret_type = func_expr->inferred_type;

				{
					struct helium_type *substituted_ret =
						subst_type(ret_type, names, args, count);

					type_name = request_type_by_name(ctx, substituted_ret,
									 error);
					helium_type_free(substituted_ret);
				}
				if (type_args) {
					for (i = 0; i < scheme->var_count; i++)
						helium_type_free(type_args[i]);
					free(type_args);
				}
				if (!type_name) {
					for (i = 0;
					     i < expr->u.call.arg_count; i++)
						helium_ir_instr_free(
							arg_instrs[i]);
					free(arg_instrs);
					return NULL;
				}

				is_record = constructor->adt &&
					    constructor->adt->kind ==
					    HELIUM_TYPE_DEF_RECORD;
				if (is_record) {
					instr = helium_ir_instr_record_alloc(
						type_name, expr->line,
						expr->col);
					for (i = 0;
					     i < expr->u.call.arg_count; i++)
						helium_ir_record_alloc_add_field(
							instr, arg_instrs[i]);
				} else {
					instr = helium_ir_instr_variant_alloc(
						type_name,
						constructor->variant ?
						constructor->variant->name :
						constructor->name,
						expr->line, expr->col);
					for (i = 0;
					     i < expr->u.call.arg_count; i++)
						helium_ir_variant_alloc_add_field(
							instr, arg_instrs[i]);
				}
				free(type_name);
				free(arg_instrs);
				instr->type = subst_type(expr->inferred_type,
						       names, args,
						       count);
				return instr;
			} else if (name_in_locals(ctx, name)) {
				/* Local variable holding a closure value. */
				struct helium_ir_instr *closure;

				closure = helium_ir_instr_ident(name, expr->line,
							expr->col);
				closure->type = subst_type(
					func_expr->inferred_type,
					names, args, count);
				instr = helium_ir_instr_closure_call(closure,
							     expr->line,
							     expr->col);
				for (i = 0; i < expr->u.call.arg_count; i++)
					helium_ir_closure_call_add_arg(instr,
							       arg_instrs[i]);
				free(arg_instrs);
				instr->type = subst_type(expr->inferred_type,
						       names, args,
						       count);
				return instr;
			} else {
				/* Foreign functions are declared at top level. */
				struct helium_top_decl *decl;
				size_t j;

				for (j = 0;
				     j < ctx->typed->module->decl_count; j++) {
					decl = ctx->typed->module->decls[j];
					if (decl->kind == HELIUM_DECL_FOREIGN &&
					    strcmp(decl->u.foreign.name,
						   name) == 0) {
						is_foreign = 1;
						break;
					}
				}
				if (!is_foreign) {
					format_error(error,
						     "%d:%d: unknown function '%s'",
						     expr->line, expr->col,
						     name);
					for (i = 0;
					     i < expr->u.call.arg_count; i++)
						helium_ir_instr_free(
							arg_instrs[i]);
					free(arg_instrs);
					return NULL;
				}
				target_name = xstrdup(name);
			}
		} else {
			/* Non-identifier callee: must be a closure value. */
			struct helium_ir_instr *closure;

			closure = translate_expr(ctx, func_expr, names, args,
						 count, error);
			if (!closure) {
				for (i = 0; i < expr->u.call.arg_count; i++)
					helium_ir_instr_free(arg_instrs[i]);
				free(arg_instrs);
				return NULL;
			}
			instr = helium_ir_instr_closure_call(closure,
						     expr->line,
						     expr->col);
			for (i = 0; i < expr->u.call.arg_count; i++)
				helium_ir_closure_call_add_arg(instr, arg_instrs[i]);
			free(arg_instrs);
			instr->type = subst_type(expr->inferred_type, names, args,
					       count);
			return instr;
		}

		(void)is_tail;

		if (is_foreign) {
			instr = helium_ir_instr_foreign_call(target_name,
						     expr->line,
						     expr->col);
		} else {
			instr = helium_ir_instr_call(target_name,
					     expr->line,
					     expr->col);
		}
		for (i = 0; i < expr->u.call.arg_count; i++)
			helium_ir_call_add_arg(instr, arg_instrs[i]);
		free(arg_instrs);
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		free((void *)target_name);
		return instr;
	}

	case HELIUM_EXPR_BLOCK:
		return translate_block(ctx, expr, names, args, count, error);

	case HELIUM_EXPR_IF: {
		struct helium_ir_instr *cond;
		struct helium_ir_instr *then_branch;
		struct helium_ir_instr *else_branch;
		struct helium_ir_block *then_block;
		struct helium_ir_block *else_block;

		cond = translate_expr(ctx, expr->u.if_expr.cond, names, args,
				      count, error);
		if (!cond)
			return NULL;
		then_branch = translate_expr(ctx, expr->u.if_expr.then_branch,
					     names, args, count, error);
		if (!then_branch)
			return NULL;
		else_branch = translate_expr(ctx, expr->u.if_expr.else_branch,
					     names, args, count, error);
		if (!else_branch)
			return NULL;

		then_block = helium_ir_block_new();
		else_block = helium_ir_block_new();
		helium_ir_block_add_instr(then_block, then_branch);
		helium_ir_block_add_instr(else_block, else_branch);

		instr = helium_ir_instr_if(cond, expr->line, expr->col);
		helium_ir_if_set_then(instr, then_block);
		helium_ir_if_set_else(instr, else_block);
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		return instr;
	}

	case HELIUM_EXPR_LAMBDA: {
		/* Anonymous local function.  If it captures variables from the
		 * enclosing scope, emit a closure value (function pointer + env).
		 * Otherwise return a plain top-level function reference.
		 */
		static int lambda_counter;
		char buf[64];
		struct helium_ir_function *func;
		struct helium_ir_block *body_block;
		struct helium_ir_instr *body_instr;
		struct helium_ir_instr *closure;
		char *captures[32];
		struct helium_type *capture_types[32];
		size_t capture_count;
		char **lambda_params;
		size_t j;
		size_t base;

		snprintf(buf, sizeof(buf), "__lambda_%d", lambda_counter++);
		capture_count = 0;
		lambda_params = xalloc(expr->u.lambda.param_count *
				       sizeof(*lambda_params));
		for (j = 0; j < expr->u.lambda.param_count; j++)
			lambda_params[j] = expr->u.lambda.params[j]->name;
		collect_captures(ctx, expr, lambda_params,
				 expr->u.lambda.param_count,
				 captures, capture_types, &capture_count, 32);
		free(lambda_params);

		func = helium_ir_function_new(buf,
				      subst_type(lambda_ret_type(expr),
						 names, args, count),
				      expr->line, expr->col);

		func->is_closure = 1;
		{
			struct helium_ir_param *env_param;
			struct helium_type *env_type;

			env_type = helium_type_named("str", expr->line,
						   expr->col);
			env_param = helium_ir_param_new("__env", env_type,
								expr->line, expr->col);
			helium_ir_function_add_param(func, env_param);
		}
		for (j = 0; j < capture_count; j++)
			helium_ir_function_add_capture(func, captures[j],
					       capture_types[j]);

		for (j = 0; j < expr->u.lambda.param_count; j++) {
			struct helium_param *p = expr->u.lambda.params[j];
			struct helium_ir_param *irp;
			struct helium_type *pt;

			pt = subst_type(p->type, names, args, count);
			irp = helium_ir_param_new(p->name, pt, p->line,
						p->col);
			helium_ir_function_add_param(func, irp);
		}

		base = mark_locals(ctx);
		for (j = 0; j < capture_count; j++)
			push_local(ctx, captures[j]);
		for (j = 0; j < expr->u.lambda.param_count; j++)
			push_local(ctx, expr->u.lambda.params[j]->name);

		body_instr = translate_expr(ctx, expr->u.lambda.body,
					    names, args, count, error);

		pop_locals_to(ctx, base);

		if (!body_instr) {
			helium_ir_function_free(func);
			for (j = 0; j < capture_count; j++)
				free(captures[j]);
			return NULL;
		}
		body_block = helium_ir_block_new();
		helium_ir_block_add_instr(body_block, body_instr);
		helium_ir_function_set_body(func, body_block);

		helium_ir_program_add_function(ctx->prog, func);

		closure = helium_ir_instr_closure_alloc(buf, expr->line,
							      expr->col);
		for (j = 0; j < capture_count; j++) {
			struct helium_ir_instr *cap;

			cap = helium_ir_instr_ident(captures[j],
					    expr->line,
					    expr->col);
			helium_ir_closure_alloc_add_capture(closure,
						    captures[j],
					    cap);
			free(captures[j]);
		}
		closure->type = subst_type(expr->inferred_type, names, args,
					 count);
		return closure;
	}

	case HELIUM_EXPR_RECORD_LIT: {
		struct helium_type *lit_type;
		char *type_name;

		lit_type = subst_type(expr->inferred_type, names, args, count);
		type_name = request_type_by_name(ctx, lit_type, error);
		helium_type_free(lit_type);
		if (!type_name)
			return NULL;

		instr = helium_ir_instr_record_alloc(type_name, expr->line,
					     expr->col);
		free(type_name);

		for (i = 0; i < expr->u.record_lit.field_count; i++) {
			struct helium_ir_instr *fv;

			fv = translate_expr(ctx,
					    expr->u.record_lit.fields[i]->value,
					    names, args, count, error);
			if (!fv) {
				helium_ir_instr_free(instr);
				return NULL;
			}
			helium_ir_record_alloc_add_field(instr, fv);
		}
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		return instr;
	}

	case HELIUM_EXPR_ARRAY_LIT: {
		struct helium_type *arr_type;
		struct helium_type *elem_type = NULL;

		arr_type = subst_type(expr->inferred_type, names, args, count);
		if (arr_type && arr_type->kind == HELIUM_TYPE_ARRAY)
			elem_type = helium_type_copy(arr_type->elem_type);
		helium_type_free(arr_type);

		instr = helium_ir_instr_array_alloc(elem_type,
					    (long)expr->u.array_lit.item_count,
					    expr->line, expr->col);
		for (i = 0; i < expr->u.array_lit.item_count; i++) {
			struct helium_ir_instr *item;

			item = translate_expr(ctx,
					      expr->u.array_lit.items[i],
					      names, args, count, error);
			if (!item) {
				helium_ir_instr_free(instr);
				return NULL;
			}
			helium_ir_array_alloc_add_item(instr, item);
		}
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		return instr;
	}

	case HELIUM_EXPR_FIELD: {
		struct helium_ir_instr *object;

		object = translate_expr(ctx, expr->u.field.object, names, args,
					count, error);
		if (!object)
			return NULL;
		instr = helium_ir_instr_record_get(object,
					   expr->u.field.name,
					   expr->line, expr->col);
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		return instr;
	}

	case HELIUM_EXPR_RETURN:
		operand = translate_expr(ctx, expr->u.ret.expr, names, args,
				     count, error);
		if (!operand)
			return NULL;
		instr = helium_ir_instr_return(operand, expr->line, expr->col);
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		return instr;

	case HELIUM_EXPR_LOOP: {
		struct helium_ir_instr *loop;
		struct helium_ir_block *body_block;
		struct helium_ir_instr *body_instr;

		loop = helium_ir_instr_loop(expr->line, expr->col);
		for (i = 0; i < expr->u.loop.binding_count; i++) {
			struct helium_loop_binding *lb = expr->u.loop.bindings[i];
			struct helium_ir_let_binding *b;
			struct helium_ir_instr *init;
			struct helium_type *bt;

			init = translate_expr(ctx, lb->init, names, args, count,
					      error);
			if (!init) {
				helium_ir_instr_free(loop);
				return NULL;
			}
			bt = subst_type(lb->type, names, args, count);
			b = helium_ir_let_binding_new(lb->name, bt, init,
						      lb->line, lb->col);
			helium_ir_loop_add_binding(loop, b);
		}

		body_instr = translate_expr(ctx, expr->u.loop.body, names, args,
					    count, error);
		if (!body_instr) {
			helium_ir_instr_free(loop);
			return NULL;
		}
		body_block = helium_ir_block_new();
		helium_ir_block_add_instr(body_block, body_instr);
		helium_ir_loop_set_body(loop, body_block);
		loop->type = subst_type(expr->inferred_type, names, args,
				      count);
		return loop;
	}

	case HELIUM_EXPR_RECUR: {
		instr = helium_ir_instr_recur(expr->line, expr->col);
		for (i = 0; i < expr->u.recur.arg_count; i++) {
			struct helium_ir_instr *arg;

			arg = translate_expr(ctx, expr->u.recur.args[i], names,
					     args, count, error);
			if (!arg) {
				helium_ir_instr_free(instr);
				return NULL;
			}
			helium_ir_recur_add_arg(instr, arg);
		}
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		return instr;
	}

	case HELIUM_EXPR_MATCH: {
		struct helium_ir_instr *value;

		value = translate_expr(ctx, expr->u.match.value, names, args,
				       count, error);
		if (!value)
			return NULL;
		instr = helium_ir_instr_match(value, expr->line, expr->col);
		for (i = 0; i < expr->u.match.arm_count; i++) {
			struct helium_match_arm *arm = expr->u.match.arms[i];
			struct helium_ir_match_arm *ir_arm;
			struct helium_ir_block *arm_block;
			struct helium_ir_instr *arm_body;

			arm_body = translate_expr(ctx, arm->expr, names, args,
						  count, error);
			if (!arm_body) {
				helium_ir_instr_free(instr);
				return NULL;
			}
			arm_block = helium_ir_block_new();
			helium_ir_block_add_instr(arm_block, arm_body);
			ir_arm = helium_ir_match_arm(
				helium_pattern_copy(arm->pattern),
				arm_block, arm->line, arm->col);
			helium_ir_match_add_arm(instr, ir_arm);
		}
		instr->type = subst_type(expr->inferred_type, names, args,
				       count);
		return instr;
	}

	case HELIUM_EXPR_ANNOT:
		return translate_expr(ctx, expr->u.annot.expr, names, args,
				      count, error);

	case HELIUM_EXPR_BIND: {
		struct helium_ir_instr *left;
		struct helium_ir_instr *right;
		struct helium_ir_instr *call;

		left = translate_expr(ctx, expr->u.bind.left, names, args,
				      count, error);
		if (!left)
			return NULL;
		right = translate_expr(ctx, expr->u.bind.right, names, args,
				       count, error);
		if (!right)
			return NULL;

		/* The right side must be a function value; call it with the
		 * left value as the argument.  Top-level functions are called
		 * directly; closure values use an indirect closure call.
		 */
		if (right->kind == HELIUM_IR_INSTR_IDENT &&
		    find_top_level_binding(ctx, right->u.ident.name)) {
			call = helium_ir_instr_call(right->u.ident.name, expr->line,
					    expr->col);
			helium_ir_call_add_arg(call, left);
			helium_ir_instr_free(right);
		} else if (right->kind == HELIUM_IR_INSTR_IDENT ||
			   right->kind == HELIUM_IR_INSTR_CLOSURE_ALLOC) {
			call = helium_ir_instr_closure_call(right, expr->line,
						    expr->col);
			helium_ir_closure_call_add_arg(call, left);
		} else {
			format_error(error,
				     "%d:%d: bind right side must be a function",
				     expr->line, expr->col);
			helium_ir_instr_free(left);
			helium_ir_instr_free(right);
			return NULL;
		}
		call->type = subst_type(expr->inferred_type, names, args, count);
		return call;
	}

	case HELIUM_EXPR_FSTRING: {
		struct helium_ir_instr *fstring;
		size_t j;

		fstring = helium_ir_instr_fstring(expr->line, expr->col);
		for (j = 0; j < expr->u.fstring.part_count; j++) {
			struct helium_fstring_part *part = expr->u.fstring.parts[j];

			if (part->is_expr) {
				struct helium_ir_instr *e;

				e = translate_expr(ctx, part->u.expr, names, args,
						   count, error);
				if (!e) {
					helium_ir_instr_free(fstring);
					return NULL;
				}
				helium_ir_fstring_add_expr(fstring, e);
			} else {
				helium_ir_fstring_add_text(fstring, part->u.text,
							   part->line,
							   part->col);
			}
		}
		fstring->type = subst_type(expr->inferred_type, names, args,
					   count);
		return fstring;
	}

	default:
		format_error(error,
			     "%d:%d: expression kind %d not yet supported by monomorphization",
			     expr->line, expr->col, expr->kind);
		return NULL;
	}
}

static struct helium_ir_instr *translate_block(struct mono_ctx *ctx,
					       struct helium_expr *expr,
					       char **names,
					       struct helium_type **args,
					       size_t count,
					       char **error)
{
	struct helium_ir_instr *block_instr;
	struct helium_ir_block *block;
	size_t i;
	size_t base;

	block = helium_ir_block_new();
	base = mark_locals(ctx);

	for (i = 0; i < expr->u.block.binding_count; i++) {
		struct helium_binding *b = expr->u.block.bindings[i];
		struct helium_ir_instr *value;
		struct helium_ir_instr *let;
		struct helium_type *bt;

		value = translate_expr(ctx, b->value, names, args, count, error);
		if (!value) {
			helium_ir_block_free(block);
			pop_locals_to(ctx, base);
			return NULL;
		}
		bt = subst_type(b->type, names, args, count);
		let = helium_ir_instr_let(b->name, bt, value, b->line, b->col);
		helium_ir_block_add_instr(block, let);
		push_local(ctx, b->name);
	}

	if (expr->u.block.expr_count == 0) {
		struct helium_ir_instr *unit;

		unit = helium_ir_instr_literal(helium_literal(HELIUM_LIT_UNIT,
							      "()", 0, 0),
					       expr->line, expr->col);
		helium_ir_block_add_instr(block, unit);
	} else {
		for (i = 0; i < expr->u.block.expr_count; i++) {
			struct helium_ir_instr *e;

			e = translate_expr(ctx, expr->u.block.exprs[i], names,
					   args, count, error);
			if (!e) {
				helium_ir_block_free(block);
				pop_locals_to(ctx, base);
				return NULL;
			}
			helium_ir_block_add_instr(block, e);
		}
	}

	pop_locals_to(ctx, base);

	block_instr = helium_ir_instr_block(expr->line, expr->col);
	helium_ir_block_free(block_instr->u.block.block);
	block_instr->u.block.block = block;
	block_instr->type = subst_type(expr->inferred_type, names, args,
				       count);
	return block_instr;
}

/* -------------------------------------------------------------------------- */
/* Function specialization                                                    */
/* -------------------------------------------------------------------------- */

static char *request_function(struct mono_ctx *ctx,
			      const char *generic_name,
			      struct helium_type **type_args,
			      size_t type_arg_count,
			      char **error)
{
	struct helium_binding *binding;
	struct helium_expr *lambda;
	struct helium_ir_function *func;
	char *name;
	size_t i;

	binding = find_top_level_binding(ctx, generic_name);
	if (!binding) {
		format_error(error, "unknown function '%s'", generic_name);
		return NULL;
	}

	if (binding->value->kind != HELIUM_EXPR_LAMBDA) {
		format_error(error, "'%s' is not a function", generic_name);
		return NULL;
	}

	lambda = binding->value;
	name = specialized_name(generic_name, type_args, type_arg_count);
	if (find_function(ctx, name)) {
		char *copy = xstrdup(name);

		free(name);
		return copy;
	}

	func = helium_ir_function_new(name,
				      subst_type(lambda_ret_type(lambda),
						 lambda->u.lambda.type_params,
						 type_args, type_arg_count),
				      binding->line, binding->col);

	for (i = 0; i < lambda->u.lambda.param_count; i++) {
		struct helium_param *p = lambda->u.lambda.params[i];
		struct helium_type *pt;
		struct helium_ir_param *irp;

		pt = subst_type(p->type, lambda->u.lambda.type_params,
				type_args, type_arg_count);
		irp = helium_ir_param_new(p->name, pt, p->line, p->col);
		helium_ir_function_add_param(func, irp);
	}

	helium_ir_program_add_function(ctx->prog, func);

	ctx->current.generic_name = generic_name;
	ctx->current.type_args = type_args;
	ctx->current.type_arg_count = type_arg_count;
	ctx->current.spec_name = name;

	{
		struct helium_ir_block *body_block;
		struct helium_ir_instr *body_instr;
		size_t base;
		size_t j;

		base = mark_locals(ctx);
		for (j = 0; j < lambda->u.lambda.param_count; j++)
			push_local(ctx, lambda->u.lambda.params[j]->name);

		body_instr = translate_expr(ctx, lambda->u.lambda.body,
					    lambda->u.lambda.type_params,
					    type_args, type_arg_count,
					    error);

		pop_locals_to(ctx, base);

		if (!body_instr) {
			ctx->current.generic_name = NULL;
			ctx->current.type_args = NULL;
			ctx->current.type_arg_count = 0;
			ctx->current.spec_name = NULL;
			free(name);
			return NULL;
		}
		body_block = helium_ir_block_new();
		helium_ir_block_add_instr(body_block, body_instr);
		helium_ir_function_set_body(func, body_block);
	}

	ctx->current.generic_name = NULL;
	ctx->current.type_args = NULL;
	ctx->current.type_arg_count = 0;
	ctx->current.spec_name = NULL;

	return name;
}

/* -------------------------------------------------------------------------- */
/* Module function generation                                                 */
/* -------------------------------------------------------------------------- */

static int generate_all_functions(struct mono_ctx *ctx, char **error)
{
	struct helium_ir_function *func;
	struct helium_ir_block *body_block;
	struct helium_ir_instr *body_instr;
	size_t i;

	for (i = 0; i < ctx->typed->module->decl_count; i++) {
		struct helium_top_decl *decl = ctx->typed->module->decls[i];
		struct helium_binding *binding;
		char *name;

		if (decl->kind != HELIUM_DECL_BINDING)
			continue;
		binding = decl->u.binding;
		name = binding->name;

		if (binding->value->kind == HELIUM_EXPR_LAMBDA) {
			struct helium_expr *lambda = binding->value;
			char *generated;

			if (lambda->u.lambda.type_param_count > 0)
				continue;
			generated = request_function(ctx, name, NULL, 0, error);
			if (!generated)
				return -1;
			free(generated);
			continue;
		}

		func = helium_ir_function_new(name,
					      subst_type(binding->value->
							 inferred_type,
							 NULL, NULL, 0),
					      binding->line, binding->col);
		body_instr = translate_expr(ctx, binding->value, NULL, NULL, 0,
					    error);
		if (!body_instr) {
			helium_ir_function_free(func);
			return -1;
		}
		body_block = helium_ir_block_new();
		helium_ir_block_add_instr(body_block, body_instr);
		helium_ir_function_set_body(func, body_block);
		helium_ir_program_add_function(ctx->prog, func);
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* Main entry generation                                                      */
/* -------------------------------------------------------------------------- */

static int generate_main(struct mono_ctx *ctx, char **error)
{
	struct helium_binding *main_binding;
	struct helium_ir_function *main_func;
	struct helium_ir_instr *value;
	struct helium_ir_block *body;

	main_binding = find_top_level_binding(ctx, "main");
	if (!main_binding) {
		format_error(error, "module has no 'main' binding");
		return -1;
	}

	if (main_binding->value->kind == HELIUM_EXPR_LAMBDA) {
		char *name;
		struct helium_ir_function *f;

		name = request_function(ctx, "main", NULL, 0, error);
		if (!name)
			return -1;
		/*
		 * Rename the generated function so the C entry point emitted by
		 * the backend can keep the name "main".
		 */
		f = find_function(ctx, name);
		if (f) {
			free(f->name);
			f->name = xstrdup("helium_main");
		}
		ctx->prog->main_name = xstrdup("helium_main");
		free(name);
		return 0;
	}

	main_func = helium_ir_function_new("helium_main",
					   subst_type(main_binding->value->
						      inferred_type,
						      NULL, NULL, 0),
					   main_binding->line,
					   main_binding->col);
	value = translate_expr(ctx, main_binding->value, NULL, NULL, 0,
			       error);
	if (!value)
		return -1;
	body = helium_ir_block_new();
	helium_ir_block_add_instr(body, value);
	helium_ir_function_set_body(main_func, body);
	helium_ir_program_add_function(ctx->prog, main_func);
	ctx->prog->main_name = xstrdup("helium_main");
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

struct helium_ir_program *helium_monomorphize(struct helium_typed_module *typed,
				      char **error)
{
	struct mono_ctx ctx;

	if (!typed || !typed->module) {
		format_error(error, "internal error: null typed module");
		return NULL;
	}


	memset(&ctx, 0, sizeof(ctx));
	ctx.typed = typed;
	ctx.prog = helium_ir_program_new();

	if (generate_main(&ctx, error) < 0) {
		helium_ir_program_free(ctx.prog);
		pop_locals_to(&ctx, 0);
		free(ctx.locals);
		return NULL;
	}

	pop_locals_to(&ctx, 0);
	free(ctx.locals);
	return ctx.prog;
}

struct helium_ir_program *helium_monomorphize_module(
				struct helium_typed_module *typed,
				char **error)
{
	struct mono_ctx ctx;

	if (!typed || !typed->module) {
		format_error(error, "internal error: null typed module");
		return NULL;
	}


	memset(&ctx, 0, sizeof(ctx));
	ctx.typed = typed;
	ctx.prog = helium_ir_program_new();

	if (generate_all_functions(&ctx, error) < 0) {
		helium_ir_program_free(ctx.prog);
		pop_locals_to(&ctx, 0);
		free(ctx.locals);
		return NULL;
	}

	pop_locals_to(&ctx, 0);
	free(ctx.locals);
	return ctx.prog;
}
