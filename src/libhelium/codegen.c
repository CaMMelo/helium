/* SPDX-License-Identifier: TBD */
/*
 * codegen.c - LLVM-C API code generator from monomorphic IR.
 */

#include "codegen.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm-c/Types.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ir.h"
#include "parser.tab.h"
#include "token.h"
#include "types.h"

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

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

static int type_is_integer(const char *name);

static void format_error(char **error, const char *fmt, ...)
{
	va_list ap;
	char *msg;
	int n;

	va_start(ap, fmt);
	n = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (n < 0) {
		*error = strdup("codegen error");
		return;
	}
	*error = msg;
}

static int is_heap_type(struct helium_type *type)
{
	const char *name;

	if (!type)
		return 0;
	if (type->kind == HELIUM_TYPE_FN)
		return 0;
	if (type->kind == HELIUM_TYPE_ARRAY)
		return 1;
	if (type->kind != HELIUM_TYPE_NAMED)
		return 0;
	name = type->name;
	if (!name)
		return 0;
	if (strcmp(name, "str") == 0)
		return 0; /* bootstrap: str is a C string pointer */
	if (type_is_integer(name) || strcmp(name, "f32") == 0 ||
	    strcmp(name, "f64") == 0 || strcmp(name, "()") == 0 ||
	    strcmp(name, "IO") == 0)
		return 0;
	return 1; /* user-defined record or ADT */
}

static int is_io_type(struct helium_type *type)
{
	if (!type || type->kind != HELIUM_TYPE_NAMED)
		return 0;
	return strcmp(type->name, "IO") == 0;
}

static int instr_is_owned_heap_value(struct helium_ir_instr *instr)
{
	if (!instr)
		return 0;
	if (!is_heap_type(instr->type))
		return 0;
	return instr->kind == HELIUM_IR_INSTR_RECORD_ALLOC ||
		instr->kind == HELIUM_IR_INSTR_VARIANT_ALLOC ||
		instr->kind == HELIUM_IR_INSTR_ARRAY_ALLOC ||
		instr->kind == HELIUM_IR_INSTR_CALL;
}

static int is_unit_type(struct helium_type *type)
{
	if (!type || type->kind != HELIUM_TYPE_NAMED)
		return 0;
	return strcmp(type->name, "()") == 0;
}

static int type_is_integer(const char *name)
{
	return strcmp(name, "i8") == 0 ||
		strcmp(name, "i16") == 0 ||
		strcmp(name, "i32") == 0 ||
		strcmp(name, "i64") == 0 ||
		strcmp(name, "u8") == 0 ||
		strcmp(name, "u16") == 0 ||
		strcmp(name, "u32") == 0 ||
		strcmp(name, "u64") == 0 ||
		strcmp(name, "bool") == 0;
}

/* -------------------------------------------------------------------------- */
/* Codegen context                                                            */
/* -------------------------------------------------------------------------- */

struct cg_func {
	struct helium_ir_function *ir_func;
	LLVMValueRef func;
	LLVMBasicBlockRef entry;
	LLVMBuilderRef builder;
	LLVMValueRef loop_fn;
	LLVMTypeRef loop_fn_type;
	unsigned loop_binding_count;
	unsigned captured_count;
	size_t binding_base;
};

struct cg_binding {
	char *name;
	LLVMValueRef value;
	struct helium_type *type;
	int owned;
	int moved;
};

struct cg_ctx {
	LLVMContextRef ctx;
	LLVMModuleRef module;
	LLVMBuilderRef builder;
	struct cg_func *current_func;
	struct cg_binding *bindings;
	size_t binding_count;
	size_t binding_capacity;
	struct helium_ir_program *prog;
	char **error;
	LLVMTypeRef ptr_type;
	LLVMTypeRef i8_type;
	LLVMTypeRef i32_type;
	LLVMTypeRef i64_type;
};

static struct cg_binding *lookup_binding(struct cg_ctx *ctx, const char *name)
{
	size_t i;

	for (i = ctx->binding_count; i > 0; i--) {
		struct cg_binding *b = &ctx->bindings[i - 1];

		if (strcmp(b->name, name) == 0)
			return b;
	}
	return NULL;
}

static void push_binding(struct cg_ctx *ctx, const char *name,
			 LLVMValueRef value, struct helium_type *type)
{
	struct cg_binding b;

	if (ctx->binding_count == ctx->binding_capacity) {
		size_t newcap = ctx->binding_capacity ? ctx->binding_capacity * 2 : 16;
		struct cg_binding *tmp = realloc(ctx->bindings,
						 newcap * sizeof(*tmp));

		if (!tmp)
			abort();
		ctx->bindings = tmp;
		ctx->binding_capacity = newcap;
	}
	b.name = xstrdup(name);
	b.value = value;
	b.type = type;
	b.owned = 0;
	b.moved = 0;
	ctx->bindings[ctx->binding_count++] = b;
}

static void emit_retain(struct cg_ctx *ctx, LLVMValueRef value,
			struct helium_type *type);
static void emit_release(struct cg_ctx *ctx, LLVMValueRef value,
			 struct helium_type *type);

static void mark_result_moved(struct cg_ctx *ctx, LLVMValueRef value)
{
	size_t i;

	for (i = 0; i < ctx->binding_count; i++) {
		struct cg_binding *b = &ctx->bindings[i];

		if (b->value == value && b->owned)
			b->moved = 1;
	}
}

static void release_owned_bindings(struct cg_ctx *ctx, size_t target)
{
	size_t i;

	for (i = ctx->binding_count; i > target; i--) {
		struct cg_binding *b = &ctx->bindings[i - 1];

		if (b->owned && !b->moved && is_heap_type(b->type))
			emit_release(ctx, b->value, b->type);
	}
}

static void pop_bindings_to(struct cg_ctx *ctx, size_t target)
{
	while (ctx->binding_count > target) {
		struct cg_binding *b = &ctx->bindings[--ctx->binding_count];

		free(b->name);
	}
}

/* -------------------------------------------------------------------------- */
/* LLVM type construction                                                     */
/* -------------------------------------------------------------------------- */

static LLVMTypeRef helium_type_to_llvm(struct cg_ctx *ctx,
				       struct helium_type *type);


static LLVMTypeRef closure_struct_type(struct cg_ctx *ctx)
{
	LLVMTypeRef fields[6];

	/* Matches struct helium_closure in the runtime. */
	fields[0] = ctx->i64_type; /* refcount */
	fields[1] = ctx->i32_type; /* kind */
	fields[2] = ctx->ptr_type; /* destroy */
	fields[3] = ctx->ptr_type; /* fn */
	fields[4] = ctx->ptr_type; /* env_destroy */
	fields[5] = ctx->ptr_type; /* env */
	return LLVMStructTypeInContext(ctx->ctx, fields, 6, 0);
}

static LLVMTypeRef closure_env_struct_type(struct cg_ctx *ctx,
					   struct helium_ir_function *func)
{
	LLVMTypeRef *fields;
	LLVMTypeRef ty;
	size_t i;

	if (func->capture_count == 0)
		return LLVMStructTypeInContext(ctx->ctx, NULL, 0, 0);
	fields = xalloc(func->capture_count * sizeof(*fields));
	for (i = 0; i < func->capture_count; i++)
		fields[i] = helium_type_to_llvm(ctx, func->capture_types[i]);
	ty = LLVMStructTypeInContext(ctx->ctx, fields,
				     (unsigned)func->capture_count, 0);
	free(fields);
	return ty;
}
static LLVMTypeRef unit_type(struct cg_ctx *ctx)
{
	return ctx->i64_type;
}

static LLVMTypeRef io_type_to_llvm(struct cg_ctx *ctx,
				   struct helium_type *type)
{
	struct helium_type *inner;

	if (!type || type->arg_count == 0)
		return ctx->i64_type;
	inner = type->args[0];
	if (is_unit_type(inner))
		return ctx->i64_type;
	return helium_type_to_llvm(ctx, inner);
}

