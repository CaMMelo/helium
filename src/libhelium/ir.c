/* SPDX-License-Identifier: TBD */
/*
 * ir.c - Monomorphic IR constructors, destructors, and printer.
 */

#include "ir.h"

#include <stdlib.h>
#include <string.h>

#include "types.h"

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

/* -------------------------------------------------------------------------- */
/* Program                                                                    */
/* -------------------------------------------------------------------------- */

struct helium_ir_program *helium_ir_program_new(void)
{
	return xalloc(sizeof(struct helium_ir_program));
}

void helium_ir_program_add_function(struct helium_ir_program *prog,
				    struct helium_ir_function *func)
{
	append_ptr((void ***)&prog->functions, &prog->function_count,
		   &prog->function_capacity, func);
}

void helium_ir_program_add_type(struct helium_ir_program *prog,
				struct helium_ir_type *type)
{
	append_ptr((void ***)&prog->types, &prog->type_count,
		   &prog->type_capacity, type);
}

void helium_ir_program_free(struct helium_ir_program *prog)
{
	size_t i;

	if (!prog)
		return;
	for (i = 0; i < prog->function_count; i++)
		helium_ir_function_free(prog->functions[i]);
	free(prog->functions);
	for (i = 0; i < prog->type_count; i++)
		helium_ir_type_free(prog->types[i]);
	free(prog->types);
	free(prog->main_name);
	free(prog);
}

/* -------------------------------------------------------------------------- */
/* Function and parameter                                                     */
/* -------------------------------------------------------------------------- */

struct helium_ir_function *helium_ir_function_new(const char *name,
					  struct helium_type *ret_type,
					  int line, int col)
{
	struct helium_ir_function *func = xalloc(sizeof(*func));

	func->name = xstrdup(name);
	func->ret_type = ret_type;
	func->line = line;
	func->col = col;
	return func;
}

void helium_ir_function_add_param(struct helium_ir_function *func,
				  struct helium_ir_param *param)
{
	append_ptr((void ***)&func->params, &func->param_count,
		   &func->param_capacity, param);
}

void helium_ir_function_set_body(struct helium_ir_function *func,
				 struct helium_ir_block *body)
{
	func->body = body;
}

void helium_ir_function_add_capture(struct helium_ir_function *func,
				    const char *name,
				    struct helium_type *type)
{
	if (func->capture_count == func->capture_capacity) {
		size_t newcap = func->capture_capacity ? func->capture_capacity * 2 : 4;
		char **names = realloc(func->capture_names,
				       newcap * sizeof(*names));
		struct helium_type **types = realloc(func->capture_types,
						     newcap * sizeof(*types));

		if (!names || !types)
			abort();
		func->capture_names = names;
		func->capture_types = types;
		func->capture_capacity = newcap;
	}
	func->capture_names[func->capture_count] = xstrdup(name);
	func->capture_types[func->capture_count] = type;
	func->capture_count++;
}

void helium_ir_function_free(struct helium_ir_function *func)
{
	size_t i;

	if (!func)
		return;
	free(func->name);
	for (i = 0; i < func->param_count; i++)
		helium_ir_param_free(func->params[i]);
	free(func->params);
	helium_type_free(func->ret_type);
	helium_ir_block_free(func->body);
	for (i = 0; i < func->capture_count; i++) {
		free(func->capture_names[i]);
		helium_type_free(func->capture_types[i]);
	}
	free(func->capture_names);
	free(func->capture_types);
	free(func);
}

struct helium_ir_param *helium_ir_param_new(const char *name,
					    struct helium_type *type,
					    int line, int col)
{
	struct helium_ir_param *param = xalloc(sizeof(*param));

	param->name = xstrdup(name);
	param->type = type;
	param->line = line;
	param->col = col;
	return param;
}

void helium_ir_param_free(struct helium_ir_param *param)
{
	if (!param)
		return;
	free(param->name);
	helium_type_free(param->type);
	free(param);
}

/* -------------------------------------------------------------------------- */
/* Type, field, variant                                                       */
/* -------------------------------------------------------------------------- */

struct helium_ir_type *helium_ir_type_new(const char *name, int kind,
					  int line, int col)
{
	struct helium_ir_type *type = xalloc(sizeof(*type));

	type->name = xstrdup(name);
	type->kind = kind;
	type->line = line;
	type->col = col;
	return type;
}

void helium_ir_type_add_field(struct helium_ir_type *type,
			      struct helium_ir_field *field)
{
	append_ptr((void ***)&type->fields, &type->field_count,
		   &type->field_capacity, field);
}

