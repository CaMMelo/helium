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
#include <time.h>
#include <unistd.h>

#include "ast.h"
#include "ast_printer.h"
#include "codegen.h"
#include "ffi.h"
#include "inference.h"
#include "ir.h"
#include "modules.h"
#include "mono.h"
#include "parser.h"
#include "token.h"

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

static char *replace_extension(const char *path, const char *new_ext)
{
	const char *dot = strrchr(path, '.');
	char *out;

	if (!dot)
		return xstrdup(new_ext);
	if (asprintf(&out, "%.*s%s", (int)(dot - path), path, new_ext) < 0)
		abort();
	return out;
}

static char *path_dirname(const char *path)
{
	const char *slash = strrchr(path, '/');

	if (!slash)
		return xstrdup(".");
	if (slash == path)
		return xstrdup("/");
	return strndup(path, slash - path);
}

static char *path_join(const char *a, const char *b)
{
	char *out;
	size_t len_a = strlen(a);
	int need_sep = len_a > 0 && a[len_a - 1] != '/';

	if (asprintf(&out, "%s%s%s", a, need_sep ? "/" : "", b) < 0)
		abort();
	return out;
}

static int file_exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

static int file_mtime(const char *path, time_t *out)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return -1;
	*out = st.st_mtime;
	return 0;
}

static int module_needs_compile(const char *source_path,
				const char *object_path,
				const char *iface_path)
{
	time_t src_mtime;
	time_t obj_mtime;
	time_t iface_mtime;

	if (!file_exists(object_path) || !file_exists(iface_path))
		return 1;
	if (file_mtime(source_path, &src_mtime) < 0)
		return 1;
	if (file_mtime(object_path, &obj_mtime) < 0)
		return 1;
	if (file_mtime(iface_path, &iface_mtime) < 0)
		return 1;
	return src_mtime > obj_mtime || src_mtime > iface_mtime;
}

/* -------------------------------------------------------------------------- */
/* Parsing                                                                    */
/* -------------------------------------------------------------------------- */

static int parse_source(const char *source_path, struct helium_module **module,
			char **error)
{
	FILE *f;
	struct helium_token *tokens;
	size_t count;
	struct helium_module *m;

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

	m = helium_parse_tokens(tokens, count, source_path, error);
	helium_tokens_free(tokens, count);
	if (!m)
		return -1;

	*module = m;
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Module compilation                                                         */
/* -------------------------------------------------------------------------- */

static int compile_module_source(const char *source_path,
				 const char *object_path,
				 const char *interface_path,
				 struct helium_import_context *parent_ctx,
				 const char *const *module_paths,
				 struct helium_module_interface **iface_out,
				 char **error);

static void inject_imported_decls(struct helium_module *module,
				  struct helium_import_context *ctx);

static struct helium_search_path *build_search_path(const char *source_path,
					    const char *const *module_paths)
{
	struct helium_search_path *sp;
	size_t i;

	sp = helium_search_path_for_project(source_path, NULL);
	if (!sp)
		return NULL;

	for (i = 0; module_paths && module_paths[i]; i++)
		helium_search_path_add(sp, module_paths[i]);

	return sp;
}

static int collect_imports(struct helium_module *module,
			   const char *source_path,
			   struct helium_import_context *ctx,
			   const char *const *module_paths,
			   char **error)
{
	struct helium_search_path *sp;
	size_t i;
	int rc = -1;

	sp = build_search_path(source_path, module_paths);
	if (!sp)
		return -1;

	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];
		struct helium_import_info *info;
		char *source;
		char *mod_name;
		char *object;
		char *iface;

		if (decl->kind != HELIUM_DECL_IMPORT)
			continue;

		if (helium_module_resolve(decl->u.import->path, sp, &source,
					  &mod_name, error) < 0)
			goto out;

		object = replace_extension(source, ".o");
		iface = replace_extension(source, ".hei");

		info = helium_import_context_add(ctx, decl->u.import->path,
						 mod_name, source, object, iface);
		free(mod_name);

		if (!module_needs_compile(source, object, iface)) {
			info->iface = helium_module_interface_load(iface, error);
			if (!info->iface)
				goto out;
		} else {
			struct helium_module_interface *dep_iface;

			if (compile_module_source(source, object, iface, ctx,
						  module_paths, &dep_iface, error) < 0)
				goto out;
			info->iface = dep_iface;
		}

		free(source);
		free(object);
		free(iface);
	}

	rc = 0;
out:
	helium_search_path_free(sp);
	return rc;
}

