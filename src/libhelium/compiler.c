/* SPDX-License-Identifier: TBD */
/*
 * compiler.c - High-level compile function for Helium source files.
 */

#include "compiler.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ast.h"
#include "codegen.h"
#include "inference.h"
#include "ir.h"
#include "mono.h"
#include "parser.h"
#include "token.h"

struct compile_options {
	const char *output_path;
	const char *extra_libs;
	int emit_llvm;
};

static void *xalloc(size_t size)
{
	void *p = calloc(1, size);

	if (!p)
		abort();
	return p;
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
		*error = strdup("compile error");
		return;
	}
	*error = msg;
}

/* -------------------------------------------------------------------------- */
/* std.io bootstrap stub                                                      */
/* -------------------------------------------------------------------------- */

static struct helium_type *make_io_unit_type(void)
{
	struct helium_type *io;

	io = helium_type_named("IO", 0, 0);
	helium_type_add_arg(io, helium_type_named("()", 0, 0));
	return io;
}

static struct helium_top_decl *make_io_println_foreign(void)
{
	struct helium_type *fn_type;
	struct helium_type *str_type;

	str_type = helium_type_named("str", 0, 0);
	fn_type = helium_type_fn(0, 0);
	helium_type_add_param(fn_type, str_type);
	helium_type_set_ret(fn_type, make_io_unit_type());
	return helium_decl_foreign("io_println", fn_type, 0, 0);
}

static struct helium_top_decl *make_io_prints_foreign(void)
{
	struct helium_type *fn_type;
	struct helium_type *str_type;

	str_type = helium_type_named("str", 0, 0);
	fn_type = helium_type_fn(0, 0);
	helium_type_add_param(fn_type, str_type);
	helium_type_set_ret(fn_type, make_io_unit_type());
	return helium_decl_foreign("io_prints", fn_type, 0, 0);
}

static struct helium_top_decl *make_io_printi_foreign(void)
{
	struct helium_type *fn_type;

	fn_type = helium_type_fn(0, 0);
	helium_type_add_param(fn_type, helium_type_named("i32", 0, 0));
	helium_type_set_ret(fn_type, make_io_unit_type());
	return helium_decl_foreign("io_printi", fn_type, 0, 0);
}

static int expr_is_io_field(struct helium_expr *expr, const char **field)
{
	if (!expr || expr->kind != HELIUM_EXPR_FIELD)
		return 0;
	if (!expr->u.field.object ||
	    expr->u.field.object->kind != HELIUM_EXPR_IDENT)
		return 0;
	if (strcmp(expr->u.field.object->u.ident.name, "io") != 0)
		return 0;
	*field = expr->u.field.name;
	return 1;
}

static void rewrite_io_field(struct helium_expr *expr)
{
	const char *field;
	char *name;

	if (!expr)
		return;

	if (expr_is_io_field(expr, &field)) {
		name = xalloc(strlen(field) + 4);
		sprintf(name, "io_%s", field);
		free(expr->u.field.object);
		free(expr->u.field.name);
		expr->kind = HELIUM_EXPR_IDENT;
		expr->u.ident.name = name;
		return;
	}

	switch (expr->kind) {
	case HELIUM_EXPR_BINARY:
		rewrite_io_field(expr->u.binary.left);
		rewrite_io_field(expr->u.binary.right);
		break;
	case HELIUM_EXPR_UNARY:
		rewrite_io_field(expr->u.unary.operand);
		break;
	case HELIUM_EXPR_CALL: {
		size_t i;

		rewrite_io_field(expr->u.call.func);
		for (i = 0; i < expr->u.call.arg_count; i++)
			rewrite_io_field(expr->u.call.args[i]);
		break;
	}
	case HELIUM_EXPR_BLOCK: {
		size_t i;

		for (i = 0; i < expr->u.block.binding_count; i++)
			rewrite_io_field(expr->u.block.bindings[i]->value);
		for (i = 0; i < expr->u.block.expr_count; i++)
			rewrite_io_field(expr->u.block.exprs[i]);
		break;
	}
	case HELIUM_EXPR_IF:
		rewrite_io_field(expr->u.if_expr.cond);
		rewrite_io_field(expr->u.if_expr.then_branch);
		rewrite_io_field(expr->u.if_expr.else_branch);
		break;
	case HELIUM_EXPR_MATCH: {
		size_t i;

		rewrite_io_field(expr->u.match.value);
		for (i = 0; i < expr->u.match.arm_count; i++)
			rewrite_io_field(expr->u.match.arms[i]->expr);
		break;
	}
	case HELIUM_EXPR_LOOP: {
		size_t i;

		for (i = 0; i < expr->u.loop.binding_count; i++)
			rewrite_io_field(expr->u.loop.bindings[i]->init);
		rewrite_io_field(expr->u.loop.body);
		break;
	}
	case HELIUM_EXPR_RECUR: {
		size_t i;

		for (i = 0; i < expr->u.recur.arg_count; i++)
			rewrite_io_field(expr->u.recur.args[i]);
		break;
	}
	case HELIUM_EXPR_LAMBDA:
		rewrite_io_field(expr->u.lambda.body);
		break;
	case HELIUM_EXPR_RECORD_LIT: {
		size_t i;

		for (i = 0; i < expr->u.record_lit.field_count; i++)
			rewrite_io_field(expr->u.record_lit.fields[i]->value);
		break;
	}
	case HELIUM_EXPR_ARRAY_LIT: {
		size_t i;

		for (i = 0; i < expr->u.array_lit.item_count; i++)
			rewrite_io_field(expr->u.array_lit.items[i]);
		break;
	}
	case HELIUM_EXPR_FIELD:
		rewrite_io_field(expr->u.field.object);
		break;
	case HELIUM_EXPR_ANNOT:
		rewrite_io_field(expr->u.annot.expr);
		break;
	case HELIUM_EXPR_BIND:
		rewrite_io_field(expr->u.bind.left);
		rewrite_io_field(expr->u.bind.right);
		break;
	case HELIUM_EXPR_RETURN:
		rewrite_io_field(expr->u.ret.expr);
		break;
	case HELIUM_EXPR_FSTRING: {
		size_t i;

		for (i = 0; i < expr->u.fstring.part_count; i++) {
			struct helium_fstring_part *p = expr->u.fstring.parts[i];

			if (p->is_expr)
				rewrite_io_field(p->u.expr);
		}
		break;
	}
	default:
		break;
	}
}