void helium_ir_type_add_variant(struct helium_ir_type *type,
				struct helium_ir_variant *variant)
{
	append_ptr((void ***)&type->variants, &type->variant_count,
		   &type->variant_capacity, variant);
}

void helium_ir_type_free(struct helium_ir_type *type)
{
	size_t i;

	if (!type)
		return;
	free(type->name);
	for (i = 0; i < type->field_count; i++)
		helium_ir_field_free(type->fields[i]);
	free(type->fields);
	for (i = 0; i < type->variant_count; i++)
		helium_ir_variant_free(type->variants[i]);
	free(type->variants);
	free(type);
}

struct helium_ir_field *helium_ir_field_new(const char *name,
					    struct helium_type *type,
					    int line, int col)
{
	struct helium_ir_field *field = xalloc(sizeof(*field));

	field->name = xstrdup(name);
	field->type = type;
	field->line = line;
	field->col = col;
	return field;
}

void helium_ir_field_free(struct helium_ir_field *field)
{
	if (!field)
		return;
	free(field->name);
	helium_type_free(field->type);
	free(field);
}

struct helium_ir_variant *helium_ir_variant_new(const char *name, int line,
						int col)
{
	struct helium_ir_variant *variant = xalloc(sizeof(*variant));

	variant->name = xstrdup(name);
	variant->line = line;
	variant->col = col;
	return variant;
}

void helium_ir_variant_add_field(struct helium_ir_variant *variant,
				 struct helium_ir_field *field)
{
	append_ptr((void ***)&variant->fields, &variant->field_count,
		   &variant->field_capacity, field);
}

void helium_ir_variant_free(struct helium_ir_variant *variant)
{
	size_t i;

	if (!variant)
		return;
	free(variant->name);
	for (i = 0; i < variant->field_count; i++)
		helium_ir_field_free(variant->fields[i]);
	free(variant->fields);
	free(variant);
}

/* -------------------------------------------------------------------------- */
/* Block                                                                      */
/* -------------------------------------------------------------------------- */

struct helium_ir_block *helium_ir_block_new(void)
{
	return xalloc(sizeof(struct helium_ir_block));
}

void helium_ir_block_add_instr(struct helium_ir_block *block,
			       struct helium_ir_instr *instr)
{
	append_ptr((void ***)&block->instrs, &block->instr_count,
		   &block->instr_capacity, instr);
}

void helium_ir_block_free(struct helium_ir_block *block)
{
	size_t i;

	if (!block)
		return;
	for (i = 0; i < block->instr_count; i++)
		helium_ir_instr_free(block->instrs[i]);
	free(block->instrs);
	free(block);
}

/* -------------------------------------------------------------------------- */
/* Instructions                                                               */
/* -------------------------------------------------------------------------- */

static struct helium_ir_instr *instr_new(int kind, int line, int col)
{
	struct helium_ir_instr *instr = xalloc(sizeof(*instr));

	instr->kind = kind;
	instr->line = line;
	instr->col = col;
	return instr;
}

struct helium_ir_instr *helium_ir_instr_literal(struct helium_literal *lit,
					int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_LITERAL,
						  line, col);

	instr->u.literal.lit = lit;
	return instr;
}

struct helium_ir_instr *helium_ir_instr_ident(const char *name, int line,
					      int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_IDENT,
						  line, col);

	instr->u.ident.name = xstrdup(name);
	return instr;
}

struct helium_ir_instr *helium_ir_instr_call(const char *name, int line,
					     int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_CALL,
						  line, col);

	instr->u.call.name = xstrdup(name);
	return instr;
}

struct helium_ir_instr *helium_ir_instr_tail_call(const char *name, int line,
						  int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_TAIL_CALL,
						  line, col);

	instr->u.call.name = xstrdup(name);
	return instr;
}

struct helium_ir_instr *helium_ir_instr_foreign_call(const char *name, int line,
						     int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_FOREIGN_CALL,
						  line, col);

	instr->u.foreign_call.name = xstrdup(name);
	return instr;
}

void helium_ir_call_add_arg(struct helium_ir_instr *call,
			    struct helium_ir_instr *arg)
{
	switch (call->kind) {
	case HELIUM_IR_INSTR_CALL:
	case HELIUM_IR_INSTR_TAIL_CALL:
		append_ptr((void ***)&call->u.call.args, &call->u.call.arg_count,
			   &call->u.call.arg_capacity, arg);
		break;
	case HELIUM_IR_INSTR_FOREIGN_CALL:
		append_ptr((void ***)&call->u.foreign_call.args,
			   &call->u.foreign_call.arg_count,
			   &call->u.foreign_call.arg_capacity, arg);
		break;
	default:
		break;
	}
}

