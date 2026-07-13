/* SPDX-License-Identifier: TBD */
/*
 * compiler.h - High-level compile function for Helium source files.
 */

#ifndef HELIUM_COMPILER_H
#define HELIUM_COMPILER_H

#include <stdio.h>

struct helium_compile_options {
	const char *output_path;
	const char *extra_libs;
	const char *const *module_paths; /* -I paths, NULL-terminated */
	const char *const *lib_paths;    /* -L paths, NULL-terminated */
	const char *const *libs;         /* -l libs, NULL-terminated */
	const char *runtime_source_path; /* path to helium_runtime.c, may be NULL */
};

/*
 * helium_compile_file - Compile a Helium source file to an executable.
 *
 * @source_path: path to the .hel source file.
 * @output_path: path for the resulting executable.
 * @extra_libs: extra libraries to link (e.g. "-lm"), may be NULL.
 * @error: set to a heap-allocated error message on failure.
 *
 * Returns 0 on success, -1 on failure.
 */
int helium_compile_file(const char *source_path, const char *output_path,
                        const char *extra_libs, char **error);

/*
 * helium_compile - Compile a Helium source file to an executable with options.
 *
 * @source_path: path to the .hel source file.
 * @opts: compilation options.  Only @output_path is required.
 * @error: set to a heap-allocated error message on failure.
 *
 * Returns 0 on success, -1 on failure.
 */
int helium_compile(const char *source_path,
                   const struct helium_compile_options *opts, char **error);

/*
 * helium_emit_ast - Parse and print the AST for a Helium source file.
 *
 * Returns 0 on success, -1 on failure.
 */
int helium_emit_ast(const char *source_path, FILE *out, char **error);

/*
 * helium_emit_ir - Type-check, monomorphize, and print the IR for a Helium
 * source file.  Extra module search paths may be NULL.
 *
 * Returns 0 on success, -1 on failure.
 */
int helium_emit_ir(const char *source_path, FILE *out,
                   const char *const *module_paths, char **error);

/*
 * helium_emit_llvm - Compile a Helium source file through monomorphization and
 * print the generated LLVM IR.  Extra module search paths may be NULL.
 *
 * Returns 0 on success, -1 on failure.
 */
int helium_emit_llvm(const char *source_path, FILE *out,
                     const char *const *module_paths, char **error);

#endif /* HELIUM_COMPILER_H */
