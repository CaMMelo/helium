/* SPDX-License-Identifier: TBD */
/*
 * lexer_internal.h - Helpers shared between the flex specification and
 * token.c.  Not part of the public lexer API.
 */

#ifndef HELIUM_LEXER_INTERNAL_H
#define HELIUM_LEXER_INTERNAL_H

#include "token.h"

void helium_lexer_push_token(struct helium_lexer_ctx *ctx, int type,
			     const char *text);
void helium_lexer_set_error(struct helium_lexer_ctx *ctx, const char *msg,
			    int line, int col);

void helium_lexer_str_init(struct helium_lexer_ctx *ctx);
void helium_lexer_str_start(struct helium_lexer_ctx *ctx, int line, int col);
void helium_lexer_str_append_char(struct helium_lexer_ctx *ctx, char c);
void helium_lexer_str_append_text(struct helium_lexer_ctx *ctx,
				  const char *text, size_t len);
void helium_lexer_str_flush(struct helium_lexer_ctx *ctx, int type);
void helium_lexer_str_finish(struct helium_lexer_ctx *ctx, int type);

void helium_lexer_fstring_start(struct helium_lexer_ctx *ctx);

#endif /* HELIUM_LEXER_INTERNAL_H */