struct helium_ir_instr *helium_ir_instr_record_alloc(const char *type_name,
					     int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_RECORD_ALLOC,
						  line, col);

	instr->u.record_alloc.type_name = xstrdup(type_name);
	return instr;
}

void helium_ir_record_alloc_add_field(struct helium_ir_instr *alloc,
				      struct helium_ir_instr *value)
{
	append_ptr((void ***)&alloc->u.record_alloc.field_values,
		   &alloc->u.record_alloc.field_count,
		   &alloc->u.record_alloc.field_capacity, value);
}

struct helium_ir_instr *helium_ir_instr_record_get(struct helium_ir_instr *object,
					   const char *field_name,
					   const char *type_name,
					   int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_RECORD_GET,
						  line, col);

	instr->u.record_get.object = object;
	instr->u.record_get.field_name = xstrdup(field_name);
	instr->u.record_get.type_name = xstrdup(type_name);
	return instr;
}

struct helium_ir_instr *helium_ir_instr_record_set(struct helium_ir_instr *object,
					   const char *field_name,
					   struct helium_ir_instr *value,
					   int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_RECORD_SET,
						  line, col);

	instr->u.record_set.object = object;
	instr->u.record_set.field_name = xstrdup(field_name);
	instr->u.record_set.value = value;
	return instr;
}

struct helium_ir_instr *helium_ir_instr_variant_alloc(const char *type_name,
					      const char *variant_name,
					      int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_VARIANT_ALLOC,
						  line, col);

	instr->u.variant_alloc.type_name = xstrdup(type_name);
	instr->u.variant_alloc.variant_name = xstrdup(variant_name);
	return instr;
}

void helium_ir_variant_alloc_add_field(struct helium_ir_instr *alloc,
				       struct helium_ir_instr *value)
{
	append_ptr((void ***)&alloc->u.variant_alloc.field_values,
		   &alloc->u.variant_alloc.field_count,
		   &alloc->u.variant_alloc.field_capacity, value);
}

struct helium_ir_instr *helium_ir_instr_array_alloc(struct helium_type *elem_type,
					    long size, int line,
					    int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_ARRAY_ALLOC,
						  line, col);

	instr->u.array_alloc.elem_type = elem_type;
	instr->u.array_alloc.size = size;
	return instr;
}

void helium_ir_array_alloc_add_item(struct helium_ir_instr *alloc,
				    struct helium_ir_instr *item)
{
	append_ptr((void ***)&alloc->u.array_alloc.items,
		   &alloc->u.array_alloc.item_count,
		   &alloc->u.array_alloc.item_capacity, item);
}

struct helium_ir_instr *helium_ir_instr_array_get(struct helium_ir_instr *array,
					  struct helium_ir_instr *index,
					  int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_ARRAY_GET,
						  line, col);

	instr->u.array_get.array = array;
	instr->u.array_get.index = index;
	return instr;
}

struct helium_ir_instr *helium_ir_instr_if(struct helium_ir_instr *cond,
					   int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_IF,
						  line, col);

	instr->u.if_expr.cond = cond;
	return instr;
}

void helium_ir_if_set_then(struct helium_ir_instr *instr,
			   struct helium_ir_block *block)
{
	if (instr->kind == HELIUM_IR_INSTR_IF)
		instr->u.if_expr.then_branch = block;
}

void helium_ir_if_set_else(struct helium_ir_instr *instr,
			   struct helium_ir_block *block)
{
	if (instr->kind == HELIUM_IR_INSTR_IF)
		instr->u.if_expr.else_branch = block;
}

struct helium_ir_instr *helium_ir_instr_match(struct helium_ir_instr *value,
					      int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_MATCH,
						  line, col);

	instr->u.match.value = value;
	return instr;
}

void helium_ir_match_add_arm(struct helium_ir_instr *match,
			     struct helium_ir_match_arm *arm)
{
	append_ptr((void ***)&match->u.match.arms, &match->u.match.arm_count,
		   &match->u.match.arm_capacity, arm);
}

struct helium_ir_match_arm *helium_ir_match_arm(struct helium_pattern *pattern,
						struct helium_ir_block *body,
						int line, int col)
{
	struct helium_ir_match_arm *arm = xalloc(sizeof(*arm));

	arm->pattern = pattern;
	arm->body = body;
	arm->line = line;
	arm->col = col;
	return arm;
}

void helium_ir_match_arm_free(struct helium_ir_match_arm *arm)
{
	if (!arm)
		return;
	helium_pattern_free(arm->pattern);
	helium_ir_block_free(arm->body);
	free(arm);
}

struct helium_ir_instr *helium_ir_instr_loop(int line, int col)
{
	return instr_new(HELIUM_IR_INSTR_LOOP, line, col);
}

