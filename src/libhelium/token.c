/* SPDX-License-Identifier: TBD */
/*
 * token.c - Token helpers and the public lexer entry point.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "token.h"
#include "lexer_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations for the generated reentrant flex scanner. */
typedef void *yyscan_t;

int yylex_init_extra(struct helium_lexer_ctx *user_defined, yyscan_t *scanner);
int yylex(yyscan_t scanner);
void yylex_destroy(yyscan_t scanner);
void yyset_in(FILE *in_str, yyscan_t scanner);

const char *helium_token_name(int type)
{
	switch (type) {
	case HELIUM_TOK_EOF:
		return "EOF";
	case HELIUM_TOK_IF:
		return "IF";
	case HELIUM_TOK_ELSE:
		return "ELSE";
	case HELIUM_TOK_MATCH:
		return "MATCH";
	case HELIUM_TOK_TYPE:
		return "TYPE";
	case HELIUM_TOK_MODULE:
		return "MODULE";
	case HELIUM_TOK_IMPORT:
		return "IMPORT";
	case HELIUM_TOK_LOOP:
		return "LOOP";
	case HELIUM_TOK_RECUR:
		return "RECUR";
	case HELIUM_TOK_FN:
		return "FN";
	case HELIUM_TOK_RETURN:
		return "RETURN";
	case HELIUM_TOK_FOREIGN:
		return "FOREIGN";
	case HELIUM_TOK_IDENT:
		return "IDENT";
	case HELIUM_TOK_INT:
		return "INT";
	case HELIUM_TOK_FLOAT:
		return "FLOAT";
	case HELIUM_TOK_BOOL:
		return "BOOL";
	case HELIUM_TOK_STRING:
		return "STRING";
	case HELIUM_TOK_FSTRING_START:
		return "FSTRING_START";
	case HELIUM_TOK_FSTRING_PART:
		return "FSTRING_PART";
	case HELIUM_TOK_FSTRING_END:
		return "FSTRING_END";
	case HELIUM_TOK_PLUS:
		return "PLUS";
	case HELIUM_TOK_MINUS:
		return "MINUS";
	case HELIUM_TOK_STAR:
		return "STAR";
	case HELIUM_TOK_SLASH:
		return "SLASH";
	case HELIUM_TOK_PERCENT:
		return "PERCENT";
	case HELIUM_TOK_EQEQ:
		return "EQEQ";
	case HELIUM_TOK_NEQ:
		return "NEQ";
	case HELIUM_TOK_LT:
		return "LT";
	case HELIUM_TOK_LE:
		return "LE";
	case HELIUM_TOK_GT:
		return "GT";
	case HELIUM_TOK_GE:
		return "GE";
	case HELIUM_TOK_AND:
		return "AND";
	case HELIUM_TOK_OR:
		return "OR";
	case HELIUM_TOK_NOT:
		return "NOT";
	case HELIUM_TOK_ASSIGN:
		return "ASSIGN";
	case HELIUM_TOK_COLON:
		return "COLON";
	case HELIUM_TOK_SEMI:
		return "SEMI";
	case HELIUM_TOK_COMMA:
		return "COMMA";
	case HELIUM_TOK_DOT:
		return "DOT";
	case HELIUM_TOK_ARROW:
		return "ARROW";
	case HELIUM_TOK_FATARROW:
		return "FATARROW";
	case HELIUM_TOK_PIPE:
		return "PIPE";
	case HELIUM_TOK_BIND:
		return "BIND";
	case HELIUM_TOK_LPAREN:
		return "LPAREN";
	case HELIUM_TOK_RPAREN:
		return "RPAREN";
	case HELIUM_TOK_LBRACKET:
		return "LBRACKET";
	case HELIUM_TOK_RBRACKET:
		return "RBRACKET";
	case HELIUM_TOK_LBRACE:
		return "LBRACE";
	case HELIUM_TOK_RBRACE:
		return "RBRACE";
	default:
		return "UNKNOWN";
	}
}

void helium_tokens_free(struct helium_token *tokens, size_t count)
{
	size_t i;

	if (!tokens)
		return;

	for (i = 0; i < count; i++)
		free(tokens[i].text);
	free(tokens);
}

static void push_token_at(struct helium_lexer_ctx *ctx, int type,
			  const char *text, int line, int col)
{
	struct helium_token *tmp;

	if (ctx->error)
		return;

	if (ctx->count == ctx->capacity) {
		size_t newcap = ctx->capacity ? ctx->capacity * 2 : 64;

		tmp = realloc(ctx->tokens, newcap * sizeof(*tmp));
		if (!tmp) {
			helium_lexer_set_error(ctx, "out of memory",
					       ctx->token_line, ctx->token_col);
			return;
		}
		ctx->tokens = tmp;
		ctx->capacity = newcap;
	}

	ctx->tokens[ctx->count].type = type;
	ctx->tokens[ctx->count].text = text ? strdup(text) : NULL;
	ctx->tokens[ctx->count].line = line;
	ctx->tokens[ctx->count].col = col;
	ctx->count++;
}

