/* SPDX-License-Identifier: TBD */
/*
 * main.c - Helium compiler driver.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"

extern const char *helium_version(void);

enum driver_action {
	ACTION_COMPILE,
	ACTION_EMIT_AST,
	ACTION_EMIT_IR,
	ACTION_EMIT_LLVM,
	ACTION_VERSION,
};

struct driver_options {
	const char *input;
	const char *output;
	const char **module_paths;
	size_t module_path_count;
	size_t module_path_capacity;
	const char **lib_paths;
	size_t lib_path_count;
	size_t lib_path_capacity;
	const char **libs;
	size_t lib_count;
	size_t lib_capacity;
	enum driver_action action;
};

static void *xalloc(size_t size)
{
	void *p = calloc(1, size);

	if (!p)
		abort();
	return p;
}

static void add_string(const char ***items, size_t *count, size_t *capacity,
		       const char *value)
{
	if (*count == *capacity) {
		size_t newcap = *capacity ? *capacity * 2 : 4;
		const char **tmp = realloc(*items, newcap * sizeof(*tmp));

		if (!tmp)
			abort();
		*items = tmp;
		*capacity = newcap;
	}
	(*items)[(*count)++] = value;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s <input.hel> [-o <output>] [-I <path>]... [-L <path>]... [-l <lib>]...\n"
		"       %s --emit-ast <input.hel>\n"
		"       %s --emit-ir <input.hel>\n"
		"       %s --emit-llvm <input.hel>\n"
		"       %s {-v|--version}\n",
		prog, prog, prog, prog, prog);
}

static char *default_output_path(const char *input)
{
	char *out;
	const char *dot = strrchr(input, '.');

	if (dot && strcmp(dot, ".hel") == 0) {
		if (asprintf(&out, "%.*s", (int)(dot - input), input) < 0)
			abort();
		return out;
	}
	if (asprintf(&out, "%s.out", input) < 0)
		abort();
	return out;
}

static int parse_options(int argc, char *argv[], struct driver_options *opts)
{
	int i;

	memset(opts, 0, sizeof(*opts));
	opts->action = ACTION_COMPILE;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (strcmp(arg, "-o") == 0) {
			if (i + 1 >= argc)
				return -1;
			opts->output = argv[++i];
		} else if (strcmp(arg, "-I") == 0) {
			if (i + 1 >= argc)
				return -1;
			add_string(&opts->module_paths, &opts->module_path_count,
				   &opts->module_path_capacity, argv[++i]);
		} else if (strcmp(arg, "-L") == 0) {
			if (i + 1 >= argc)
				return -1;
			add_string(&opts->lib_paths, &opts->lib_path_count,
				   &opts->lib_path_capacity, argv[++i]);
		} else if (strcmp(arg, "-l") == 0) {
			if (i + 1 >= argc)
				return -1;
			add_string(&opts->libs, &opts->lib_count,
				   &opts->lib_capacity, argv[++i]);
		} else if (strcmp(arg, "--emit-ast") == 0) {
			opts->action = ACTION_EMIT_AST;
		} else if (strcmp(arg, "--emit-ir") == 0) {
			opts->action = ACTION_EMIT_IR;
		} else if (strcmp(arg, "--emit-llvm") == 0) {
			opts->action = ACTION_EMIT_LLVM;
		} else if (strcmp(arg, "-v") == 0 ||
			   strcmp(arg, "--version") == 0) {
			opts->action = ACTION_VERSION;
		} else if (arg[0] == '-') {
			return -1;
		} else {
			opts->input = arg;
		}
	}

	if (opts->action == ACTION_VERSION)
		return 0;

	if (!opts->input)
		return -1;

	if (opts->action == ACTION_COMPILE && !opts->output)
		opts->output = default_output_path(opts->input);

	return 0;
}

static void free_options(struct driver_options *opts)
{
	free(opts->module_paths);
	free(opts->lib_paths);
	free(opts->libs);
}

static int make_argv_terminated(const char **items, size_t count,
				const char ***out)
{
	const char **arr;

	if (count == 0) {
		*out = NULL;
		return 0;
	}

	arr = xalloc((count + 1) * sizeof(*arr));
	memcpy(arr, items, count * sizeof(*arr));
	arr[count] = NULL;
	*out = arr;
	return 1;
}

static int run_compile(const struct driver_options *opts)
{
	struct helium_compile_options copts;
	char *error;
	int rc;
	const char **module_paths = NULL;
	const char **lib_paths = NULL;
	const char **libs = NULL;
	int free_module_paths = 0;
	int free_lib_paths = 0;
	int free_libs = 0;

	memset(&copts, 0, sizeof(copts));
	copts.output_path = opts->output;
	free_module_paths = make_argv_terminated(opts->module_paths,
						 opts->module_path_count,
						 &module_paths);
	free_lib_paths = make_argv_terminated(opts->lib_paths,
					      opts->lib_path_count,
					      &lib_paths);
	free_libs = make_argv_terminated(opts->libs, opts->lib_count, &libs);
	copts.module_paths = module_paths;
	copts.lib_paths = lib_paths;
	copts.libs = libs;

	rc = helium_compile(opts->input, &copts, &error);
	if (rc < 0) {
		fprintf(stderr, "error: %s\n", error ? error : "compilation failed");
		free(error);
	}

	if (free_module_paths)
		free(module_paths);
	if (free_lib_paths)
		free(lib_paths);
	if (free_libs)
		free(libs);
	return rc;
}

static int run_emit(const struct driver_options *opts)
{
	const char **module_paths = NULL;
	int free_module_paths = 0;
	char *error;
	int rc;

	free_module_paths = make_argv_terminated(opts->module_paths,
						 opts->module_path_count,
						 &module_paths);

	switch (opts->action) {
	case ACTION_EMIT_AST:
		rc = helium_emit_ast(opts->input, stdout, &error);
		break;
	case ACTION_EMIT_IR:
		rc = helium_emit_ir(opts->input, stdout, module_paths, &error);
		break;
	case ACTION_EMIT_LLVM:
		rc = helium_emit_llvm(opts->input, stdout, module_paths, &error);
		break;
	default:
		if (free_module_paths)
			free(module_paths);
		return -1;
	}

	if (free_module_paths)
		free(module_paths);

	if (rc < 0) {
		fprintf(stderr, "error: %s\n", error ? error : "compilation failed");
		free(error);
	}
	return rc;
}

int main(int argc, char *argv[])
{
	struct driver_options opts;
	int rc;

	if (parse_options(argc, argv, &opts) < 0) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	if (opts.action == ACTION_VERSION) {
		printf("Helium compiler version %s\n", helium_version());
		free_options(&opts);
		return EXIT_SUCCESS;
	}

	if (opts.action == ACTION_COMPILE)
		rc = run_compile(&opts);
	else
		rc = run_emit(&opts);

	free_options(&opts);
	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
