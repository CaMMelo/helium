/* SPDX-License-Identifier: TBD */
/*
 * mono.h - Monomorphization pass for Helium.
 */

#ifndef HELIUM_MONO_H
#define HELIUM_MONO_H

#include "inference.h"
#include "ir.h"

/* Convert a fully typed module into a monomorphic IR program.
 * Returns NULL and sets *error on failure.  The caller owns the
 * returned program and must free it with helium_ir_program_free().
 */
struct helium_ir_program *helium_monomorphize(struct helium_typed_module *typed,
					      char **error);

#endif /* HELIUM_MONO_H */