void helium_lexer_push_token(struct helium_lexer_ctx *ctx, int type,
			     const char *text)
{
	push_token_at(ctx, type, text, ctx->token_line, ctx->token_col);
}

void helium_lexer_set_error(struct helium_lexer_ctx *ctx, const char *msg,
			    int line, int col)
{
	char *buf = NULL;

	if (ctx->error)
		return;

	if (asprintf(&buf, "%s at line %d, column %d", msg, line, col) < 0) {
		ctx->error = strdup(msg);
		return;
	}
	ctx->error = buf;
}

void helium_lexer_str_init(struct helium_lexer_ctx *ctx)
{
	free(ctx->str_buf);
	ctx->str_buf = NULL;
	ctx->str_len = 0;
	ctx->str_cap = 0;
	ctx->str_line = 0;
	ctx->str_col = 0;
}

void helium_lexer_str_start(struct helium_lexer_ctx *ctx, int line, int col)
{
	ctx->str_line = line;
	ctx->str_col = col;
}

static int str_ensure(struct helium_lexer_ctx *ctx, size_t need)
{
	size_t newcap;
	char *tmp;

	if (need <= ctx->str_cap)
		return 0;

	newcap = ctx->str_cap ? ctx->str_cap * 2 : 16;
	while (newcap < need)
		newcap *= 2;

	tmp = realloc(ctx->str_buf, newcap);
	if (!tmp) {
		helium_lexer_set_error(ctx, "out of memory",
				       ctx->token_line, ctx->token_col);
		return -1;
	}
	ctx->str_buf = tmp;
	ctx->str_cap = newcap;
	return 0;
}

void helium_lexer_str_append_char(struct helium_lexer_ctx *ctx, char c)
{
	if (ctx->str_len == 0) {
		ctx->str_line = ctx->token_line;
		ctx->str_col = ctx->token_col;
	}
	if (str_ensure(ctx, ctx->str_len + 2))
		return;
	ctx->str_buf[ctx->str_len++] = c;
	ctx->str_buf[ctx->str_len] = '\0';
}

void helium_lexer_str_append_text(struct helium_lexer_ctx *ctx,
				  const char *text, size_t len)
{
	if (ctx->str_len == 0) {
		ctx->str_line = ctx->token_line;
		ctx->str_col = ctx->token_col;
	}
	if (str_ensure(ctx, ctx->str_len + len + 1))
		return;
	memcpy(ctx->str_buf + ctx->str_len, text, len);
	ctx->str_len += len;
	ctx->str_buf[ctx->str_len] = '\0';
}

void helium_lexer_str_flush(struct helium_lexer_ctx *ctx, int type)
{
	if (ctx->str_len > 0)
		push_token_at(ctx, type, ctx->str_buf, ctx->str_line, ctx->str_col);
	helium_lexer_str_init(ctx);
}

void helium_lexer_str_finish(struct helium_lexer_ctx *ctx, int type)
{
	push_token_at(ctx, type, ctx->str_len ? ctx->str_buf : "",
		      ctx->str_line, ctx->str_col);
	helium_lexer_str_init(ctx);
}

void helium_lexer_fstring_start(struct helium_lexer_ctx *ctx)
{
	ctx->fstr_depth = 0;
	helium_lexer_str_init(ctx);
}

static void ctx_init(struct helium_lexer_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->line = 1;
	ctx->col = 1;
}

static void ctx_cleanup(struct helium_lexer_ctx *ctx)
{
	helium_tokens_free(ctx->tokens, ctx->count);
	ctx->tokens = NULL;
	ctx->count = 0;
	ctx->capacity = 0;
	free(ctx->error);
	ctx->error = NULL;
	free(ctx->str_buf);
	ctx->str_buf = NULL;
	ctx->str_len = 0;
	ctx->str_cap = 0;
}

int helium_lex_file(FILE *file, struct helium_token **tokens, size_t *count,
		    char **error)
{
	yyscan_t scanner;
	struct helium_lexer_ctx ctx;
	int ret;

	if (!file || !tokens || !count || !error)
		return -1;

	ctx_init(&ctx);

	ret = yylex_init_extra(&ctx, &scanner);
	if (ret) {
		helium_lexer_set_error(&ctx, "failed to initialize lexer", 1, 1);
		goto fail;
	}

	yyset_in(file, scanner);
	yylex(scanner);
	yylex_destroy(scanner);

	if (ctx.error) {
		*tokens = NULL;
		*count = 0;
		*error = ctx.error;
		ctx.error = NULL;
		ctx_cleanup(&ctx);
		return -1;
	}

	*tokens = ctx.tokens;
	*count = ctx.count;
	*error = NULL;
	ctx.tokens = NULL;
	ctx_cleanup(&ctx);
	return 0;

fail:
	*tokens = NULL;
	*count = 0;
	*error = ctx.error ? ctx.error : strdup("lexer error");
	ctx.error = NULL;
	ctx_cleanup(&ctx);
	return -1;
}