void helium_ir_loop_add_binding(struct helium_ir_instr *loop,
				struct helium_ir_let_binding *binding)
{
	append_ptr((void ***)&loop->u.loop.bindings, &loop->u.loop.binding_count,
		   &loop->u.loop.binding_capacity, binding);
}

void helium_ir_loop_set_body(struct helium_ir_instr *loop,
			     struct helium_ir_block *body)
{
	if (loop->kind == HELIUM_IR_INSTR_LOOP)
		loop->u.loop.body = body;
}

struct helium_ir_instr *helium_ir_instr_recur(int line, int col)
{
	return instr_new(HELIUM_IR_INSTR_RECUR, line, col);
}

void helium_ir_recur_add_arg(struct helium_ir_instr *recur,
			     struct helium_ir_instr *arg)
{
	append_ptr((void ***)&recur->u.recur.args, &recur->u.recur.arg_count,
		   &recur->u.recur.arg_capacity, arg);
}

struct helium_ir_instr *helium_ir_instr_let(const char *name,
					    struct helium_type *type,
					    struct helium_ir_instr *value,
					    int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_LET,
						  line, col);

	instr->u.let.name = xstrdup(name);
	instr->u.let.type = type;
	instr->u.let.value = value;
	return instr;
}

struct helium_ir_instr *helium_ir_instr_return(struct helium_ir_instr *value,
					       int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_RETURN,
						  line, col);

	instr->u.ret.value = value;
	return instr;
}

struct helium_ir_instr *helium_ir_instr_retain(struct helium_ir_instr *value,
					       int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_RETAIN,
						  line, col);

	instr->u.retain.value = value;
	return instr;
}

struct helium_ir_instr *helium_ir_instr_release(struct helium_ir_instr *value,
						int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_RELEASE,
						  line, col);

	instr->u.release.value = value;
	return instr;
}

struct helium_ir_instr *helium_ir_instr_block(int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_BLOCK,
						  line, col);

	instr->u.block.block = helium_ir_block_new();
	return instr;
}

void helium_ir_instr_block_add_instr(struct helium_ir_instr *block,
				     struct helium_ir_instr *instr)
{
	if (block->kind == HELIUM_IR_INSTR_BLOCK)
		helium_ir_block_add_instr(block->u.block.block, instr);
}

struct helium_ir_instr *helium_ir_instr_binary(int op,
					       struct helium_ir_instr *left,
					       struct helium_ir_instr *right,
					       int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_BINARY,
						  line, col);

	instr->u.binary.op = op;
	instr->u.binary.left = left;
	instr->u.binary.right = right;
	return instr;
}

struct helium_ir_instr *helium_ir_instr_unary(int op,
				      struct helium_ir_instr *operand,
				      int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_UNARY,
						  line, col);

	instr->u.unary.op = op;
	instr->u.unary.operand = operand;
	return instr;
}

struct helium_ir_instr *helium_ir_instr_fstring(int line, int col)
{
	return instr_new(HELIUM_IR_INSTR_FSTRING, line, col);
}

struct helium_ir_instr *helium_ir_instr_closure_alloc(const char *func_name,
				      int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_CLOSURE_ALLOC,
					  line, col);

	instr->u.closure_alloc.func_name = xstrdup(func_name);
	return instr;
}

void helium_ir_closure_alloc_add_capture(struct helium_ir_instr *alloc,
					 const char *name,
					 struct helium_ir_instr *value)
{
	if (alloc->kind != HELIUM_IR_INSTR_CLOSURE_ALLOC)
		return;
	append_ptr((void ***)&alloc->u.closure_alloc.capture_names,
		   &alloc->u.closure_alloc.name_count,
		   &alloc->u.closure_alloc.name_capacity,
		   xstrdup(name));
	append_ptr((void ***)&alloc->u.closure_alloc.capture_values,
		   &alloc->u.closure_alloc.value_count,
		   &alloc->u.closure_alloc.value_capacity,
		   value);
}

struct helium_ir_instr *helium_ir_instr_closure_call(
				struct helium_ir_instr *closure,
				int line, int col)
{
	struct helium_ir_instr *instr = instr_new(HELIUM_IR_INSTR_CLOSURE_CALL,
					  line, col);

	instr->u.closure_call.closure = closure;
	return instr;
}

void helium_ir_closure_call_add_arg(struct helium_ir_instr *call,
				    struct helium_ir_instr *arg)
{
	if (call->kind != HELIUM_IR_INSTR_CLOSURE_CALL)
		return;
	append_ptr((void ***)&call->u.closure_call.args,
		   &call->u.closure_call.arg_count,
		   &call->u.closure_call.arg_capacity, arg);
}