static LLVMTypeRef helium_type_to_llvm(struct cg_ctx *ctx,
				       struct helium_type *type)
{
	const char *name;
	LLVMTypeRef *params;
	LLVMTypeRef ret;
	size_t i;

	if (!type)
		return ctx->i64_type;

	if (is_io_type(type))
		return io_type_to_llvm(ctx, type);

	switch (type->kind) {
	case HELIUM_TYPE_VAR:
		return ctx->i64_type;
	case HELIUM_TYPE_NAMED:
		name = type->name ? type->name : "i32";
		if (strcmp(name, "()") == 0)
			return unit_type(ctx);
		if (strcmp(name, "bool") == 0)
			return LLVMInt8TypeInContext(ctx->ctx);
		if (strcmp(name, "i8") == 0)
			return LLVMInt8TypeInContext(ctx->ctx);
		if (strcmp(name, "i16") == 0)
			return LLVMInt16TypeInContext(ctx->ctx);
		if (strcmp(name, "i32") == 0)
			return LLVMInt32TypeInContext(ctx->ctx);
		if (strcmp(name, "i64") == 0)
			return LLVMInt64TypeInContext(ctx->ctx);
		if (strcmp(name, "u8") == 0)
			return LLVMInt8TypeInContext(ctx->ctx);
		if (strcmp(name, "u16") == 0)
			return LLVMInt16TypeInContext(ctx->ctx);
		if (strcmp(name, "u32") == 0)
			return LLVMInt32TypeInContext(ctx->ctx);
		if (strcmp(name, "u64") == 0)
			return LLVMInt64TypeInContext(ctx->ctx);
		if (strcmp(name, "f32") == 0)
			return LLVMFloatTypeInContext(ctx->ctx);
		if (strcmp(name, "f64") == 0)
			return LLVMDoubleTypeInContext(ctx->ctx);
		if (strcmp(name, "str") == 0)
			return ctx->ptr_type;
		return ctx->ptr_type;
	case HELIUM_TYPE_FN:
		/* Function values are represented as opaque pointers.  Use
		 * helium_function_type_to_llvm() when the underlying function
		 * type is required (e.g. for LLVMAddFunction).
		 */
		(void)params;
		(void)ret;
		(void)i;
		return ctx->ptr_type;
	case HELIUM_TYPE_ARRAY:
		return ctx->ptr_type;
	}
	return ctx->i64_type;
}


static LLVMTypeRef llvm_type_for_instr_type(struct cg_ctx *ctx,
					    struct helium_ir_instr *instr)
{
	return helium_type_to_llvm(ctx, instr->type);
}

/* -------------------------------------------------------------------------- */
/* Runtime helpers                                                            */
/* -------------------------------------------------------------------------- */

static LLVMValueRef get_runtime_function(struct cg_ctx *ctx,
					 const char *name,
					 LLVMTypeRef func_type)
{
	LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, name);

	if (!fn)
		fn = LLVMAddFunction(ctx->module, name, func_type);
	return fn;
}

static LLVMValueRef get_retain_fn(struct cg_ctx *ctx)
{
	LLVMTypeRef args[1];
	LLVMTypeRef ft;

	args[0] = ctx->ptr_type;
	ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->ctx), args, 1, 0);
	return get_runtime_function(ctx, "helium_retain", ft);
}

static LLVMValueRef get_release_fn(struct cg_ctx *ctx)
{
	LLVMTypeRef args[1];
	LLVMTypeRef ft;

	args[0] = ctx->ptr_type;
	ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->ctx), args, 1, 0);
	return get_runtime_function(ctx, "helium_release", ft);
}

static void emit_retain(struct cg_ctx *ctx, LLVMValueRef value,
			struct helium_type *type)
{
	LLVMValueRef fn;
	LLVMValueRef args[1];

	if (!is_heap_type(type))
		return;
	fn = get_retain_fn(ctx);
	args[0] = value;
	LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(fn),
		       fn, args, 1, "");
}

static void emit_release(struct cg_ctx *ctx, LLVMValueRef value,
			 struct helium_type *type)
{
	LLVMValueRef fn;
	LLVMValueRef args[1];
	LLVMBasicBlockRef bb;

	if (!is_heap_type(type))
		return;
	bb = LLVMGetInsertBlock(ctx->builder);
	if (!bb || LLVMGetBasicBlockTerminator(bb))
		return;
	fn = get_release_fn(ctx);
	args[0] = value;
	LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(fn),
		       fn, args, 1, "");
}

static LLVMValueRef emit_cstring(struct cg_ctx *ctx, const char *text)
{
	LLVMValueRef global;
	LLVMValueRef str;
	LLVMValueRef zero;
	LLVMValueRef indices[2];
	LLVMTypeRef arr_type;
	size_t len;

	len = strlen(text);
	arr_type = LLVMArrayType(ctx->i8_type, len + 1);
	str = LLVMConstStringInContext(ctx->ctx, text, (unsigned)len, 0);
	global = LLVMAddGlobal(ctx->module, arr_type, "");
	LLVMSetInitializer(global, str);
	LLVMSetGlobalConstant(global, 1);
	LLVMSetLinkage(global, LLVMPrivateLinkage);
	zero = LLVMConstInt(ctx->i32_type, 0, 0);
	indices[0] = zero;
	indices[1] = zero;
	return LLVMBuildGEP2(ctx->builder, arr_type, global, indices, 2,
			     "cstr");
}

/* -------------------------------------------------------------------------- */
/* Forward declarations                                                       */
/* -------------------------------------------------------------------------- */

static LLVMValueRef codegen_instr(struct cg_ctx *ctx,
				  struct helium_ir_instr *instr);

static void codegen_block(struct cg_ctx *ctx, struct helium_ir_block *block,
			  LLVMValueRef *out_value, struct helium_type **out_type);

static LLVMValueRef codegen_closure_alloc(struct cg_ctx *ctx,
					  struct helium_ir_instr *instr);
static LLVMValueRef codegen_closure_call(struct cg_ctx *ctx,
					 struct helium_ir_instr *instr);

/* -------------------------------------------------------------------------- */
/* Literals and identifiers                                                   */
/* -------------------------------------------------------------------------- */

static LLVMValueRef codegen_literal(struct cg_ctx *ctx,
				    struct helium_ir_instr *instr)
{
	struct helium_literal *lit = instr->u.literal.lit;
	LLVMTypeRef type;
	long long ival;
	double fval;

	type = llvm_type_for_instr_type(ctx, instr);

	switch (lit->kind) {
	case HELIUM_LIT_INT:
		ival = strtoll(lit->text, NULL, 10);
		return LLVMConstInt(type, (unsigned long long)ival,
				    LLVMGetIntTypeWidth(type) != 64);
	case HELIUM_LIT_FLOAT:
		fval = strtod(lit->text, NULL);
		if (LLVMGetTypeKind(type) == LLVMFloatTypeKind)
			return LLVMConstReal(LLVMFloatTypeInContext(ctx->ctx),
					     fval);
		return LLVMConstReal(LLVMDoubleTypeInContext(ctx->ctx), fval);
	case HELIUM_LIT_BOOL:
		return LLVMConstInt(type,
				    strcmp(lit->text, "true") == 0 ? 1 : 0,
				    0);
	case HELIUM_LIT_STRING:
		/* Bootstrap: str is represented as a null-terminated C string. */
		return emit_cstring(ctx, lit->text);
	case HELIUM_LIT_UNIT:
		return LLVMConstInt(ctx->i64_type, 0, 0);
	}
	return LLVMConstInt(ctx->i64_type, 0, 0);
}

static LLVMValueRef codegen_ident(struct cg_ctx *ctx,
				  struct helium_ir_instr *instr)
{
	struct cg_binding *b;

	b = lookup_binding(ctx, instr->u.ident.name);
	if (b)
		return b->value;

