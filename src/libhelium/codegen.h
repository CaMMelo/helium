/* SPDX-License-Identifier: TBD */
/*
 * codegen.h - LLVM code generation from monomorphic IR.
 */

#ifndef HELIUM_CODEGEN_H
#define HELIUM_CODEGEN_H

#include <stdio.h>

#include "ir.h"

/*
 * helium_codegen_program - Generate an object file from a monomorphic IR
 * program.
 *
 * On success returns 0 and writes the object file to @output_path.
 * On failure returns -1 and sets *error to a heap-allocated message.
 */
int helium_codegen_program(struct helium_ir_program *prog,
                           const char *output_path, char **error);

/*
 * helium_codegen_program_ir - Generate LLVM IR from a monomorphic IR program
 * and print it to @out.
 *
 * On success returns 0.  On failure returns -1 and sets *error to a
 * heap-allocated message.
 */
int helium_codegen_program_ir(struct helium_ir_program *prog, FILE *out,
                              char **error);

#endif /* HELIUM_CODEGEN_H */