void helium_ir_fstring_add_text(struct helium_ir_instr *fstring,
				const char *text, int line, int col)
{
	struct helium_ir_fstring_part *part;

	if (fstring->kind != HELIUM_IR_INSTR_FSTRING)
		return;
	part = xalloc(sizeof(*part));
	part->is_expr = 0;
	part->u.text = xstrdup(text);
	part->line = line;
	part->col = col;
	append_ptr((void ***)&fstring->u.fstring.parts,
		   &fstring->u.fstring.part_count,
		   &fstring->u.fstring.part_capacity, part);
}

void helium_ir_fstring_add_expr(struct helium_ir_instr *fstring,
				struct helium_ir_instr *expr)
{
	struct helium_ir_fstring_part *part;

	if (fstring->kind != HELIUM_IR_INSTR_FSTRING)
		return;
	part = xalloc(sizeof(*part));
	part->is_expr = 1;
	part->u.expr = expr;
	part->line = expr->line;
	part->col = expr->col;
	append_ptr((void ***)&fstring->u.fstring.parts,
		   &fstring->u.fstring.part_count,
		   &fstring->u.fstring.part_capacity, part);
}

struct helium_ir_let_binding *helium_ir_let_binding_new(const char *name,
						struct helium_type *type,
						struct helium_ir_instr *value,
						int line, int col)
{
	struct helium_ir_let_binding *binding = xalloc(sizeof(*binding));

	binding->name = xstrdup(name);
	binding->type = type;
	binding->value = value;
	binding->line = line;
	binding->col = col;
	return binding;
}

void helium_ir_let_binding_free(struct helium_ir_let_binding *binding)
{
	if (!binding)
		return;
	free(binding->name);
	helium_type_free(binding->type);
	helium_ir_instr_free(binding->value);
	free(binding);
}