static void module_insert_decl_at(struct helium_module *module,
				  struct helium_top_decl *decl, size_t idx)
{
	size_t i;

	if (module->decl_count == module->decl_capacity) {
		size_t newcap = module->decl_capacity ?
				module->decl_capacity * 2 : 4;
		struct helium_top_decl **tmp = realloc(module->decls,
						       newcap * sizeof(*tmp));

		if (!tmp)
			abort();
		module->decls = tmp;
		module->decl_capacity = newcap;
	}
	for (i = module->decl_count; i > idx; i--)
		module->decls[i] = module->decls[i - 1];
	module->decls[idx] = decl;
	module->decl_count++;
}

static void rewrite_std_io_fields(struct helium_module *module)
{
	size_t i;

	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];

		if (decl->kind == HELIUM_DECL_BINDING)
			rewrite_io_field(decl->u.binding->value);
	}
}

static void handle_std_io_import(struct helium_module *module)
{
	size_t i;
	int has_import = 0;
	struct helium_top_decl *foreigns[3];

	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];

		if (decl->kind == HELIUM_DECL_IMPORT &&
		    strcmp(decl->u.import->path, "std.io") == 0) {
			has_import = 1;
			break;
		}
	}

	if (!has_import)
		return;

	rewrite_std_io_fields(module);

	foreigns[0] = make_io_println_foreign();
	foreigns[1] = make_io_prints_foreign();
	foreigns[2] = make_io_printi_foreign();
	for (i = 0; i < 3; i++)
		module_insert_decl_at(module, foreigns[i], 0);
}

/* -------------------------------------------------------------------------- */
/* Compile pipeline                                                           */
/* -------------------------------------------------------------------------- */

static int run_linker(const char *obj_path, const char *output_path,
		      const char *extra_libs, char **error)
{
	char runtime_obj[4096];
	const char *runtime_src = "src/runtime/helium_runtime.c";
	pid_t pid;
	int status;

	snprintf(runtime_obj, sizeof(runtime_obj), "%s.runtime.o", output_path);

	pid = fork();
	if (pid < 0) {
		format_error(error, "fork failed");
		return -1;
	}
	if (pid == 0) {
		execlp("cc", "cc", "-c", "-O2", "-g", runtime_src, "-o",
		       runtime_obj, (char *)NULL);
		_exit(127);
	}
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		format_error(error, "failed to compile runtime");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		format_error(error, "fork failed");
		return -1;
	}
	if (pid == 0) {
		if (extra_libs && extra_libs[0])
			execlp("cc", "cc", "-no-pie", "-O2", "-g", obj_path,
			       runtime_obj, "-o", output_path, extra_libs,
			       (char *)NULL);
		else
			execlp("cc", "cc", "-no-pie", "-O2", "-g", obj_path,
			       runtime_obj, "-o", output_path, (char *)NULL);
		_exit(127);
	}
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		format_error(error, "linking failed");
		return -1;
	}
	return 0;
}

static int validate_imports(struct helium_module *module, char **error)
{
	size_t i;

	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];
		const char *path;

		if (decl->kind != HELIUM_DECL_IMPORT)
			continue;
		path = decl->u.import->path;
		if (strcmp(path, "std.io") != 0) {
			format_error(error, "module not found: %s", path);
			return -1;
		}
	}
	return 0;
}

static int compile_module(struct helium_module *module,
			  const char *output_path,
			  const char *extra_libs, char **error)
{
	struct helium_typed_module *typed = NULL;
	struct helium_ir_program *prog = NULL;
	char *obj_path = NULL;
	int rc = -1;

	if (validate_imports(module, error) < 0)
		goto out;

	handle_std_io_import(module);

	if (helium_infer_module(module, &typed, error) < 0)
		goto out;

	prog = helium_monomorphize(typed, error);
	if (!prog)
		goto out;

	obj_path = xalloc(strlen(output_path) + 5);
	sprintf(obj_path, "%s.o", output_path);

	if (helium_codegen_program(prog, obj_path, error) < 0)
		goto out;

	if (run_linker(obj_path, output_path, extra_libs, error) < 0)
		goto out;

	rc = 0;
out:
	if (obj_path) {
		/* unlink(obj_path); */
		free(obj_path);
	}
	if (prog)
		helium_ir_program_free(prog);
	if (typed)
		helium_typed_module_free(typed);
	return rc;
}

int helium_compile_file(const char *source_path, const char *output_path,
			const char *extra_libs, char **error)
{
	FILE *f;
	struct helium_token *tokens;
	size_t count;
	struct helium_module *module;
	int rc;

	f = fopen(source_path, "r");
	if (!f) {
		format_error(error, "%s: %m", source_path);
		return -1;
	}

	if (helium_lex_file(f, &tokens, &count, error) < 0) {
		fclose(f);
		return -1;
	}
	fclose(f);

	module = helium_parse_tokens(tokens, count, source_path, error);
	helium_tokens_free(tokens, count);
	if (!module)
		return -1;

	rc = compile_module(module, output_path, extra_libs, error);
	helium_module_free(module);
	return rc;
}
