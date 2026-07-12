/* SPDX-License-Identifier: TBD */
/*
 * token.h - Helium token definitions and public lexer interface.
 */

#ifndef HELIUM_TOKEN_H
#define HELIUM_TOKEN_H

#include <stddef.h>
#include <stdio.h>

enum helium_token_type {
	HELIUM_TOK_EOF = 0,
	HELIUM_TOK_IF,
	HELIUM_TOK_ELSE,
	HELIUM_TOK_MATCH,
	HELIUM_TOK_TYPE,
	HELIUM_TOK_MODULE,
	HELIUM_TOK_IMPORT,
	HELIUM_TOK_LOOP,
	HELIUM_TOK_RECUR,
	HELIUM_TOK_FN,
	HELIUM_TOK_RETURN,
	HELIUM_TOK_FOREIGN,

	HELIUM_TOK_IDENT,
	HELIUM_TOK_INT,
	HELIUM_TOK_FLOAT,
	HELIUM_TOK_BOOL,
	HELIUM_TOK_STRING,

	HELIUM_TOK_FSTRING_START,
	HELIUM_TOK_FSTRING_PART,
	HELIUM_TOK_FSTRING_END,

	HELIUM_TOK_PLUS,
	HELIUM_TOK_MINUS,
	HELIUM_TOK_STAR,
	HELIUM_TOK_SLASH,
	HELIUM_TOK_PERCENT,
	HELIUM_TOK_EQEQ,
	HELIUM_TOK_NEQ,
	HELIUM_TOK_LT,
	HELIUM_TOK_LE,
	HELIUM_TOK_GT,
	HELIUM_TOK_GE,
	HELIUM_TOK_AND,
	HELIUM_TOK_OR,
	HELIUM_TOK_NOT,
	HELIUM_TOK_ASSIGN,
	HELIUM_TOK_COLON,
	HELIUM_TOK_SEMI,
	HELIUM_TOK_COMMA,
	HELIUM_TOK_DOT,
	HELIUM_TOK_ARROW,
	HELIUM_TOK_FATARROW,
	HELIUM_TOK_PIPE,
	HELIUM_TOK_BIND,

	HELIUM_TOK_LPAREN,
	HELIUM_TOK_RPAREN,
	HELIUM_TOK_LBRACKET,
	HELIUM_TOK_RBRACKET,
	HELIUM_TOK_LBRACE,
	HELIUM_TOK_RBRACE,
};

struct helium_token {
	int type;
	char *text;
	int line;
	int col;
};

struct helium_lexer_ctx {
	struct helium_token *tokens;
	size_t count;
	size_t capacity;
	char *error;
	int line;
	int col;
	int token_line;
	int token_col;
	int fstr_depth;
	int str_line;
	int str_col;
	char *str_buf;
	size_t str_len;
	size_t str_cap;
};

const char *helium_token_name(int type);
void helium_tokens_free(struct helium_token *tokens, size_t count);
int helium_lex_file(FILE *file, struct helium_token **tokens, size_t *count,
		    char **error);

#endif /* HELIUM_TOKEN_H */