	/* Top-level function reference. */
	LLVMValueRef fn = LLVMGetNamedFunction(ctx->module,
					       instr->u.ident.name);
	if (fn)
		return fn;

	format_error(ctx->error, "%d:%d: unknown identifier '%s'",
		     instr->line, instr->col, instr->u.ident.name);
	return NULL;
}

/* -------------------------------------------------------------------------- */
/* Binary and unary operations                                                */
/* -------------------------------------------------------------------------- */

static LLVMValueRef codegen_binary(struct cg_ctx *ctx,
				   struct helium_ir_instr *instr)
{
	LLVMValueRef left = codegen_instr(ctx, instr->u.binary.left);
	LLVMValueRef right = codegen_instr(ctx, instr->u.binary.right);
	LLVMValueRef result;
	LLVMTypeRef type;
	int op = instr->u.binary.op;
	int is_float;

	if (!left || !right)
		return NULL;

	type = llvm_type_for_instr_type(ctx, instr);
	is_float = LLVMGetTypeKind(type) == LLVMFloatTypeKind ||
		LLVMGetTypeKind(type) == LLVMDoubleTypeKind;

	if (op == PLUS) {
		if (is_float)
			result = LLVMBuildFAdd(ctx->builder, left, right, "add");
		else
			result = LLVMBuildAdd(ctx->builder, left, right, "add");
	} else if (op == MINUS) {
		if (is_float)
			result = LLVMBuildFSub(ctx->builder, left, right, "sub");
		else
			result = LLVMBuildSub(ctx->builder, left, right, "sub");
	} else if (op == STAR) {
		if (is_float)
			result = LLVMBuildFMul(ctx->builder, left, right, "mul");
		else
			result = LLVMBuildMul(ctx->builder, left, right, "mul");
	} else if (op == SLASH) {
		if (is_float)
			result = LLVMBuildFDiv(ctx->builder, left, right, "div");
		else
			result = LLVMBuildSDiv(ctx->builder, left, right, "div");
	} else if (op == PERCENT) {
		result = LLVMBuildSRem(ctx->builder, left, right, "rem");
	} else if (op == EQEQ) {
		if (is_float)
			result = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, left,
					       right, "eq");
		else
			result = LLVMBuildICmp(ctx->builder, LLVMIntEQ, left,
					       right, "eq");
	} else if (op == NEQ) {
		if (is_float)
			result = LLVMBuildFCmp(ctx->builder, LLVMRealONE, left,
					       right, "ne");
		else
			result = LLVMBuildICmp(ctx->builder, LLVMIntNE, left,
					       right, "ne");
	} else if (op == LT) {
		if (is_float)
			result = LLVMBuildFCmp(ctx->builder, LLVMRealOLT, left,
					       right, "lt");
		else
			result = LLVMBuildICmp(ctx->builder, LLVMIntSLT, left,
					       right, "lt");
	} else if (op == LE) {
		if (is_float)
			result = LLVMBuildFCmp(ctx->builder, LLVMRealOLE, left,
					       right, "le");
		else
			result = LLVMBuildICmp(ctx->builder, LLVMIntSLE, left,
					       right, "le");
	} else if (op == GT) {
		if (is_float)
			result = LLVMBuildFCmp(ctx->builder, LLVMRealOGT, left,
					       right, "gt");
		else
			result = LLVMBuildICmp(ctx->builder, LLVMIntSGT, left,
					       right, "gt");
	} else if (op == GE) {
		if (is_float)
			result = LLVMBuildFCmp(ctx->builder, LLVMRealOGE, left,
					       right, "ge");
		else
			result = LLVMBuildICmp(ctx->builder, LLVMIntSGE, left,
					       right, "ge");
	} else if (op == AND) {
		result = LLVMBuildAnd(ctx->builder, left, right, "and");
	} else if (op == OR) {
		result = LLVMBuildOr(ctx->builder, left, right, "or");
	} else {
		format_error(ctx->error, "%d:%d: unsupported binary operator %d",
			     instr->line, instr->col, op);
		return NULL;
	}

	/*
	 * Comparison operators produce i1, but Helium's bool is i8.  Zero-extend
	 * the result when the inferred type is bool.
	 */
	if (LLVMGetTypeKind(LLVMTypeOf(result)) == LLVMIntegerTypeKind &&
	    LLVMGetIntTypeWidth(LLVMTypeOf(result)) == 1 &&
	    LLVMGetTypeKind(type) == LLVMIntegerTypeKind &&
	    LLVMGetIntTypeWidth(type) == 8)
		result = LLVMBuildZExt(ctx->builder, result, type, "bool_ext");

	return result;
}

static LLVMValueRef codegen_unary(struct cg_ctx *ctx,
				  struct helium_ir_instr *instr)
{
	LLVMValueRef operand = codegen_instr(ctx, instr->u.unary.operand);
	int op = instr->u.unary.op;

	if (!operand)
		return NULL;

	if (op == MINUS)
		return LLVMBuildNeg(ctx->builder, operand, "neg");
	if (op == NOT)
		return LLVMBuildNot(ctx->builder, operand, "not");
	if (op == PLUS)
		return operand;

	format_error(ctx->error, "%d:%d: unsupported unary operator %d",
		     instr->line, instr->col, op);
	return NULL;
}

/* -------------------------------------------------------------------------- */
/* Calls                                                                      */
/* -------------------------------------------------------------------------- */

static LLVMValueRef codegen_call(struct cg_ctx *ctx,
				 struct helium_ir_instr *instr,
				 int is_foreign)
{
	const char *name;
	LLVMValueRef fn;
	LLVMValueRef *args;
	LLVMTypeRef func_type;
	LLVMTypeRef *arg_types;
	LLVMTypeRef ret_type;
	size_t i;
	unsigned arg_count;

	if (is_foreign) {
		name = instr->u.foreign_call.name;
		arg_count = (unsigned)instr->u.foreign_call.arg_count;
	} else {
		name = instr->u.call.name;
		arg_count = (unsigned)instr->u.call.arg_count;
	}

	fn = LLVMGetNamedFunction(ctx->module, name);
	if (!fn) {
		/* Declare it with types from the IR instruction. */
		arg_types = xalloc(arg_count * sizeof(*arg_types));
		for (i = 0; i < arg_count; i++) {
			struct helium_ir_instr *arg;

			arg = is_foreign ? instr->u.foreign_call.args[i] :
					   instr->u.call.args[i];
			arg_types[i] = llvm_type_for_instr_type(ctx, arg);
		}
		ret_type = llvm_type_for_instr_type(ctx, instr);
		func_type = LLVMFunctionType(ret_type, arg_types, arg_count, 0);
		fn = LLVMAddFunction(ctx->module, name, func_type);
		free(arg_types);
	} else {
		func_type = LLVMGlobalGetValueType(fn);
	}

	args = xalloc(arg_count * sizeof(*args));
	for (i = 0; i < arg_count; i++) {
		struct helium_ir_instr *arg;

		arg = is_foreign ? instr->u.foreign_call.args[i] :
				   instr->u.call.args[i];
		args[i] = codegen_instr(ctx, arg);
		if (!args[i]) {
			free(args);
			return NULL;
		}
	}

	return LLVMBuildCall2(ctx->builder, func_type, fn, args, arg_count,
			      "call");
}

/* -------------------------------------------------------------------------- */
/* Aggregate types                                                            */
/* -------------------------------------------------------------------------- */

static LLVMValueRef codegen_record_alloc(struct cg_ctx *ctx,
					 struct helium_ir_instr *instr)
{
	LLVMTypeRef args[2];
	LLVMTypeRef ft;
	LLVMValueRef fn;
	LLVMValueRef call_args[2];
	LLVMValueRef rec;
	size_t i;
	size_t size = 0;

	(void)instr;
	/* Sizing would require type layout; use a fixed large buffer for now. */
	size = instr->u.record_alloc.field_count * 8;
	if (size < 8)
		size = 8;

