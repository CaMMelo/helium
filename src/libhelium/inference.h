/* SPDX-License-Identifier: TBD */
/*
 * inference.h - Type inference engine for Helium.
 */

#ifndef HELIUM_INFERENCE_H
#define HELIUM_INFERENCE_H

#include "ast.h"
#include "type_env.h"

struct helium_typed_module {
	struct helium_module *module;
	struct helium_env *env;
	struct helium_subst *subst;
};

/* Type-check a module and return a fully typed view.
 * On success the caller owns *out and must free it with
 * helium_typed_module_free().  On error *out is NULL and *error
 * contains a message.
 */
int helium_infer_module(struct helium_module *module,
			struct helium_typed_module **out, char **error);

/* Convenience wrapper that discards the typed view. */
int helium_check_module(struct helium_module *module, char **error);

void helium_typed_module_free(struct helium_typed_module *typed);

#endif /* HELIUM_INFERENCE_H */
