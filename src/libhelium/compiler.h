/* SPDX-License-Identifier: TBD */
/*
 * compiler.h - High-level compile function for Helium source files.
 */

#ifndef HELIUM_COMPILER_H
#define HELIUM_COMPILER_H

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

#endif /* HELIUM_COMPILER_H */