	args[0] = ctx->i64_type;
	args[1] = ctx->ptr_type;
	ft = LLVMFunctionType(ctx->ptr_type, args, 2, 0);
	fn = get_runtime_function(ctx, "helium_alloc_record", ft);
	call_args[0] = LLVMConstInt(ctx->i64_type, size, 0);
	call_args[1] = LLVMConstPointerNull(ctx->ptr_type);
	rec = LLVMBuildCall2(ctx->builder, ft, fn, call_args, 2, "rec");

	for (i = 0; i < instr->u.record_alloc.field_count; i++) {
		LLVMValueRef val = codegen_instr(ctx,
						 instr->u.record_alloc.field_values[i]);
		(void)val;
		/* Field layout not yet implemented. */
	}
	return rec;
}

static LLVMValueRef codegen_record_get(struct cg_ctx *ctx,
				       struct helium_ir_instr *instr)
{
	format_error(ctx->error, "%d:%d: record_get not yet implemented",
		     instr->line, instr->col);
	return NULL;
}

static LLVMValueRef codegen_record_set(struct cg_ctx *ctx,
				       struct helium_ir_instr *instr)
{
	format_error(ctx->error, "%d:%d: record_set not yet implemented",
		     instr->line, instr->col);
	return NULL;
}

static LLVMValueRef codegen_variant_alloc(struct cg_ctx *ctx,
					  struct helium_ir_instr *instr)
{
	LLVMTypeRef args[3];
	LLVMTypeRef ft;
	LLVMValueRef fn;
	LLVMValueRef call_args[3];
	size_t size = instr->u.variant_alloc.field_count * 8;

	if (size < 8)
		size = 8;

	args[0] = ctx->i64_type;
	args[1] = ctx->i64_type;
	args[2] = ctx->ptr_type;
	ft = LLVMFunctionType(ctx->ptr_type, args, 3, 0);
	fn = get_runtime_function(ctx, "helium_alloc_adt", ft);
	call_args[0] = LLVMConstInt(ctx->i64_type, 0, 0); /* variant tag */
	call_args[1] = LLVMConstInt(ctx->i64_type, size, 0);
	call_args[2] = LLVMConstPointerNull(ctx->ptr_type);
	return LLVMBuildCall2(ctx->builder, ft, fn, call_args, 3, "adt");
}

static LLVMValueRef codegen_array_alloc(struct cg_ctx *ctx,
					struct helium_ir_instr *instr)
{
	LLVMTypeRef args[3];
	LLVMTypeRef ft;
	LLVMValueRef fn;
	LLVMValueRef call_args[3];
	LLVMValueRef arr;
	size_t i;
	size_t elem_size = 8;

	args[0] = ctx->i64_type;
	args[1] = ctx->i64_type;
	args[2] = ctx->ptr_type;
	ft = LLVMFunctionType(ctx->ptr_type, args, 3, 0);
	fn = get_runtime_function(ctx, "helium_alloc_array", ft);
	call_args[0] = LLVMConstInt(ctx->i64_type,
				    (unsigned long long)instr->u.array_alloc.size,
				    0);
	call_args[1] = LLVMConstInt(ctx->i64_type, elem_size, 0);
	call_args[2] = LLVMConstPointerNull(ctx->ptr_type);
	arr = LLVMBuildCall2(ctx->builder, ft, fn, call_args, 3, "arr");

	for (i = 0; i < instr->u.array_alloc.item_count; i++) {
		LLVMValueRef item = codegen_instr(ctx,
						  instr->u.array_alloc.items[i]);
		(void)item;
		/* Element storage not yet implemented. */
	}
	return arr;
}

static LLVMValueRef codegen_array_get(struct cg_ctx *ctx,
				      struct helium_ir_instr *instr)
{
	format_error(ctx->error, "%d:%d: array_get not yet implemented",
		     instr->line, instr->col);
	return NULL;
}

/* -------------------------------------------------------------------------- */
/* Control flow                                                               */
/* -------------------------------------------------------------------------- */

static LLVMValueRef codegen_if(struct cg_ctx *ctx,
			       struct helium_ir_instr *instr)
{
	LLVMValueRef cond;
	LLVMBasicBlockRef then_bb;
	LLVMBasicBlockRef else_bb;
	LLVMBasicBlockRef merge_bb;
	LLVMValueRef then_val;
	LLVMValueRef else_val;
	LLVMTypeRef result_type;
	LLVMValueRef phi;
	int in_loop;