static int compile_module_source(const char *source_path,
				 const char *object_path,
				 const char *interface_path,
				 struct helium_import_context *parent_ctx,
				 const char *const *module_paths,
				 struct helium_module_interface **iface_out,
				 char **error)
{
	struct helium_module *module = NULL;
	struct helium_typed_module *typed = NULL;
	struct helium_ir_program *prog = NULL;
	struct helium_import_context *ctx = NULL;
	struct helium_module_interface *iface = NULL;
	char *module_name;
	int rc = -1;

	if (iface_out)
		*iface_out = NULL;

	if (parse_source(source_path, &module, error) < 0)
		return -1;

	ctx = helium_import_context_new();
	if (collect_imports(module, source_path, ctx, module_paths, error) < 0)
		goto out;

	if (parent_ctx) {
		size_t i;

		for (i = 0; i < ctx->object_count; i++)
			helium_import_context_add_object(parent_ctx,
							 ctx->object_paths[i]);
	}

	module_name = module->name ? xstrdup(module->name) :
			     replace_extension(source_path, "");
	{
		char *base = strrchr(module_name, '/');
		char *tmp;

		if (base) {
			tmp = xstrdup(base + 1);
			free(module_name);
			module_name = tmp;
		}
	}

	helium_rewrite_qualified_access(module, ctx);
	if (parent_ctx)
		helium_rewrite_qualified_access(module, parent_ctx);
	inject_imported_decls(module, ctx);

	if (helium_infer_module_no_main(module, &typed, error) < 0)
		goto out;


	if (helium_module_interface_emit(module, typed->env, interface_path,
					 error) < 0) {
		free(module_name);
		goto out;
	}

	iface = helium_module_interface_load(interface_path, error);
	if (!iface) {
		free(module_name);
		goto out;
	}
	iface->source_path = xstrdup(source_path);
	iface->object_path = xstrdup(object_path);

	helium_typed_module_free(typed);
	typed = NULL;

	helium_prefix_module_names(module, module_name);

	if (helium_infer_module_no_main(module, &typed, error) < 0) {
		free(module_name);
		goto out;
	}

	prog = helium_monomorphize_module(typed, error);
	if (!prog) {
		free(module_name);
		goto out;
	}

	if (helium_codegen_program(prog, object_path, error) < 0) {
		free(module_name);
		goto out;
	}

	if (iface_out) {
		*iface_out = iface;
		iface = NULL;
	} else {
		helium_module_interface_free(iface);
	}
	iface = NULL;

	free(module_name);
	rc = 0;
out:
	if (iface)
		helium_module_interface_free(iface);
	if (prog)
		helium_ir_program_free(prog);
	if (typed)
		helium_typed_module_free(typed);
	if (module)
		helium_module_free(module);
	helium_import_context_free(ctx);
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Inject imported declarations into the entry module                         */
/* -------------------------------------------------------------------------- */

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

static void inject_imported_decls(struct helium_module *module,
				  struct helium_import_context *ctx)
{
	size_t i;
	size_t j;

	for (i = 0; i < ctx->count; i++) {
		struct helium_import_info *info = ctx->imports[i];
		struct helium_module_interface *iface = info->iface;

		if (!iface)
			continue;
		for (j = 0; j < iface->type_def_count; j++) {
			struct helium_type_def *def = iface->type_defs[j];
			struct helium_top_decl *decl;
			size_t k;
			int found = 0;

			/* Local or previously injected defs win; first wins. */
			for (k = 0; k < module->decl_count; k++) {
				struct helium_top_decl *existing =
					module->decls[k];

				if (existing->kind == HELIUM_DECL_TYPE &&
				    strcmp(existing->u.type_def->name,
					   def->name) == 0) {
					found = 1;
					break;
				}
			}
			if (found)
				continue;
			decl = helium_decl_type(helium_type_def_copy(def));
			decl->u.type_def->injected = 1;
			module_insert_decl_at(module, decl, 0);
		}
		for (j = 0; j < iface->export_count; j++) {
			struct helium_interface_export *exp = iface->exports[j];
			char *mangled;
			struct helium_top_decl *decl;

			if (asprintf(&mangled, "%s_%s", info->prefix,
				     exp->name) < 0)
				abort();
			decl = helium_decl_foreign(mangled,
						   exp->type_params,
						   exp->type_param_count,
						   helium_type_copy(exp->type),
						   0, 0);
			decl->u.foreign.injected = 1;
			free(mangled);
			module_insert_decl_at(module, decl, 0);
		}
	}
}

/* -------------------------------------------------------------------------- */
/* Linking                                                                    */
/* -------------------------------------------------------------------------- */

static char *build_extra_libs(const struct helium_compile_options *opts)
{
	char *result = NULL;
	char *tmp;
	size_t i;

	if (opts->extra_libs && opts->extra_libs[0]) {
		result = xstrdup(opts->extra_libs);
	}

	for (i = 0; opts->lib_paths && opts->lib_paths[i]; i++) {
		if (result) {
			if (asprintf(&tmp, "%s -L%s", result, opts->lib_paths[i]) < 0)
				abort();
			free(result);
			result = tmp;
		} else {
			if (asprintf(&result, "-L%s", opts->lib_paths[i]) < 0)
				abort();
		}
	}

	for (i = 0; opts->libs && opts->libs[i]; i++) {
		if (result) {
			if (asprintf(&tmp, "%s -l%s", result, opts->libs[i]) < 0)
				abort();
			free(result);
			result = tmp;
		} else {
			if (asprintf(&result, "-l%s", opts->libs[i]) < 0)
				abort();
		}
	}

	return result;
}

static int run_linker(const char *obj_path, const char *output_path,
		      const char *extra_libs,
		      struct helium_import_context *ctx, char **error,
		      const char *runtime_source_path)
{
	char runtime_obj[4096];
	const char *runtime_src = runtime_source_path ?
				  runtime_source_path :
				  "src/runtime/helium_runtime.c";
	pid_t pid;
	int status;
	size_t i;
	char **argv;
	int argc;
	int cap;
	int rc = -1;

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

	cap = 16 + (ctx ? (int)ctx->object_count : 0);
	argv = xalloc(cap * sizeof(*argv));
	argc = 0;
	argv[argc++] = xstrdup("cc");
	argv[argc++] = xstrdup("-no-pie");
	argv[argc++] = xstrdup("-O2");
	argv[argc++] = xstrdup("-g");
	argv[argc++] = xstrdup(obj_path);
	argv[argc++] = xstrdup(runtime_obj);

	if (ctx) {
		for (i = 0; i < ctx->object_count; i++)
			argv[argc++] = xstrdup(ctx->object_paths[i]);
	}

	argv[argc++] = xstrdup("-o");
	argv[argc++] = xstrdup(output_path);

	if (extra_libs && extra_libs[0]) {
		char *libs = xstrdup(extra_libs);
		char *tok = strtok(libs, " \t");

		while (tok) {
			if (argc + 1 >= cap) {
				cap *= 2;
				argv = realloc(argv, cap * sizeof(*argv));
				if (!argv)
					abort();
			}
			argv[argc++] = xstrdup(tok);
			tok = strtok(NULL, " \t");
		}
		free(libs);
	}
	argv[argc] = NULL;

	pid = fork();
	if (pid < 0) {
		format_error(error, "fork failed");
		goto out;
	}
	if (pid == 0) {
		execvp("cc", argv);
		_exit(127);
	}
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		format_error(error, "linking failed");
		goto out;
	}
	rc = 0;

out:
	for (i = 0; i < (size_t)argc; i++)
		free(argv[i]);
	free(argv);
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Entry-point compilation                                                    */
/* -------------------------------------------------------------------------- */

static int compile_program(struct helium_module *module,
			   const char *source_path,
			   const struct helium_compile_options *opts,
			   char **error)
{
	struct helium_typed_module *typed = NULL;
	struct helium_ir_program *prog = NULL;
	struct helium_import_context *ctx = NULL;
	char *obj_path = NULL;
	char *heliumfile;
	char *link_flags = NULL;
	char *combined_libs = NULL;
	char *extra_libs = NULL;
	int rc = -1;

	extra_libs = build_extra_libs(opts);

	ctx = helium_import_context_new();
	if (collect_imports(module, source_path, ctx, opts->module_paths, error) < 0)
		goto out;

	helium_rewrite_qualified_access(module, ctx);
	inject_imported_decls(module, ctx);

	if (helium_infer_module(module, &typed, error) < 0)
		goto out;

	prog = helium_monomorphize(typed, error);
	if (!prog)
		goto out;

	obj_path = xalloc(strlen(opts->output_path) + 5);
	sprintf(obj_path, "%s.o", opts->output_path);

	if (helium_codegen_program(prog, obj_path, error) < 0)
		goto out;

	{
		char *root = path_dirname(source_path);

		heliumfile = path_join(root, "Heliumfile");
		link_flags = helium_ffi_read_link_flags(heliumfile);
		free(heliumfile);
		free(root);
	}

	if (extra_libs && extra_libs[0] && link_flags && link_flags[0]) {
		if (asprintf(&combined_libs, "%s %s", extra_libs, link_flags) < 0)
			abort();
	} else if (extra_libs && extra_libs[0]) {
		combined_libs = xstrdup(extra_libs);
	} else if (link_flags && link_flags[0]) {
		combined_libs = xstrdup(link_flags);
	}

	if (run_linker(obj_path, opts->output_path, combined_libs, ctx, error,
		       opts->runtime_source_path) < 0)
		goto out;

	rc = 0;
out:
	free(extra_libs);
	free(combined_libs);
	free(link_flags);
	if (obj_path) {
		/* unlink(obj_path); */
		free(obj_path);
	}
	if (prog)
		helium_ir_program_free(prog);
	if (typed)
		helium_typed_module_free(typed);
	helium_import_context_free(ctx);
	return rc;
}

int helium_compile_file(const char *source_path, const char *output_path,
			const char *extra_libs, char **error)
{
	struct helium_compile_options opts = {
		.output_path = output_path,
		.extra_libs = extra_libs,
	};

	return helium_compile(source_path, &opts, error);
}

int helium_compile(const char *source_path,
		   const struct helium_compile_options *opts, char **error)
{
	struct helium_module *module;
	int rc;

	if (!opts || !opts->output_path) {
		format_error(error, "output path is required");
		return -1;
	}

	if (parse_source(source_path, &module, error) < 0)
		return -1;

	rc = compile_program(module, source_path, opts, error);
	helium_module_free(module);
	return rc;
}

/*
 * helium_compile_module - Compile a single Helium module to object + interface.
 *
 * @source_path: path to the module's .hel source file.
 * @object_path: path where the compiled .o object file should be written.
 * @interface_path: path where the .hei interface file should be written.
 * @module_paths: extra search paths for imports, NULL-terminated.
 * @iface_out: optional output for the loaded module interface.
 * @error: set to a heap-allocated error message on failure.
 *
 * Returns 0 on success, -1 on failure.
 */
int helium_compile_module(const char *source_path,
                          const char *object_path,
                          const char *interface_path,
                          const char *const *module_paths,
                          struct helium_module_interface **iface_out,
                          char **error)
{
	return compile_module_source(source_path, object_path, interface_path,
				     NULL, module_paths, iface_out, error);
}

/* -------------------------------------------------------------------------- */
/* Emit passes                                                                */
/* -------------------------------------------------------------------------- */

int helium_emit_ast(const char *source_path, FILE *out, char **error)
{
	struct helium_module *module = NULL;
	int rc;

	if (parse_source(source_path, &module, error) < 0)
		return -1;

	helium_ast_print(out, module);
	rc = ferror(out) ? -1 : 0;
	if (rc < 0)
		format_error(error, "failed to write AST output");

	helium_module_free(module);
	return rc;
}

int helium_emit_ir(const char *source_path, FILE *out,
		   const char *const *module_paths, char **error)
{
	struct helium_module *module = NULL;
	struct helium_typed_module *typed = NULL;
	struct helium_import_context *ctx = NULL;
	struct helium_ir_program *prog = NULL;
	int rc = -1;

	if (parse_source(source_path, &module, error) < 0)
		return -1;

	ctx = helium_import_context_new();
	if (collect_imports(module, source_path, ctx, module_paths, error) < 0)
		goto out;

	helium_rewrite_qualified_access(module, ctx);
	inject_imported_decls(module, ctx);

	if (helium_infer_module(module, &typed, error) < 0)
		goto out;

	prog = helium_monomorphize(typed, error);
	if (!prog)
		goto out;

	helium_ir_program_print(prog, out);
	rc = ferror(out) ? -1 : 0;
	if (rc < 0)
		format_error(error, "failed to write IR output");

out:
	if (prog)
		helium_ir_program_free(prog);
	if (typed)
		helium_typed_module_free(typed);
	if (module)
		helium_module_free(module);
	helium_import_context_free(ctx);
	return rc;
}

int helium_emit_llvm(const char *source_path, FILE *out,
		     const char *const *module_paths, char **error)
{
	struct helium_module *module = NULL;
	struct helium_typed_module *typed = NULL;
	struct helium_import_context *ctx = NULL;
	struct helium_ir_program *prog = NULL;
	int rc = -1;

	if (parse_source(source_path, &module, error) < 0)
		return -1;

	ctx = helium_import_context_new();
	if (collect_imports(module, source_path, ctx, module_paths, error) < 0)
		goto out;

	helium_rewrite_qualified_access(module, ctx);
	inject_imported_decls(module, ctx);

	if (helium_infer_module(module, &typed, error) < 0)
		goto out;

	prog = helium_monomorphize(typed, error);
	if (!prog)
		goto out;

	if (helium_codegen_program_ir(prog, out, error) < 0)
		goto out;

	rc = ferror(out) ? -1 : 0;
	if (rc < 0)
		format_error(error, "failed to write LLVM IR output");

out:
	if (prog)
		helium_ir_program_free(prog);
	if (typed)
		helium_typed_module_free(typed);
	if (module)
		helium_module_free(module);
	helium_import_context_free(ctx);
	return rc;
}
