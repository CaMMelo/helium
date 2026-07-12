/* SPDX-License-Identifier: TBD */
/*
 * parser.h - Public parser interface for Helium.
 */

#ifndef HELIUM_PARSER_H
#define HELIUM_PARSER_H

#include "ast.h"
#include "token.h"

struct helium_module *helium_parse_tokens(struct helium_token *tokens,
					  size_t count,
					  const char *filename,
					  char **error);

#endif /* HELIUM_PARSER_H */
