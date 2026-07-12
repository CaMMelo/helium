/* SPDX-License-Identifier: TBD */
/*
 * inference.h - Type inference engine for Helium.
 */

#ifndef HELIUM_INFERENCE_H
#define HELIUM_INFERENCE_H

#include "ast.h"

int helium_check_module(struct helium_module *module, char **error);

#endif /* HELIUM_INFERENCE_H */