	cond = codegen_instr(ctx, instr->u.if_expr.cond);
	if (!cond)
		return NULL;
	if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
	    LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
		LLVMTypeRef cond_type = LLVMTypeOf(cond);
		cond = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond,
				     LLVMConstInt(cond_type, 0, 0),
				     "cond");
	}

	in_loop = ctx->current_func && ctx->current_func->loop_fn != NULL;

	then_bb = LLVMAppendBasicBlockInContext(ctx->ctx,
						ctx->current_func->func,
						"then");
	else_bb = LLVMAppendBasicBlockInContext(ctx->ctx,
						ctx->current_func->func,
						"else");
	if (!in_loop) {
		merge_bb = LLVMAppendBasicBlockInContext(ctx->ctx,
							 ctx->current_func->func,
							 "merge");
	}

	LLVMBuildCondBr(ctx->builder, cond, then_bb, else_bb);

	LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
	codegen_block(ctx, instr->u.if_expr.then_branch, &then_val, NULL);
	if (in_loop && then_val &&
	    !LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
		LLVMBuildRet(ctx->builder, then_val);
	else if (!in_loop && then_val &&
		 !LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
		LLVMBuildBr(ctx->builder, merge_bb);

	LLVMPositionBuilderAtEnd(ctx->builder, else_bb);
	codegen_block(ctx, instr->u.if_expr.else_branch, &else_val, NULL);
	if (in_loop && else_val &&
	    !LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
		LLVMBuildRet(ctx->builder, else_val);
	else if (!in_loop && else_val &&
		 !LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
		LLVMBuildBr(ctx->builder, merge_bb);

	if (in_loop)
		return NULL;

	LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
	result_type = llvm_type_for_instr_type(ctx, instr);
	if (LLVMGetTypeKind(result_type) == LLVMVoidTypeKind)
		return NULL;
	phi = LLVMBuildPhi(ctx->builder, result_type, "phi");
	if (then_val)
		LLVMAddIncoming(phi, &then_val, &then_bb, 1);
	if (else_val)
		LLVMAddIncoming(phi, &else_val, &else_bb, 1);
	return phi;
}

static LLVMValueRef codegen_match(struct cg_ctx *ctx,
				  struct helium_ir_instr *instr)
{
	LLVMValueRef value;
	size_t i;

	value = codegen_instr(ctx, instr->u.match.value);
	if (!value)
		return NULL;

	/* Minimal implementation: treat match as the value of the first arm. */
	(void)value;
	for (i = 0; i < instr->u.match.arm_count; i++) {
		struct helium_ir_match_arm *arm = instr->u.match.arms[i];
		LLVMValueRef arm_val;
		struct helium_type *arm_type;

		codegen_block(ctx, arm->body, &arm_val, &arm_type);
		if (i == 0 && arm_val)
			return arm_val;
	}
	return LLVMConstInt(ctx->i64_type, 0, 0);
}

static LLVMValueRef codegen_loop(struct cg_ctx *ctx,
				 struct helium_ir_instr *instr);
static LLVMValueRef codegen_recur(struct cg_ctx *ctx,
				  struct helium_ir_instr *instr);

static LLVMTypeRef llvm_type_for_binding(struct cg_ctx *ctx,
					 struct helium_ir_let_binding *binding)
{
	if (binding->type && binding->type->kind != HELIUM_TYPE_VAR)
		return helium_type_to_llvm(ctx, binding->type);
	return llvm_type_for_instr_type(ctx, binding->value);
}

static LLVMValueRef codegen_loop(struct cg_ctx *ctx,
				 struct helium_ir_instr *instr)
{
	LLVMTypeRef *param_types;
	LLVMValueRef *arg_values;
	LLVMTypeRef ret_type;
	LLVMTypeRef loop_fn_type;
	LLVMValueRef loop_fn;
	LLVMValueRef call_args[32];
	char name[64];
	static int loop_counter;
	struct cg_func *outer;
	struct cg_func inner;
	LLVMBasicBlockRef entry;
	size_t i;
	unsigned loop_binding_count;
	unsigned captured_count;
	unsigned total_params;
	LLVMValueRef result;

	loop_binding_count = (unsigned)instr->u.loop.binding_count;
	captured_count = (unsigned)ctx->binding_count;
	total_params = loop_binding_count + captured_count;
	if (total_params > 32) {
		format_error(ctx->error, "%d:%d: too many loop captures",
			     instr->line, instr->col);
		return NULL;
	}

	snprintf(name, sizeof(name), "__loop_%d", loop_counter++);

	param_types = xalloc(total_params * sizeof(*param_types));
	arg_values = xalloc(total_params * sizeof(*arg_values));

	for (i = 0; i < loop_binding_count; i++)
		param_types[i] = llvm_type_for_binding(ctx,
						       instr->u.loop.bindings[i]);
	for (i = 0; i < captured_count; i++) {
		if (ctx->bindings[i].type &&
		    ctx->bindings[i].type->kind != HELIUM_TYPE_VAR) {
			param_types[loop_binding_count + i] =
				helium_type_to_llvm(ctx, ctx->bindings[i].type);
		} else {
			param_types[loop_binding_count + i] =
				LLVMTypeOf(ctx->bindings[i].value);
		}
	}

	ret_type = llvm_type_for_instr_type(ctx, instr);
	loop_fn_type = LLVMFunctionType(ret_type, param_types, total_params, 0);
	loop_fn = LLVMAddFunction(ctx->module, name, loop_fn_type);

	/* Initial call arguments. */
	for (i = 0; i < loop_binding_count; i++) {
		arg_values[i] = codegen_instr(ctx,
					      instr->u.loop.bindings[i]->value);
		if (!arg_values[i]) {
			free(param_types);
			free(arg_values);
			return NULL;
		}
	}
	for (i = 0; i < captured_count; i++)
		arg_values[loop_binding_count + i] = ctx->bindings[i].value;

	/* Generate loop function body. */
	LLVMBasicBlockRef outer_block = LLVMGetInsertBlock(ctx->builder);

	outer = ctx->current_func;
	entry = LLVMAppendBasicBlockInContext(ctx->ctx, loop_fn, "entry");
	LLVMPositionBuilderAtEnd(ctx->builder, entry);

	memset(&inner, 0, sizeof(inner));
	inner.ir_func = NULL;
	inner.func = loop_fn;
	inner.entry = entry;
	inner.builder = ctx->builder;
	inner.loop_fn = loop_fn;
	inner.loop_fn_type = loop_fn_type;
	inner.loop_binding_count = loop_binding_count;
	inner.captured_count = captured_count;
	ctx->current_func = &inner;
	{
		size_t base = ctx->binding_count;
		struct helium_type *result_type = NULL;

		inner.binding_base = base;

		for (i = 0; i < loop_binding_count; i++) {
			LLVMValueRef param = LLVMGetParam(loop_fn, (unsigned)i);

			push_binding(ctx, instr->u.loop.bindings[i]->name, param,
				     instr->u.loop.bindings[i]->type);
			ctx->bindings[ctx->binding_count - 1].owned =
				is_heap_type(instr->u.loop.bindings[i]->type);
		}
		for (i = 0; i < captured_count; i++) {
			LLVMValueRef param = LLVMGetParam(loop_fn,
							  (unsigned)(loop_binding_count + i));

			push_binding(ctx, ctx->bindings[i].name, param,
				     ctx->bindings[i].type);
		}

		codegen_block(ctx, instr->u.loop.body, &result, &result_type);
		if (result && LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL) {
			if (result_type && is_heap_type(result_type))
				mark_result_moved(ctx, result);
			release_owned_bindings(ctx, base);
			LLVMBuildRet(ctx->builder, result);
		} else if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL) {
			release_owned_bindings(ctx, base);
			LLVMBuildRet(ctx->builder, LLVMConstInt(ctx->i64_type, 0, 0));
		}

		pop_bindings_to(ctx, base);
	}
	ctx->current_func = outer;
	LLVMPositionBuilderAtEnd(ctx->builder, outer_block);

	free(param_types);

	for (i = 0; i < total_params; i++)
		call_args[i] = arg_values[i];
	free(arg_values);

	return LLVMBuildCall2(ctx->builder, loop_fn_type, loop_fn, call_args,
			      total_params, "loop");
}

static LLVMValueRef codegen_recur(struct cg_ctx *ctx,
				  struct helium_ir_instr *instr)
{
	LLVMValueRef args[32];
	size_t i;
	unsigned total;

	if (!ctx->current_func || !ctx->current_func->loop_fn) {
		format_error(ctx->error, "%d:%d: recur outside loop",
			     instr->line, instr->col);
		return NULL;
	}

	total = ctx->current_func->loop_binding_count +
		ctx->current_func->captured_count;
	if (instr->u.recur.arg_count != ctx->current_func->loop_binding_count) {
		format_error(ctx->error, "%d:%d: recur argument count mismatch",
			     instr->line, instr->col);
		return NULL;
	}
	if (total > 32) {
		format_error(ctx->error, "%d:%d: too many loop arguments",
			     instr->line, instr->col);
		return NULL;
	}

	for (i = 0; i < instr->u.recur.arg_count; i++) {
		args[i] = codegen_instr(ctx, instr->u.recur.args[i]);
		if (!args[i])
			return NULL;
	}
	for (i = 0; i < ctx->current_func->captured_count; i++) {
		args[instr->u.recur.arg_count + i] = LLVMGetParam(
			ctx->current_func->func,
			(unsigned)(ctx->current_func->loop_binding_count + i));
	}

	{
		size_t base = ctx->current_func ? ctx->current_func->binding_base : 0;
		LLVMValueRef call;

		release_owned_bindings(ctx, base);

		call = LLVMBuildCall2(ctx->builder,
				      ctx->current_func->loop_fn_type,
				      ctx->current_func->loop_fn,
				      args, total, "recur");

		LLVMSetTailCall(call, 1);
		LLVMBuildRet(ctx->builder, call);
	}
	return NULL;
}

/* -------------------------------------------------------------------------- */
/* Block, let, return, retain, release, fstring                               */
/* -------------------------------------------------------------------------- */

static LLVMValueRef codegen_instr_block(struct cg_ctx *ctx,
					struct helium_ir_instr *instr)
{
	LLVMValueRef result = NULL;
	struct helium_type *result_type = NULL;
	size_t base;

	base = ctx->binding_count;
	codegen_block(ctx, instr->u.block.block, &result, &result_type);
	if (result && result_type && is_heap_type(result_type))
		mark_result_moved(ctx, result);
	release_owned_bindings(ctx, base);
	pop_bindings_to(ctx, base);
	return result;
}

static LLVMValueRef codegen_let(struct cg_ctx *ctx,
				struct helium_ir_instr *instr)
{
	LLVMValueRef value = codegen_instr(ctx, instr->u.let.value);
	struct helium_type *bind_type;

	if (!value)
		return NULL;
	bind_type = instr->u.let.type ? instr->u.let.type :
		    instr->u.let.value->type;
	push_binding(ctx, instr->u.let.name, value, bind_type);
	ctx->bindings[ctx->binding_count - 1].owned =
		instr_is_owned_heap_value(instr->u.let.value);
	return value;
}

static LLVMValueRef codegen_return(struct cg_ctx *ctx,
				   struct helium_ir_instr *instr)
{
	LLVMValueRef value = codegen_instr(ctx, instr->u.ret.value);
	size_t base;

	if (!value)
		return NULL;
	if (instr->u.ret.value->kind == HELIUM_IR_INSTR_IDENT)
		mark_result_moved(ctx, value);
	base = ctx->current_func ? ctx->current_func->binding_base : 0;
	release_owned_bindings(ctx, base);
	LLVMBuildRet(ctx->builder, value);
	return value;
}

static LLVMValueRef codegen_retain(struct cg_ctx *ctx,
				   struct helium_ir_instr *instr)
{
	LLVMValueRef value = codegen_instr(ctx, instr->u.retain.value);

	if (!value)
		return NULL;
	emit_retain(ctx, value, instr->u.retain.value->type);
	return value;
}

static LLVMValueRef codegen_release(struct cg_ctx *ctx,
				    struct helium_ir_instr *instr)
{
	LLVMValueRef value = codegen_instr(ctx, instr->u.release.value);

	if (!value)
		return NULL;
	emit_release(ctx, value, instr->u.release.value->type);
	return value;
}

static LLVMValueRef codegen_fstring(struct cg_ctx *ctx,
				    struct helium_ir_instr *instr)
{
	LLVMValueRef fmt = NULL;
	LLVMValueRef args[16];
	LLVMTypeRef snprintf_type;
	LLVMValueRef snprintf_fn;
	LLVMValueRef buf;
	LLVMValueRef size;
	LLVMValueRef call_args[3 + 16];
	LLVMValueRef call;
	size_t i;
	unsigned arg_count = 0;
	char format_buf[256];
	int format_len = 0;

	format_buf[0] = '\0';

	for (i = 0; i < instr->u.fstring.part_count && arg_count < 16; i++) {
		struct helium_ir_fstring_part *p = instr->u.fstring.parts[i];

		if (!p->is_expr) {
			/* Escape percent signs in literal text. */
			const char *text = p->u.text ? p->u.text : "";
			size_t j;

			for (j = 0; text[j] && format_len + 2 < (int)sizeof(format_buf);
			     j++) {
				if (text[j] == '%')
					format_buf[format_len++] = '%';
				format_buf[format_len++] = text[j];
			}
			format_buf[format_len] = '\0';
		} else {
			LLVMValueRef val = codegen_instr(ctx, p->u.expr);
			LLVMTypeRef val_type;

			if (!val)
				return NULL;
			val_type = LLVMTypeOf(val);
			args[arg_count] = val;
			if (LLVMGetTypeKind(val_type) == LLVMIntegerTypeKind &&
			    LLVMGetIntTypeWidth(val_type) == 32) {
				strcat(format_buf + format_len, "%d");
				format_len += 2;
			} else if (LLVMGetTypeKind(val_type) ==
				   LLVMDoubleTypeKind) {
				strcat(format_buf + format_len, "%g");
				format_len += 2;
			} else {
				strcat(format_buf + format_len, "%s");
				format_len += 2;
			}
			arg_count++;
		}
	}

	fmt = emit_cstring(ctx, format_buf);
	size = LLVMConstInt(ctx->i64_type, 256, 0);

	buf = LLVMBuildAlloca(ctx->builder,
			      LLVMArrayType(ctx->i8_type, 256),
			      "fstr_buf");

	snprintf_type = LLVMFunctionType(ctx->i32_type,
					 (LLVMTypeRef[]){ ctx->ptr_type,
							  ctx->i64_type,
							  ctx->ptr_type },
					 3, 1);
	snprintf_fn = get_runtime_function(ctx, "snprintf", snprintf_type);

	call_args[0] = buf;
	call_args[1] = size;
	call_args[2] = fmt;
	for (i = 0; i < arg_count; i++)
		call_args[3 + i] = args[i];

	call = LLVMBuildCall2(ctx->builder, snprintf_type, snprintf_fn,
			      call_args, 3 + arg_count, "snprintf");
	(void)call;

	return buf;
}

/* -------------------------------------------------------------------------- */
/* Instruction dispatcher                                                     */
/* -------------------------------------------------------------------------- */


/* -------------------------------------------------------------------------- */
/* Closures                                                                   */
/* -------------------------------------------------------------------------- */

static LLVMValueRef codegen_closure_alloc(struct cg_ctx *ctx,
					  struct helium_ir_instr *instr)
{
	LLVMValueRef fn;
	LLVMValueRef env;
	LLVMValueRef closure;
	LLVMTypeRef env_type;
	LLVMTypeRef alloc_closure_args[3];
	LLVMTypeRef alloc_closure_type;
	LLVMValueRef alloc_closure_args_values[3];
	LLVMValueRef alloc_closure_fn;
	LLVMValueRef fn_ptr;
	LLVMValueRef zero;
	size_t i;
	struct helium_ir_function *ir_func = NULL;

	fn = LLVMGetNamedFunction(ctx->module, instr->u.closure_alloc.func_name);
	if (!fn) {
		format_error(ctx->error, "%d:%d: closure function '%s' not found",
			     instr->line, instr->col,
			     instr->u.closure_alloc.func_name);
		return NULL;
	}

	for (i = 0; i < ctx->prog->function_count; i++) {
		if (strcmp(ctx->prog->functions[i]->name,
			   instr->u.closure_alloc.func_name) == 0) {
			ir_func = ctx->prog->functions[i];
			break;
		}
	}
	if (!ir_func) {
		format_error(ctx->error, "%d:%d: closure capture metadata missing",
			     instr->line, instr->col);
		return NULL;
	}

	env_type = closure_env_struct_type(ctx, ir_func);
	if (ir_func->capture_count > 0)
		env = LLVMBuildMalloc(ctx->builder, env_type, "closure_env");
	else
		env = LLVMConstPointerNull(ctx->ptr_type);

	zero = LLVMConstInt(ctx->i32_type, 0, 0);
	for (i = 0; i < ir_func->capture_count; i++) {
		LLVMValueRef val;
		LLVMValueRef indices[2];
		LLVMValueRef field_ptr;
		LLVMTypeRef field_type;

		val = codegen_instr(ctx, instr->u.closure_alloc.capture_values[i]);
		if (!val)
			return NULL;
		indices[0] = zero;
		indices[1] = LLVMConstInt(ctx->i32_type, (unsigned)i, 0);
		field_ptr = LLVMBuildGEP2(ctx->builder, env_type, env,
					  indices, 2,
					  ir_func->capture_names[i]);
		field_type = helium_type_to_llvm(ctx, ir_func->capture_types[i]);
		LLVMBuildStore(ctx->builder, val, field_ptr);
		(void)field_type;
	}

	fn_ptr = LLVMBuildBitCast(ctx->builder, fn, ctx->ptr_type, "fn_ptr");

	alloc_closure_args[0] = ctx->ptr_type;
	alloc_closure_args[1] = ctx->ptr_type;
	alloc_closure_args[2] = ctx->ptr_type;
	alloc_closure_type = LLVMFunctionType(
				ctx->ptr_type,
				alloc_closure_args, 3, 0);
	alloc_closure_fn = get_runtime_function(ctx, "helium_alloc_closure",
						alloc_closure_type);

	alloc_closure_args_values[0] = fn_ptr;
	alloc_closure_args_values[1] = LLVMBuildBitCast(ctx->builder, env,
						      ctx->ptr_type,
						      "env_ptr");
	alloc_closure_args_values[2] = LLVMConstPointerNull(ctx->ptr_type);
	closure = LLVMBuildCall2(ctx->builder, alloc_closure_type,
				 alloc_closure_fn,
				 alloc_closure_args_values, 3,
				 "closure");
	return closure;
}

static LLVMValueRef codegen_closure_call(struct cg_ctx *ctx,
					 struct helium_ir_instr *instr)
{
	LLVMValueRef closure;
	LLVMValueRef fn_ptr;
	LLVMValueRef env_ptr;
	LLVMTypeRef closure_type;
	LLVMTypeRef *arg_types;
	LLVMTypeRef func_type;
	LLVMValueRef *args;
	LLVMValueRef fn_typed;
	size_t i;
	unsigned arg_count;
	unsigned total_args;

	closure = codegen_instr(ctx, instr->u.closure_call.closure);
	if (!closure)
		return NULL;

	arg_count = (unsigned)instr->u.closure_call.arg_count;
	total_args = arg_count + 1;

	closure_type = closure_struct_type(ctx);
	fn_ptr = LLVMBuildStructGEP2(ctx->builder, closure_type, closure,
				     3, "fn");
	fn_ptr = LLVMBuildLoad2(ctx->builder, ctx->ptr_type, fn_ptr, "fn_ptr");
	env_ptr = LLVMBuildStructGEP2(ctx->builder, closure_type, closure,
				     5, "env");
	env_ptr = LLVMBuildLoad2(ctx->builder, ctx->ptr_type, env_ptr,
				 "env_ptr");

	arg_types = xalloc(total_args * sizeof(*arg_types));
	arg_types[0] = ctx->ptr_type;
	for (i = 0; i < arg_count; i++)
		arg_types[i + 1] = llvm_type_for_instr_type(
					ctx, instr->u.closure_call.args[i]);
	func_type = LLVMFunctionType(
			llvm_type_for_instr_type(ctx, instr),
			arg_types, total_args, 0);
	fn_typed = LLVMBuildBitCast(ctx->builder, fn_ptr,
				    LLVMPointerType(func_type, 0),
				    "fn_typed");

	args = xalloc(total_args * sizeof(*args));
	args[0] = env_ptr;
	for (i = 0; i < arg_count; i++) {
		args[i + 1] = codegen_instr(ctx, instr->u.closure_call.args[i]);
		if (!args[i + 1]) {
			free(args);
			free(arg_types);
			return NULL;
		}
	}

	return LLVMBuildCall2(ctx->builder, func_type, fn_typed, args,
			      total_args, "closure_call");
}
static LLVMValueRef codegen_instr(struct cg_ctx *ctx,
				  struct helium_ir_instr *instr)
{
	if (!instr)
		return LLVMConstInt(ctx->i64_type, 0, 0);


	switch (instr->kind) {
	case HELIUM_IR_INSTR_LITERAL:
		return codegen_literal(ctx, instr);
	case HELIUM_IR_INSTR_IDENT:
		return codegen_ident(ctx, instr);
	case HELIUM_IR_INSTR_CALL:
	case HELIUM_IR_INSTR_TAIL_CALL:
		return codegen_call(ctx, instr, 0);
	case HELIUM_IR_INSTR_FOREIGN_CALL:
		return codegen_call(ctx, instr, 1);
	case HELIUM_IR_INSTR_RECORD_ALLOC:
		return codegen_record_alloc(ctx, instr);
	case HELIUM_IR_INSTR_RECORD_GET:
		return codegen_record_get(ctx, instr);
	case HELIUM_IR_INSTR_RECORD_SET:
		return codegen_record_set(ctx, instr);
	case HELIUM_IR_INSTR_VARIANT_ALLOC:
		return codegen_variant_alloc(ctx, instr);
	case HELIUM_IR_INSTR_ARRAY_ALLOC:
		return codegen_array_alloc(ctx, instr);
	case HELIUM_IR_INSTR_ARRAY_GET:
		return codegen_array_get(ctx, instr);
	case HELIUM_IR_INSTR_IF:
		return codegen_if(ctx, instr);
	case HELIUM_IR_INSTR_MATCH:
		return codegen_match(ctx, instr);
	case HELIUM_IR_INSTR_LOOP:
		return codegen_loop(ctx, instr);
	case HELIUM_IR_INSTR_RECUR:
		return codegen_recur(ctx, instr);
	case HELIUM_IR_INSTR_LET:
		return codegen_let(ctx, instr);
	case HELIUM_IR_INSTR_RETURN:
		return codegen_return(ctx, instr);
	case HELIUM_IR_INSTR_RETAIN:
		return codegen_retain(ctx, instr);
	case HELIUM_IR_INSTR_RELEASE:
		return codegen_release(ctx, instr);
	case HELIUM_IR_INSTR_BLOCK:
		return codegen_instr_block(ctx, instr);
	case HELIUM_IR_INSTR_BINARY:
		return codegen_binary(ctx, instr);
	case HELIUM_IR_INSTR_UNARY:
		return codegen_unary(ctx, instr);
	case HELIUM_IR_INSTR_FSTRING:
		return codegen_fstring(ctx, instr);
	case HELIUM_IR_INSTR_CLOSURE_ALLOC:
		return codegen_closure_alloc(ctx, instr);
	case HELIUM_IR_INSTR_CLOSURE_CALL:
		return codegen_closure_call(ctx, instr);
	}
	format_error(ctx->error, "%d:%d: unsupported IR instruction kind %d",
		     instr->line, instr->col, instr->kind);
	return NULL;
}

static void codegen_block(struct cg_ctx *ctx, struct helium_ir_block *block,
			  LLVMValueRef *out_value, struct helium_type **out_type)
{
	size_t i;

	*out_value = NULL;
	if (out_type)
		*out_type = NULL;

	if (!block)
		return;

	for (i = 0; i < block->instr_count; i++) {
		struct helium_ir_instr *instr = block->instrs[i];
		LLVMValueRef val;

		val = codegen_instr(ctx, instr);
		if (instr->kind == HELIUM_IR_INSTR_RETURN ||
		    instr->kind == HELIUM_IR_INSTR_RECUR ||
		    LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
			/* Control flow leaves this block. */
			break;
		}
		if (val && i < block->instr_count - 1 &&
		    instr->kind != HELIUM_IR_INSTR_LET &&
		    instr_is_owned_heap_value(instr))
			emit_release(ctx, val, instr->type);
		if (val) {
			*out_value = val;
			if (out_type)
				*out_type = instr->type;
		}
	}
}

/* -------------------------------------------------------------------------- */
/* Function generation                                                        */
/* -------------------------------------------------------------------------- */

static LLVMValueRef declare_function(struct cg_ctx *ctx,
				     struct helium_ir_function *func)
{
	LLVMTypeRef *param_types;
	LLVMTypeRef func_type;
	LLVMTypeRef ret_type;
	LLVMValueRef fn;
	unsigned param_count;
	size_t i;

	param_count = (unsigned)func->param_count;
	param_types = xalloc(param_count * sizeof(*param_types));
	for (i = 0; i < func->param_count; i++)
		param_types[i] = helium_type_to_llvm(ctx, func->params[i]->type);
	ret_type = helium_type_to_llvm(ctx, func->ret_type);
	func_type = LLVMFunctionType(ret_type, param_types, param_count, 0);
	fn = LLVMAddFunction(ctx->module, func->name, func_type);
	LLVMSetFunctionCallConv(fn, LLVMCCallConv);
	free(param_types);
	return fn;
}

static int codegen_function(struct cg_ctx *ctx,
			    struct helium_ir_function *func)
{
	LLVMValueRef fn;
	LLVMBasicBlockRef entry;
	struct cg_func cg_func;
	size_t i;
	LLVMValueRef result;

	fn = LLVMGetNamedFunction(ctx->module, func->name);
	if (!fn)
		fn = declare_function(ctx, func);

	entry = LLVMAppendBasicBlockInContext(ctx->ctx, fn, "entry");
	LLVMPositionBuilderAtEnd(ctx->builder, entry);

	memset(&cg_func, 0, sizeof(cg_func));
	cg_func.ir_func = func;
	cg_func.func = fn;
	cg_func.entry = entry;
	cg_func.builder = ctx->builder;
	ctx->current_func = &cg_func;

	{
		size_t base = ctx->binding_count;
		struct helium_type *result_type = NULL;

		cg_func.binding_base = base;

		if (func->is_closure && func->capture_count > 0) {
			LLVMValueRef env_param = LLVMGetParam(fn, 0);
			LLVMTypeRef env_type = closure_env_struct_type(ctx, func);
			LLVMValueRef env_ptr;
			size_t j;

			env_ptr = LLVMBuildBitCast(ctx->builder, env_param,
						     LLVMPointerType(env_type, 0),
						     "env");
			for (j = 0; j < func->capture_count; j++) {
				LLVMValueRef indices[2];
				LLVMValueRef field_ptr;
				LLVMValueRef val;

				indices[0] = LLVMConstInt(ctx->i32_type, 0, 0);
				indices[1] = LLVMConstInt(ctx->i32_type, (unsigned)j, 0);
				field_ptr = LLVMBuildGEP2(ctx->builder, env_type,
							  env_ptr, indices, 2,
							  func->capture_names[j]);
				val = LLVMBuildLoad2(ctx->builder,
						     helium_type_to_llvm(ctx,
								 func->capture_types[j]),
						     field_ptr,
						     func->capture_names[j]);
				push_binding(ctx, func->capture_names[j], val,
					     func->capture_types[j]);
			}
		}

		for (i = 0; i < func->param_count; i++) {
			LLVMValueRef param = LLVMGetParam(fn, (unsigned)i);

			push_binding(ctx, func->params[i]->name, param,
				     func->params[i]->type);
		}

		codegen_block(ctx, func->body, &result, &result_type);

		if (result && LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL) {
			if (result_type && is_heap_type(result_type))
				mark_result_moved(ctx, result);
			release_owned_bindings(ctx, base);
			LLVMBuildRet(ctx->builder, result);
		} else if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL) {
			release_owned_bindings(ctx, base);
			LLVMBuildRet(ctx->builder, LLVMConstInt(ctx->i64_type, 0, 0));
		}

		pop_bindings_to(ctx, base);
	}
	ctx->current_func = NULL;
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Main wrapper and object emission                                           */
/* -------------------------------------------------------------------------- */

static int emit_main_wrapper(struct cg_ctx *ctx)
{
	LLVMTypeRef wrapper_args[1];
	LLVMTypeRef wrapper_type;
	LLVMTypeRef main_type;
	LLVMValueRef wrapper;
	LLVMValueRef main_fn;
	LLVMValueRef call_args[1];
	LLVMTypeRef cmain_type;
	LLVMValueRef cmain;
	LLVMBasicBlockRef entry;

	if (!ctx->prog->main_name)
		return -1;

	main_fn = LLVMGetNamedFunction(ctx->module, ctx->prog->main_name);
	if (!main_fn)
		return -1;

	main_type = LLVMFunctionType(ctx->i64_type, NULL, 0, 0);
	wrapper_args[0] = LLVMPointerType(main_type, 0);
	wrapper_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->ctx),
				      wrapper_args, 1, 0);
	wrapper = LLVMAddFunction(ctx->module, "helium_main_wrapper",
				  wrapper_type);

	cmain_type = LLVMFunctionType(ctx->i32_type, NULL, 0, 0);
	cmain = LLVMAddFunction(ctx->module, "main", cmain_type);
	entry = LLVMAppendBasicBlockInContext(ctx->ctx, cmain, "entry");
	LLVMPositionBuilderAtEnd(ctx->builder, entry);
	call_args[0] = main_fn;
	LLVMBuildCall2(ctx->builder, wrapper_type, wrapper, call_args, 1, "");
	LLVMBuildRet(ctx->builder, LLVMConstInt(ctx->i32_type, 0, 0));
	return 0;
}