void helium_ir_instr_free(struct helium_ir_instr *instr)
{
	size_t i;

	if (!instr)
		return;
	helium_type_free(instr->type);
	switch (instr->kind) {
	case HELIUM_IR_INSTR_LITERAL:
		helium_literal_free(instr->u.literal.lit);
		break;
	case HELIUM_IR_INSTR_IDENT:
		free(instr->u.ident.name);
		break;
	case HELIUM_IR_INSTR_CALL:
	case HELIUM_IR_INSTR_TAIL_CALL:
		free(instr->u.call.name);
		for (i = 0; i < instr->u.call.arg_count; i++)
			helium_ir_instr_free(instr->u.call.args[i]);
		free(instr->u.call.args);
		break;
	case HELIUM_IR_INSTR_FOREIGN_CALL:
		free(instr->u.foreign_call.name);
		for (i = 0; i < instr->u.foreign_call.arg_count; i++)
			helium_ir_instr_free(instr->u.foreign_call.args[i]);
		free(instr->u.foreign_call.args);
		break;
	case HELIUM_IR_INSTR_RECORD_ALLOC:
		free(instr->u.record_alloc.type_name);
		for (i = 0; i < instr->u.record_alloc.field_count; i++)
			helium_ir_instr_free(instr->u.record_alloc.field_values[i]);
		free(instr->u.record_alloc.field_values);
		break;
	case HELIUM_IR_INSTR_RECORD_GET:
		helium_ir_instr_free(instr->u.record_get.object);
		free(instr->u.record_get.field_name);
		free(instr->u.record_get.type_name);
		break;
	case HELIUM_IR_INSTR_RECORD_SET:
		helium_ir_instr_free(instr->u.record_set.object);
		free(instr->u.record_set.field_name);
		helium_ir_instr_free(instr->u.record_set.value);
		break;
	case HELIUM_IR_INSTR_VARIANT_ALLOC:
		free(instr->u.variant_alloc.type_name);
		free(instr->u.variant_alloc.variant_name);
		for (i = 0; i < instr->u.variant_alloc.field_count; i++)
			helium_ir_instr_free(instr->u.variant_alloc.field_values[i]);
		free(instr->u.variant_alloc.field_values);
		break;
	case HELIUM_IR_INSTR_ARRAY_ALLOC:
		helium_type_free(instr->u.array_alloc.elem_type);
		for (i = 0; i < instr->u.array_alloc.item_count; i++)
			helium_ir_instr_free(instr->u.array_alloc.items[i]);
		free(instr->u.array_alloc.items);
		break;
	case HELIUM_IR_INSTR_ARRAY_GET:
		helium_ir_instr_free(instr->u.array_get.array);
		helium_ir_instr_free(instr->u.array_get.index);
		break;
	case HELIUM_IR_INSTR_IF:
		helium_ir_instr_free(instr->u.if_expr.cond);
		helium_ir_block_free(instr->u.if_expr.then_branch);
		helium_ir_block_free(instr->u.if_expr.else_branch);
		break;
	case HELIUM_IR_INSTR_MATCH:
		helium_ir_instr_free(instr->u.match.value);
		for (i = 0; i < instr->u.match.arm_count; i++)
			helium_ir_match_arm_free(instr->u.match.arms[i]);
		free(instr->u.match.arms);
		break;
	case HELIUM_IR_INSTR_LOOP:
		for (i = 0; i < instr->u.loop.binding_count; i++)
			helium_ir_let_binding_free(instr->u.loop.bindings[i]);
		free(instr->u.loop.bindings);
		helium_ir_block_free(instr->u.loop.body);
		break;
	case HELIUM_IR_INSTR_RECUR:
		for (i = 0; i < instr->u.recur.arg_count; i++)
			helium_ir_instr_free(instr->u.recur.args[i]);
		free(instr->u.recur.args);
		break;
	case HELIUM_IR_INSTR_LET:
		free(instr->u.let.name);
		helium_type_free(instr->u.let.type);
		helium_ir_instr_free(instr->u.let.value);
		break;
	case HELIUM_IR_INSTR_RETURN:
		helium_ir_instr_free(instr->u.ret.value);
		break;
	case HELIUM_IR_INSTR_RETAIN:
		helium_ir_instr_free(instr->u.retain.value);
		break;
	case HELIUM_IR_INSTR_RELEASE:
		helium_ir_instr_free(instr->u.release.value);
		break;
	case HELIUM_IR_INSTR_BLOCK:
		helium_ir_block_free(instr->u.block.block);
		break;
	case HELIUM_IR_INSTR_BINARY:
		helium_ir_instr_free(instr->u.binary.left);
		helium_ir_instr_free(instr->u.binary.right);
		break;
	case HELIUM_IR_INSTR_UNARY:
		helium_ir_instr_free(instr->u.unary.operand);
		break;
	case HELIUM_IR_INSTR_FSTRING:
		for (i = 0; i < instr->u.fstring.part_count; i++) {
			struct helium_ir_fstring_part *p = instr->u.fstring.parts[i];

			if (!p->is_expr)
				free(p->u.text);
			else
				helium_ir_instr_free(p->u.expr);
			free(p);
		}
		free(instr->u.fstring.parts);
		break;
	case HELIUM_IR_INSTR_CLOSURE_ALLOC:
		free(instr->u.closure_alloc.func_name);
		for (i = 0; i < instr->u.closure_alloc.name_count; i++)
			free(instr->u.closure_alloc.capture_names[i]);
		for (i = 0; i < instr->u.closure_alloc.value_count; i++)
			helium_ir_instr_free(instr->u.closure_alloc.capture_values[i]);
		free(instr->u.closure_alloc.capture_names);
		free(instr->u.closure_alloc.capture_values);
		break;
	case HELIUM_IR_INSTR_CLOSURE_CALL:
		helium_ir_instr_free(instr->u.closure_call.closure);
		for (i = 0; i < instr->u.closure_call.arg_count; i++)
			helium_ir_instr_free(instr->u.closure_call.args[i]);
		free(instr->u.closure_call.args);
		break;
	}
	free(instr);
}

/* -------------------------------------------------------------------------- */
/* Printer                                                                    */
/* -------------------------------------------------------------------------- */

static void print_type(struct helium_type *type, FILE *out)
{
	char buf[256];

	if (!type) {
		fprintf(out, "<?>");
		return;
	}
	helium_type_to_string(type, buf, sizeof(buf));
	fprintf(out, "%s", buf);
}

static void print_indent(int depth, FILE *out)
{
	int i;

	for (i = 0; i < depth; i++)
		fprintf(out, "    ");
}

static void print_instr(struct helium_ir_instr *instr, int depth, FILE *out);

static void print_block(struct helium_ir_block *block, int depth, FILE *out)
{
	size_t i;

	if (!block) {
		print_indent(depth, out);
		fprintf(out, "<null block>\n");
		return;
	}
	for (i = 0; i < block->instr_count; i++) {
		print_instr(block->instrs[i], depth, out);
	}
}

