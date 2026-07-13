/* SPDX-License-Identifier: TBD */
/*
 * ast.c - AST node constructors and destructors.
 */

#include "ast.h"

#include <stdlib.h>
#include <string.h>

static char *xstrdup(const char *s)
{
	if (!s)
		return NULL;
	return strdup(s);
}

static void *xalloc(size_t size)
{
	void *p = calloc(1, size);

	if (!p)
		abort();
	return p;
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

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

struct helium_type *helium_type_named(const char *name, int line, int col)
{
	struct helium_type *type = xalloc(sizeof(*type));

	type->kind = HELIUM_TYPE_NAMED;
	type->line = line;
	type->col = col;
	type->name = xstrdup(name);
	return type;
}

struct helium_type *helium_type_var(const char *name, int line, int col)
{
	struct helium_type *type = xalloc(sizeof(*type));

	type->kind = HELIUM_TYPE_VAR;
	type->line = line;
	type->col = col;
	type->name = xstrdup(name);
	return type;
}

struct helium_type *helium_type_fn(int line, int col)
{
	struct helium_type *type = xalloc(sizeof(*type));

	type->kind = HELIUM_TYPE_FN;
	type->line = line;
	type->col = col;
	return type;
}

struct helium_type *helium_type_array(struct helium_type *elem, long size,
				      int line, int col)
{
	struct helium_type *type = xalloc(sizeof(*type));

	type->kind = HELIUM_TYPE_ARRAY;
	type->line = line;
	type->col = col;
	type->elem_type = elem;
	type->array_size = size;
	return type;
}

void helium_type_add_arg(struct helium_type *type, struct helium_type *arg)
{
	append_ptr((void ***)&type->args, &type->arg_count, &type->arg_capacity,
		   arg);
}

void helium_type_add_param(struct helium_type *type, struct helium_type *param)
{
	append_ptr((void ***)&type->params, &type->param_count,
		   &type->param_capacity, param);
}

void helium_type_set_ret(struct helium_type *type, struct helium_type *ret)
{
	type->ret = ret;
}

static void free_types(struct helium_type **types, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		helium_type_free(types[i]);
}

void helium_type_free(struct helium_type *type)
{
	if (!type)
		return;
	free(type->name);
	free_types(type->args, type->arg_count);
	free(type->args);
	free_types(type->params, type->param_count);
	free(type->params);
	helium_type_free(type->ret);
	helium_type_free(type->elem_type);
	free(type);
}

/* -------------------------------------------------------------------------- */
/* Literals and small helpers                                                 */
/* -------------------------------------------------------------------------- */

struct helium_literal *helium_literal(int kind, const char *text, int line,
				      int col)
{
	struct helium_literal *lit = xalloc(sizeof(*lit));

	lit->kind = kind;
	lit->text = xstrdup(text);
	lit->line = line;
	lit->col = col;
	return lit;
}

void helium_literal_free(struct helium_literal *lit)
{
	if (!lit)
		return;
	free(lit->text);
	free(lit);
}

struct helium_field_init *helium_field_init(const char *name,
					    struct helium_expr *value,
					    int line, int col)
{
	struct helium_field_init *field = xalloc(sizeof(*field));

	field->name = xstrdup(name);
	field->value = value;
	field->line = line;
	field->col = col;
	return field;
}

void helium_field_init_free(struct helium_field_init *field)
{
	if (!field)
		return;
	free(field->name);
	helium_expr_free(field->value);
	free(field);
}

struct helium_fstring_part *helium_fstring_part_text(const char *text,
						     int line, int col)
{
	struct helium_fstring_part *part = xalloc(sizeof(*part));

	part->is_expr = 0;
	part->u.text = xstrdup(text);
	part->line = line;
	part->col = col;
	return part;
}

struct helium_fstring_part *helium_fstring_part_expr(struct helium_expr *expr)
{
	struct helium_fstring_part *part = xalloc(sizeof(*part));

	part->is_expr = 1;
	part->u.expr = expr;
	part->line = expr->line;
	part->col = expr->col;
	return part;
}

void helium_fstring_part_free(struct helium_fstring_part *part)
{
	if (!part)
		return;
	if (!part->is_expr)
		free(part->u.text);
	else
		helium_expr_free(part->u.expr);
	free(part);
}

/* -------------------------------------------------------------------------- */
/* Expressions                                                                */
/* -------------------------------------------------------------------------- */

struct helium_expr *helium_expr_literal(struct helium_literal *lit,
					int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_LITERAL;
	expr->line = line;
	expr->col = col;
	expr->u.lit = lit;
	return expr;
}

struct helium_expr *helium_expr_ident(const char *name, int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_IDENT;
	expr->line = line;
	expr->col = col;
	expr->u.ident.name = xstrdup(name);
	return expr;
}

struct helium_expr *helium_expr_binary(int op, struct helium_expr *left,
				       struct helium_expr *right, int line,
				       int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_BINARY;
	expr->line = line;
	expr->col = col;
	expr->u.binary.op = op;
	expr->u.binary.left = left;
	expr->u.binary.right = right;
	return expr;
}

struct helium_expr *helium_expr_unary(int op, struct helium_expr *operand,
				      int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_UNARY;
	expr->line = line;
	expr->col = col;
	expr->u.unary.op = op;
	expr->u.unary.operand = operand;
	return expr;
}

struct helium_expr *helium_expr_call(struct helium_expr *func, int line,
				     int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_CALL;
	expr->line = line;
	expr->col = col;
	expr->u.call.func = func;
	return expr;
}

void helium_call_add_arg(struct helium_expr *call, struct helium_expr *arg)
{
	append_ptr((void ***)&call->u.call.args, &call->u.call.arg_count,
		   &call->u.call.arg_capacity, arg);
}

struct helium_expr *helium_expr_block(int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_BLOCK;
	expr->line = line;
	expr->col = col;
	return expr;
}

void helium_block_add_binding(struct helium_expr *block,
			      struct helium_binding *binding)
{
	append_ptr((void ***)&block->u.block.bindings,
		   &block->u.block.binding_count,
		   &block->u.block.binding_capacity, binding);
}

void helium_block_add_expr(struct helium_expr *block, struct helium_expr *expr)
{
	append_ptr((void ***)&block->u.block.exprs, &block->u.block.expr_count,
		   &block->u.block.expr_capacity, expr);
}

struct helium_expr *helium_expr_if(struct helium_expr *cond,
				   struct helium_expr *then_branch,
				   struct helium_expr *else_branch,
				   int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_IF;
	expr->line = line;
	expr->col = col;
	expr->u.if_expr.cond = cond;
	expr->u.if_expr.then_branch = then_branch;
	expr->u.if_expr.else_branch = else_branch;
	return expr;
}

struct helium_expr *helium_expr_match(struct helium_expr *value, int line,
				      int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_MATCH;
	expr->line = line;
	expr->col = col;
	expr->u.match.value = value;
	return expr;
}

void helium_match_add_arm(struct helium_expr *match,
			  struct helium_match_arm *arm)
{
	append_ptr((void ***)&match->u.match.arms, &match->u.match.arm_count,
		   &match->u.match.arm_capacity, arm);
}

struct helium_expr *helium_expr_loop(int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_LOOP;
	expr->line = line;
	expr->col = col;
	return expr;
}

void helium_loop_add_binding(struct helium_expr *loop,
			     struct helium_loop_binding *binding)
{
	append_ptr((void ***)&loop->u.loop.bindings,
		   &loop->u.loop.binding_count, &loop->u.loop.binding_capacity,
		   binding);
}

struct helium_expr *helium_expr_recur(int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_RECUR;
	expr->line = line;
	expr->col = col;
	return expr;
}

void helium_recur_add_arg(struct helium_expr *recur, struct helium_expr *arg)
{
	append_ptr((void ***)&recur->u.recur.args, &recur->u.recur.arg_count,
		   &recur->u.recur.arg_capacity, arg);
}

struct helium_expr *helium_expr_lambda(int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_LAMBDA;
	expr->line = line;
	expr->col = col;
	return expr;
}

void helium_lambda_add_type_param(struct helium_expr *lambda,
				  const char *name)
{
	append_ptr((void ***)&lambda->u.lambda.type_params,
		   &lambda->u.lambda.type_param_count,
		   &lambda->u.lambda.type_param_capacity,
		   (void *)xstrdup(name));
}

void helium_lambda_add_param(struct helium_expr *lambda,
			     struct helium_param *param)
{
	append_ptr((void ***)&lambda->u.lambda.params,
		   &lambda->u.lambda.param_count,
		   &lambda->u.lambda.param_capacity, param);
}

void helium_lambda_set_ret_type(struct helium_expr *lambda,
				struct helium_type *type)
{
	lambda->u.lambda.ret_type = type;
}

void helium_lambda_set_body(struct helium_expr *lambda,
			    struct helium_expr *body)
{
	lambda->u.lambda.body = body;
}

struct helium_expr *helium_expr_record_lit(const char *name, int line,
					   int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_RECORD_LIT;
	expr->line = line;
	expr->col = col;
	expr->u.record_lit.name = xstrdup(name);
	return expr;
}

void helium_record_lit_add_field(struct helium_expr *lit,
				 struct helium_field_init *field)
{
	append_ptr((void ***)&lit->u.record_lit.fields,
		   &lit->u.record_lit.field_count,
		   &lit->u.record_lit.field_capacity, field);
}

struct helium_expr *helium_expr_array_lit(int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_ARRAY_LIT;
	expr->line = line;
	expr->col = col;
	return expr;
}

void helium_array_lit_add_item(struct helium_expr *lit,
			       struct helium_expr *item)
{
	append_ptr((void ***)&lit->u.array_lit.items,
		   &lit->u.array_lit.item_count,
		   &lit->u.array_lit.item_capacity, item);
}

struct helium_expr *helium_expr_field(struct helium_expr *object,
				      const char *name, int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_FIELD;
	expr->line = line;
	expr->col = col;
	expr->u.field.object = object;
	expr->u.field.name = xstrdup(name);
	return expr;
}

struct helium_expr *helium_expr_annot(struct helium_expr *expr,
				      struct helium_type *type, int line,
				      int col)
{
	struct helium_expr *node = xalloc(sizeof(*node));

	node->kind = HELIUM_EXPR_ANNOT;
	node->line = line;
	node->col = col;
	node->u.annot.expr = expr;
	node->u.annot.type = type;
	return node;
}

struct helium_expr *helium_expr_bind(struct helium_expr *left,
				     struct helium_expr *right, int line,
				     int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_BIND;
	expr->line = line;
	expr->col = col;
	expr->u.bind.left = left;
	expr->u.bind.right = right;
	return expr;
}

struct helium_expr *helium_expr_return(struct helium_expr *expr, int line,
				       int col)
{
	struct helium_expr *node = xalloc(sizeof(*node));

	node->kind = HELIUM_EXPR_RETURN;
	node->line = line;
	node->col = col;
	node->u.ret.expr = expr;
	return node;
}

struct helium_expr *helium_expr_fstring(int line, int col)
{
	struct helium_expr *expr = xalloc(sizeof(*expr));

	expr->kind = HELIUM_EXPR_FSTRING;
	expr->line = line;
	expr->col = col;
	return expr;
}

void helium_fstring_add_text(struct helium_expr *fstring, const char *text,
			     int line, int col)
{
	struct helium_fstring_part *part = helium_fstring_part_text(text, line, col);

	append_ptr((void ***)&fstring->u.fstring.parts,
		   &fstring->u.fstring.part_count,
		   &fstring->u.fstring.part_capacity, part);
}

void helium_fstring_add_expr(struct helium_expr *fstring,
			     struct helium_expr *expr)
{
	struct helium_fstring_part *part = helium_fstring_part_expr(expr);

	append_ptr((void ***)&fstring->u.fstring.parts,
		   &fstring->u.fstring.part_count,
		   &fstring->u.fstring.part_capacity, part);
}

static void free_bindings(struct helium_binding **bindings, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		helium_binding_free(bindings[i]);
}

static void free_exprs(struct helium_expr **exprs, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		helium_expr_free(exprs[i]);
}

static void free_fields(struct helium_field_init **fields, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		helium_field_init_free(fields[i]);
}

static void free_fstring_parts(struct helium_fstring_part **parts, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		helium_fstring_part_free(parts[i]);
}

void helium_expr_free(struct helium_expr *expr)
{
	if (!expr)
		return;
	helium_type_free(expr->inferred_type);
	switch (expr->kind) {
	case HELIUM_EXPR_LITERAL:
		helium_literal_free(expr->u.lit);
		break;
	case HELIUM_EXPR_IDENT:
		free(expr->u.ident.name);
		break;
	case HELIUM_EXPR_BINARY:
		helium_expr_free(expr->u.binary.left);
		helium_expr_free(expr->u.binary.right);
		break;
	case HELIUM_EXPR_UNARY:
		helium_expr_free(expr->u.unary.operand);
		break;
	case HELIUM_EXPR_CALL:
		helium_expr_free(expr->u.call.func);
		free_exprs(expr->u.call.args, expr->u.call.arg_count);
		free(expr->u.call.args);
		break;
	case HELIUM_EXPR_BLOCK:
		free_bindings(expr->u.block.bindings, expr->u.block.binding_count);
		free(expr->u.block.bindings);
		free_exprs(expr->u.block.exprs, expr->u.block.expr_count);
		free(expr->u.block.exprs);
		break;
	case HELIUM_EXPR_IF:
		helium_expr_free(expr->u.if_expr.cond);
		helium_expr_free(expr->u.if_expr.then_branch);
		helium_expr_free(expr->u.if_expr.else_branch);
		break;
	case HELIUM_EXPR_MATCH:
		helium_expr_free(expr->u.match.value);
		free(expr->u.match.arms);
		break;
	case HELIUM_EXPR_LOOP:
		free(expr->u.loop.bindings);
		helium_expr_free(expr->u.loop.body);
		break;
	case HELIUM_EXPR_RECUR:
		free_exprs(expr->u.recur.args, expr->u.recur.arg_count);
		free(expr->u.recur.args);
		break;
	case HELIUM_EXPR_LAMBDA: {
		size_t i;

		for (i = 0; i < expr->u.lambda.type_param_count; i++)
			free(expr->u.lambda.type_params[i]);
		free(expr->u.lambda.type_params);
		for (i = 0; i < expr->u.lambda.param_count; i++)
			helium_param_free(expr->u.lambda.params[i]);
		free(expr->u.lambda.params);
		helium_type_free(expr->u.lambda.ret_type);
		helium_expr_free(expr->u.lambda.body);
		break;
	}
	case HELIUM_EXPR_RECORD_LIT:
		free(expr->u.record_lit.name);
		free_fields(expr->u.record_lit.fields,
			    expr->u.record_lit.field_count);
		free(expr->u.record_lit.fields);
		break;
	case HELIUM_EXPR_ARRAY_LIT:
		free_exprs(expr->u.array_lit.items, expr->u.array_lit.item_count);
		free(expr->u.array_lit.items);
		break;
	case HELIUM_EXPR_FIELD:
		helium_expr_free(expr->u.field.object);
		free(expr->u.field.name);
		break;
	case HELIUM_EXPR_ANNOT:
		helium_expr_free(expr->u.annot.expr);
		helium_type_free(expr->u.annot.type);
		break;
	case HELIUM_EXPR_BIND:
		helium_expr_free(expr->u.bind.left);
		helium_expr_free(expr->u.bind.right);
		break;
	case HELIUM_EXPR_RETURN:
		helium_expr_free(expr->u.ret.expr);
		break;
	case HELIUM_EXPR_FSTRING:
		free_fstring_parts(expr->u.fstring.parts,
				   expr->u.fstring.part_count);
		free(expr->u.fstring.parts);
		break;
	}
	free(expr);
}

/* -------------------------------------------------------------------------- */
/* Patterns                                                                   */
/* -------------------------------------------------------------------------- */

struct helium_pattern *helium_pattern_wild(int line, int col)
{
	struct helium_pattern *pat = xalloc(sizeof(*pat));

	pat->kind = HELIUM_PATTERN_WILD;
	pat->line = line;
	pat->col = col;
	return pat;
}

struct helium_pattern *helium_pattern_literal(struct helium_literal *lit,
					      int line, int col)
{
	struct helium_pattern *pat = xalloc(sizeof(*pat));

	pat->kind = HELIUM_PATTERN_LITERAL;
	pat->line = line;
	pat->col = col;
	pat->lit = lit;
	return pat;
}

struct helium_pattern *helium_pattern_ident(const char *name, int line,
					    int col)
{
	struct helium_pattern *pat = xalloc(sizeof(*pat));

	pat->kind = HELIUM_PATTERN_IDENT;
	pat->line = line;
	pat->col = col;
	pat->name = xstrdup(name);
	return pat;
}

struct helium_pattern *helium_pattern_constructor(const char *name, int line,
						  int col)
{
	struct helium_pattern *pat = xalloc(sizeof(*pat));

	pat->kind = HELIUM_PATTERN_CONSTRUCTOR;
	pat->line = line;
	pat->col = col;
	pat->name = xstrdup(name);
	return pat;
}

void helium_pattern_add_field(struct helium_pattern *pat,
			      const char *name, struct helium_pattern *field)
{
	append_ptr((void ***)&pat->fields, &pat->field_count,
		   &pat->field_capacity, field);
	append_ptr((void ***)&pat->field_names, &pat->field_name_count,
		   &pat->field_name_capacity, (void *)xstrdup(name));
}

struct helium_pattern *helium_pattern_copy(struct helium_pattern *pat)
{
	struct helium_pattern *copy;
	size_t i;

	if (!pat)
		return NULL;
	copy = xalloc(sizeof(*copy));
	copy->kind = pat->kind;
	copy->line = pat->line;
	copy->col = pat->col;
	copy->name = xstrdup(pat->name);
	copy->lit = pat->lit ? helium_literal(pat->lit->kind, pat->lit->text,
					      pat->lit->line, pat->lit->col) :
			       NULL;
	for (i = 0; i < pat->field_count; i++)
		helium_pattern_add_field(copy, pat->field_names[i],
					 helium_pattern_copy(pat->fields[i]));
	return copy;
}

void helium_pattern_free(struct helium_pattern *pat)
{
	size_t i;

	if (!pat)
		return;
	free(pat->name);
	helium_literal_free(pat->lit);
	for (i = 0; i < pat->field_count; i++)
		helium_pattern_free(pat->fields[i]);
	free(pat->fields);
	for (i = 0; i < pat->field_name_count; i++)
		free(pat->field_names[i]);
	free(pat->field_names);
	free(pat);
}

/* -------------------------------------------------------------------------- */
/* Match arms, loop bindings, parameters                                      */
/* -------------------------------------------------------------------------- */

struct helium_match_arm *helium_match_arm(struct helium_pattern *pattern,
					  struct helium_expr *expr,
					  int line, int col)
{
	struct helium_match_arm *arm = xalloc(sizeof(*arm));

	arm->pattern = pattern;
	arm->expr = expr;
	arm->line = line;
	arm->col = col;
	return arm;
}

void helium_match_arm_free(struct helium_match_arm *arm)
{
	if (!arm)
		return;
	helium_pattern_free(arm->pattern);
	helium_expr_free(arm->expr);
	free(arm);
}

struct helium_loop_binding *helium_loop_binding(const char *name,
						struct helium_type *type,
						struct helium_expr *init,
						int line, int col)
{
	struct helium_loop_binding *binding = xalloc(sizeof(*binding));

	binding->name = xstrdup(name);
	binding->type = type;
	binding->init = init;
	binding->line = line;
	binding->col = col;
	return binding;
}

void helium_loop_binding_free(struct helium_loop_binding *binding)
{
	if (!binding)
		return;
	free(binding->name);
	helium_type_free(binding->type);
	helium_expr_free(binding->init);
	free(binding);
}

struct helium_param *helium_param(const char *name, struct helium_type *type,
				  int line, int col)
{
	struct helium_param *param = xalloc(sizeof(*param));

	param->name = xstrdup(name);
	param->type = type;
	param->line = line;
	param->col = col;
	return param;
}

void helium_param_free(struct helium_param *param)
{
	if (!param)
		return;
	free(param->name);
	helium_type_free(param->type);
	free(param);
}

/* -------------------------------------------------------------------------- */
/* Type definitions                                                           */
/* -------------------------------------------------------------------------- */

struct helium_record_field *helium_record_field(const char *name,
						struct helium_type *type,
						int line, int col)
{
	struct helium_record_field *field = xalloc(sizeof(*field));

	field->name = xstrdup(name);
	field->type = type;
	field->line = line;
	field->col = col;
	return field;
}

void helium_record_field_free(struct helium_record_field *field)
{
	if (!field)
		return;
	free(field->name);
	helium_type_free(field->type);
	free(field);
}

struct helium_variant *helium_variant(const char *name, int line, int col)
{
	struct helium_variant *variant = xalloc(sizeof(*variant));

	variant->name = xstrdup(name);
	variant->line = line;
	variant->col = col;
	return variant;
}

void helium_variant_add_field(struct helium_variant *variant,
			      struct helium_record_field *field)
{
	append_ptr((void ***)&variant->fields, &variant->field_count,
		   &variant->field_capacity, field);
}

void helium_variant_add_type_arg(struct helium_variant *variant,
				 struct helium_type *arg)
{
	append_ptr((void ***)&variant->type_args, &variant->type_arg_count,
		   &variant->type_arg_capacity, arg);
}

void helium_variant_free(struct helium_variant *variant)
{
	size_t i;

	if (!variant)
		return;
	free(variant->name);
	for (i = 0; i < variant->field_count; i++)
		helium_record_field_free(variant->fields[i]);
	free(variant->fields);
	free_types(variant->type_args, variant->type_arg_count);
	free(variant->type_args);
	free(variant);
}

struct helium_type_def *helium_type_def(const char *name, int kind, int line,
					int col)
{
	struct helium_type_def *def = xalloc(sizeof(*def));

	def->name = xstrdup(name);
	def->kind = kind;
	def->line = line;
	def->col = col;
	return def;
}

void helium_type_def_add_param(struct helium_type_def *def,
			       const char *param)
{
	append_ptr((void ***)&def->params, &def->param_count,
		   &def->param_capacity, (void *)xstrdup(param));
}

void helium_type_def_add_record_field(struct helium_type_def *def,
				      struct helium_record_field *field)
{
	append_ptr((void ***)&def->u.record.fields, &def->u.record.field_count,
		   &def->u.record.field_capacity, field);
}

void helium_type_def_add_variant(struct helium_type_def *def,
				 struct helium_variant *variant)
{
	append_ptr((void ***)&def->u.adt.variants, &def->u.adt.variant_count,
		   &def->u.adt.variant_capacity, variant);
}

void helium_type_def_free(struct helium_type_def *def)
{
	size_t i;

	if (!def)
		return;
	free(def->name);
	for (i = 0; i < def->param_count; i++)
		free(def->params[i]);
	free(def->params);
	if (def->kind == HELIUM_TYPE_DEF_RECORD) {
		for (i = 0; i < def->u.record.field_count; i++)
			helium_record_field_free(def->u.record.fields[i]);
		free(def->u.record.fields);
	} else {
		for (i = 0; i < def->u.adt.variant_count; i++)
			helium_variant_free(def->u.adt.variants[i]);
		free(def->u.adt.variants);
	}
	free(def);
}

/* -------------------------------------------------------------------------- */
/* Bindings and imports                                                       */
/* -------------------------------------------------------------------------- */

struct helium_binding *helium_binding(const char *name,
				      struct helium_type *type,
				      struct helium_expr *value,
				      int line, int col)
{
	struct helium_binding *binding = xalloc(sizeof(*binding));

	binding->name = xstrdup(name);
	binding->type = type;
	binding->value = value;
	binding->line = line;
	binding->col = col;
	return binding;
}

void helium_binding_free(struct helium_binding *binding)
{
	if (!binding)
		return;
	free(binding->name);
	helium_type_free(binding->type);
	helium_expr_free(binding->value);
	free(binding);
}

struct helium_import *helium_import(const char *path, int line, int col)
{
	struct helium_import *import = xalloc(sizeof(*import));

	import->path = xstrdup(path);
	import->line = line;
	import->col = col;
	return import;
}

void helium_import_free(struct helium_import *import)
{
	if (!import)
		return;
	free(import->path);
	free(import);
}

/* -------------------------------------------------------------------------- */
/* Top-level declarations                                                     */
/* -------------------------------------------------------------------------- */

struct helium_top_decl *helium_decl_import(struct helium_import *import,
					   int line, int col)
{
	struct helium_top_decl *decl = xalloc(sizeof(*decl));

	decl->kind = HELIUM_DECL_IMPORT;
	decl->line = line;
	decl->col = col;
	decl->u.import = import;
	return decl;
}

struct helium_top_decl *helium_decl_type(struct helium_type_def *type_def)
{
	struct helium_top_decl *decl = xalloc(sizeof(*decl));

	decl->kind = HELIUM_DECL_TYPE;
	decl->line = type_def->line;
	decl->col = type_def->col;
	decl->u.type_def = type_def;
	return decl;
}

struct helium_top_decl *helium_decl_binding(struct helium_binding *binding)
{
	struct helium_top_decl *decl = xalloc(sizeof(*decl));

	decl->kind = HELIUM_DECL_BINDING;
	decl->line = binding->line;
	decl->col = binding->col;
	decl->u.binding = binding;
	return decl;
}

struct helium_top_decl *helium_decl_foreign(const char *name,
					    char **type_params,
					    size_t type_param_count,
					    struct helium_type *type,
					    int line, int col)
{
	struct helium_top_decl *decl = xalloc(sizeof(*decl));
	char **params = NULL;
	size_t i;

	decl->kind = HELIUM_DECL_FOREIGN;
	decl->line = line;
	decl->col = col;
	decl->u.foreign.name = xstrdup(name);
	decl->u.foreign.type = type;
	decl->u.foreign.type_params = NULL;
	decl->u.foreign.type_param_count = type_param_count;

	if (type_param_count) {
		params = malloc(type_param_count * sizeof(*params));
		if (!params)
			abort();
		for (i = 0; i < type_param_count; i++)
			params[i] = xstrdup(type_params[i]);
		decl->u.foreign.type_params = params;
	}

	return decl;
}

void helium_top_decl_free(struct helium_top_decl *decl)
{
	if (!decl)
		return;
	switch (decl->kind) {
	case HELIUM_DECL_IMPORT:
		helium_import_free(decl->u.import);
		break;
	case HELIUM_DECL_TYPE:
		helium_type_def_free(decl->u.type_def);
		break;
	case HELIUM_DECL_BINDING:
		helium_binding_free(decl->u.binding);
		break;
	case HELIUM_DECL_FOREIGN: {
		size_t i;

		free(decl->u.foreign.name);
		helium_type_free(decl->u.foreign.type);
		for (i = 0; i < decl->u.foreign.type_param_count; i++)
			free(decl->u.foreign.type_params[i]);
		free(decl->u.foreign.type_params);
		break;
	}
	}
	free(decl);
}

/* -------------------------------------------------------------------------- */
/* Module                                                                     */
/* -------------------------------------------------------------------------- */

struct helium_module *helium_module(const char *name, int line, int col)
{
	struct helium_module *module = xalloc(sizeof(*module));

	module->name = xstrdup(name);
	module->line = line;
	module->col = col;
	return module;
}

void helium_module_add_decl(struct helium_module *module,
			    struct helium_top_decl *decl)
{
	append_ptr((void ***)&module->decls, &module->decl_count,
		   &module->decl_capacity, decl);
}

void helium_module_free(struct helium_module *module)
{
	size_t i;

	if (!module)
		return;
	free(module->name);
	for (i = 0; i < module->decl_count; i++)
		helium_top_decl_free(module->decls[i]);
	free(module->decls);
	free(module);
}