static int emit_object_file(LLVMModuleRef module, const char *path,
			    char **error)
{
	LLVMTargetRef target;
	LLVMTargetMachineRef tm;
	LLVMTargetDataRef td;
	char *triple;
	char *cpu;
	char *features;
	LLVMBool failed;

	LLVMInitializeAllTargetInfos();
	LLVMInitializeAllTargets();
	LLVMInitializeAllTargetMCs();
	LLVMInitializeAllAsmPrinters();
	LLVMInitializeAllAsmParsers();

	triple = LLVMGetDefaultTargetTriple();
	if (LLVMGetTargetFromTriple(triple, &target, error)) {
		free(triple);
		return -1;
	}

	cpu = LLVMGetHostCPUName();
	features = LLVMGetHostCPUFeatures();
	tm = LLVMCreateTargetMachine(target, triple, cpu, features,
				     LLVMCodeGenLevelDefault,
				     LLVMRelocDefault,
				     LLVMCodeModelDefault);
	free(cpu);
	free(features);
	if (!tm) {
		free(triple);
		format_error(error, "failed to create target machine");
		return -1;
	}

	td = LLVMCreateTargetDataLayout(tm);
	LLVMSetModuleDataLayout(module, td);
	LLVMSetTarget(module, triple);
	free(triple);

	failed = LLVMTargetMachineEmitToFile(tm, module, path, LLVMObjectFile,
					   error);
	LLVMDisposeTargetMachine(tm);
	if (failed)
		return -1;
	return 0;
}