static void print_instr(struct helium_ir_instr *instr, int depth, FILE *out)
{
	size_t i;

	if (!instr) {
		print_indent(depth, out);
		fprintf(out, "<null>\n");
		return;
	}

	switch (instr->kind) {
	case HELIUM_IR_INSTR_LITERAL:
		print_indent(depth, out);
		fprintf(out, "literal %s\n",
			instr->u.literal.lit ? instr->u.literal.lit->text : "?");
		break;
	case HELIUM_IR_INSTR_IDENT:
		print_indent(depth, out);
		fprintf(out, "ident %s\n", instr->u.ident.name);
		break;
	case HELIUM_IR_INSTR_CALL:
		print_indent(depth, out);
		fprintf(out, "call %s\n", instr->u.call.name);
		for (i = 0; i < instr->u.call.arg_count; i++)
			print_instr(instr->u.call.args[i], depth + 1, out);
		break;
	case HELIUM_IR_INSTR_TAIL_CALL:
		print_indent(depth, out);
		fprintf(out, "tailcall %s\n", instr->u.call.name);
		for (i = 0; i < instr->u.call.arg_count; i++)
			print_instr(instr->u.call.args[i], depth + 1, out);
		break;
	case HELIUM_IR_INSTR_FOREIGN_CALL:
		print_indent(depth, out);
		fprintf(out, "foreign_call %s\n", instr->u.foreign_call.name);
		for (i = 0; i < instr->u.foreign_call.arg_count; i++)
			print_instr(instr->u.foreign_call.args[i], depth + 1, out);
		break;
	case HELIUM_IR_INSTR_RECORD_ALLOC:
		print_indent(depth, out);
		fprintf(out, "record_alloc %s\n", instr->u.record_alloc.type_name);
		for (i = 0; i < instr->u.record_alloc.field_count; i++)
			print_instr(instr->u.record_alloc.field_values[i],
				    depth + 1, out);
		break;
	case HELIUM_IR_INSTR_RECORD_GET:
		print_indent(depth, out);
		fprintf(out, "record_get %s\n", instr->u.record_get.field_name);
		print_instr(instr->u.record_get.object, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_RECORD_SET:
		print_indent(depth, out);
		fprintf(out, "record_set %s\n", instr->u.record_set.field_name);
		print_instr(instr->u.record_set.object, depth + 1, out);
		print_instr(instr->u.record_set.value, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_VARIANT_ALLOC:
		print_indent(depth, out);
		fprintf(out, "variant_alloc %s::%s\n",
			instr->u.variant_alloc.type_name,
			instr->u.variant_alloc.variant_name);
		for (i = 0; i < instr->u.variant_alloc.field_count; i++)
			print_instr(instr->u.variant_alloc.field_values[i],
				    depth + 1, out);
		break;
	case HELIUM_IR_INSTR_ARRAY_ALLOC:
		print_indent(depth, out);
		fprintf(out, "array_alloc [");
		print_type(instr->u.array_alloc.elem_type, out);
		fprintf(out, "; %ld]\n", instr->u.array_alloc.size);
		for (i = 0; i < instr->u.array_alloc.item_count; i++)
			print_instr(instr->u.array_alloc.items[i], depth + 1, out);
		break;
	case HELIUM_IR_INSTR_ARRAY_GET:
		print_indent(depth, out);
		fprintf(out, "array_get\n");
		print_instr(instr->u.array_get.array, depth + 1, out);
		print_instr(instr->u.array_get.index, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_IF:
		print_indent(depth, out);
		fprintf(out, "if\n");
		print_instr(instr->u.if_expr.cond, depth + 1, out);
		print_indent(depth, out);
		fprintf(out, "then\n");
		print_block(instr->u.if_expr.then_branch, depth + 1, out);
		print_indent(depth, out);
		fprintf(out, "else\n");
		print_block(instr->u.if_expr.else_branch, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_MATCH:
		print_indent(depth, out);
		fprintf(out, "match\n");
		print_instr(instr->u.match.value, depth + 1, out);
		for (i = 0; i < instr->u.match.arm_count; i++) {
			struct helium_ir_match_arm *arm = instr->u.match.arms[i];

			print_indent(depth + 1, out);
			fprintf(out, "arm %s\n",
				arm->pattern ? arm->pattern->name : "?");
			print_block(arm->body, depth + 2, out);
		}
		break;
	case HELIUM_IR_INSTR_LOOP:
		print_indent(depth, out);
		fprintf(out, "loop\n");
		for (i = 0; i < instr->u.loop.binding_count; i++) {
			struct helium_ir_let_binding *b = instr->u.loop.bindings[i];

			print_indent(depth + 1, out);
			fprintf(out, "binding %s : ", b->name);
			print_type(b->type, out);
			fprintf(out, "\n");
			print_instr(b->value, depth + 2, out);
		}
		print_indent(depth, out);
		fprintf(out, "do\n");
		print_block(instr->u.loop.body, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_RECUR:
		print_indent(depth, out);
		fprintf(out, "recur\n");
		for (i = 0; i < instr->u.recur.arg_count; i++)
			print_instr(instr->u.recur.args[i], depth + 1, out);
		break;
	case HELIUM_IR_INSTR_LET:
		print_indent(depth, out);
		fprintf(out, "let %s : ", instr->u.let.name);
		print_type(instr->u.let.type, out);
		fprintf(out, "\n");
		print_instr(instr->u.let.value, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_RETURN:
		print_indent(depth, out);
		fprintf(out, "return\n");
		print_instr(instr->u.ret.value, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_RETAIN:
		print_indent(depth, out);
		fprintf(out, "retain\n");
		print_instr(instr->u.retain.value, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_RELEASE:
		print_indent(depth, out);
		fprintf(out, "release\n");
		print_instr(instr->u.release.value, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_BLOCK:
		print_indent(depth, out);
		fprintf(out, "block\n");
		print_block(instr->u.block.block, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_BINARY:
		print_indent(depth, out);
		fprintf(out, "binary %d\n", instr->u.binary.op);
		print_instr(instr->u.binary.left, depth + 1, out);
		print_instr(instr->u.binary.right, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_UNARY:
		print_indent(depth, out);
		fprintf(out, "unary %d\n", instr->u.unary.op);
		print_instr(instr->u.unary.operand, depth + 1, out);
		break;
	case HELIUM_IR_INSTR_FSTRING:
		print_indent(depth, out);
		fprintf(out, "fstring\n");
		break;
	case HELIUM_IR_INSTR_CLOSURE_ALLOC:
		print_indent(depth, out);
		fprintf(out, "closure_alloc %s",
			instr->u.closure_alloc.func_name);
		for (i = 0; i < instr->u.closure_alloc.name_count; i++)
			fprintf(out, " %s",
				instr->u.closure_alloc.capture_names[i]);
		fprintf(out, "\n");
		for (i = 0; i < instr->u.closure_alloc.value_count; i++)
			print_instr(instr->u.closure_alloc.capture_values[i],
				    depth + 1, out);
		break;
	case HELIUM_IR_INSTR_CLOSURE_CALL:
		print_indent(depth, out);
		fprintf(out, "closure_call\n");
		print_instr(instr->u.closure_call.closure, depth + 1, out);
		for (i = 0; i < instr->u.closure_call.arg_count; i++)
			print_instr(instr->u.closure_call.args[i], depth + 1, out);
		break;
	}
}

void helium_ir_program_print(struct helium_ir_program *prog, FILE *out)
{
	size_t i;

	if (!prog) {
		fprintf(out, "<null program>\n");
		return;
	}

	fprintf(out, "program main=%s\n",
		prog->main_name ? prog->main_name : "?");

	for (i = 0; i < prog->type_count; i++) {
		struct helium_ir_type *type = prog->types[i];
		size_t j;

		fprintf(out, "type %s\n", type->name);
		if (type->kind == HELIUM_IR_TYPE_RECORD) {
			for (j = 0; j < type->field_count; j++) {
				struct helium_ir_field *f = type->fields[j];

				fprintf(out, "    field %s : ", f->name);
				print_type(f->type, out);
				fprintf(out, "\n");
			}
		} else {
			for (j = 0; j < type->variant_count; j++) {
				struct helium_ir_variant *v = type->variants[j];
				size_t k;

				fprintf(out, "    variant %s\n", v->name);
				for (k = 0; k < v->field_count; k++) {
					struct helium_ir_field *f = v->fields[k];

					fprintf(out, "        field %s : ", f->name);
					print_type(f->type, out);
					fprintf(out, "\n");
				}
			}
		}
	}

	for (i = 0; i < prog->function_count; i++) {
		struct helium_ir_function *func = prog->functions[i];
		size_t j;

		fprintf(out, "function %s%s%s(",
			func->is_foreign ? "foreign " : "",
			func->is_closure ? "closure " : "",
			func->name);
		for (j = 0; j < func->capture_count; j++) {
			if (j > 0)
				fprintf(out, ", ");
			fprintf(out, "[%s: ", func->capture_names[j]);
			print_type(func->capture_types[j], out);
			fprintf(out, "]");
		}
		if (func->capture_count > 0 && func->param_count > 0)
			fprintf(out, ", ");
		for (j = 0; j < func->param_count; j++) {
			struct helium_ir_param *p = func->params[j];

			if (j > 0)
				fprintf(out, ", ");
			fprintf(out, "%s: ", p->name);
			print_type(p->type, out);
		}
		fprintf(out, ") -> ");
		print_type(func->ret_type, out);
		fprintf(out, "\n");
		print_block(func->body, 1, out);
	}
}