static int build_module(struct helium_ir_program *prog,
			struct cg_ctx *ctx, LLVMModuleRef module,
			LLVMContextRef context, LLVMBuilderRef builder,
			char **error)
{
	size_t i;

	memset(ctx, 0, sizeof(*ctx));
	ctx->ctx = context;
	ctx->module = module;
	ctx->builder = builder;
	ctx->prog = prog;
	ctx->error = error;
	ctx->ptr_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
	ctx->i8_type = LLVMInt8TypeInContext(context);
	ctx->i32_type = LLVMInt32TypeInContext(context);
	ctx->i64_type = LLVMInt64TypeInContext(context);

	for (i = 0; i < prog->function_count; i++)
		declare_function(ctx, prog->functions[i]);

	for (i = 0; i < prog->function_count; i++) {
		if (codegen_function(ctx, prog->functions[i]) < 0)
			return -1;
	}

	if (prog->main_name && emit_main_wrapper(ctx) < 0) {
		format_error(error, "failed to emit main wrapper");
		return -1;
	}

	if (LLVMVerifyModule(module, LLVMAbortProcessAction, NULL)) {
		format_error(error, "LLVM module verification failed");
		return -1;
	}

	return 0;
}

int helium_codegen_program(struct helium_ir_program *prog,
			   const char *output_path, char **error)
{
	struct cg_ctx ctx;
	LLVMModuleRef module;
	LLVMContextRef context;
	LLVMBuilderRef builder;
	int rc;

	context = LLVMContextCreate();
	module = LLVMModuleCreateWithNameInContext("helium", context);
	builder = LLVMCreateBuilderInContext(context);

	rc = build_module(prog, &ctx, module, context, builder, error);
	if (rc == 0)
		rc = emit_object_file(module, output_path, error);

	if (ctx.bindings)
		free(ctx.bindings);
	LLVMDisposeBuilder(builder);
	LLVMDisposeModule(module);
	LLVMContextDispose(context);
	return rc;
}

int helium_codegen_program_ir(struct helium_ir_program *prog, FILE *out,
			      char **error)
{
	struct cg_ctx ctx;
	LLVMModuleRef module;
	LLVMContextRef context;
	LLVMBuilderRef builder;
	char *ir;
	int rc;

	context = LLVMContextCreate();
	module = LLVMModuleCreateWithNameInContext("helium", context);
	builder = LLVMCreateBuilderInContext(context);

	rc = build_module(prog, &ctx, module, context, builder, error);
	if (rc == 0) {
		ir = LLVMPrintModuleToString(module);
		if (!ir) {
			format_error(error, "failed to print LLVM IR");
			rc = -1;
		} else {
			fputs(ir, out);
			LLVMDisposeMessage(ir);
		}
	}

	if (ctx.bindings)
		free(ctx.bindings);
	LLVMDisposeBuilder(builder);
	LLVMDisposeModule(module);
	LLVMContextDispose(context);
	return rc;
}
