/* SPDX-License-Identifier: TBD */
/*
 * lexer.l - Flex specification for the Helium lexer.
 */

%top{
#define YY_USER_ACTION \
	do { \
		struct helium_lexer_ctx *_ctx = yyextra; \
		int _i; \
		_ctx->token_line = _ctx->line; \
		_ctx->token_col = _ctx->col; \
		for (_i = 0; _i < yyleng; _i++) { \
			if (yytext[_i] == '\n') { \
				_ctx->line++; \
				_ctx->col = 1; \
			} else { \
				_ctx->col++; \
			} \
		} \
	} while (0);
}

%{
#include <stdio.h>
#include <string.h>

#include "token.h"
#include "lexer_internal.h"
%}

%option reentrant noyywrap noinput nounput
%option extra-type="struct helium_lexer_ctx *"

%x STRING FSTRING COMMENT

ident    [a-zA-Z_][a-zA-Z0-9_]*
digit    [0-9]

%%

[ \t\r]+        { /* ignored whitespace */ }
\n              { /* ignored newline; tracked by YY_USER_ACTION */ }
"//".*          { /* ignored line comment */ }
"/*"            { BEGIN(COMMENT); }
<COMMENT>"*/"   { BEGIN(INITIAL); }
<COMMENT>[^*\n]+ { /* ignored block comment text */ }
<COMMENT>"*"    { /* ignored star */ }
<COMMENT>\n     { /* ignored newline */ }
<COMMENT><<EOF>> {
	helium_lexer_set_error(yyextra, "unterminated block comment",
			       yyextra->line, yyextra->col);
	return 0;
}

"if"            { helium_lexer_push_token(yyextra, HELIUM_TOK_IF, NULL); }
"else"          { helium_lexer_push_token(yyextra, HELIUM_TOK_ELSE, NULL); }
"match"         { helium_lexer_push_token(yyextra, HELIUM_TOK_MATCH, NULL); }
"type"          { helium_lexer_push_token(yyextra, HELIUM_TOK_TYPE, NULL); }
"module"        { helium_lexer_push_token(yyextra, HELIUM_TOK_MODULE, NULL); }
"import"        { helium_lexer_push_token(yyextra, HELIUM_TOK_IMPORT, NULL); }
"loop"          { helium_lexer_push_token(yyextra, HELIUM_TOK_LOOP, NULL); }
"recur"         { helium_lexer_push_token(yyextra, HELIUM_TOK_RECUR, NULL); }
"fn"            { helium_lexer_push_token(yyextra, HELIUM_TOK_FN, NULL); }
"return"        { helium_lexer_push_token(yyextra, HELIUM_TOK_RETURN, NULL); }
"foreign"       { helium_lexer_push_token(yyextra, HELIUM_TOK_FOREIGN, NULL); }

"true"          { helium_lexer_push_token(yyextra, HELIUM_TOK_BOOL, yytext); }
"false"         { helium_lexer_push_token(yyextra, HELIUM_TOK_BOOL, yytext); }

{ident}         { helium_lexer_push_token(yyextra, HELIUM_TOK_IDENT, yytext); }

[-]?[0-9]+"."[0-9]+ { helium_lexer_push_token(yyextra, HELIUM_TOK_FLOAT, yytext); }
[-]?{digit}+             { helium_lexer_push_token(yyextra, HELIUM_TOK_INT, yytext); }

"\""            {
	BEGIN(STRING);
	helium_lexer_str_init(yyextra);
	helium_lexer_str_start(yyextra, yyextra->token_line, yyextra->token_col);
}
<STRING>"\""    { BEGIN(INITIAL); helium_lexer_str_finish(yyextra, HELIUM_TOK_STRING); }
<STRING>\\n     { helium_lexer_str_append_char(yyextra, '\n'); }
<STRING>\\\\    { helium_lexer_str_append_char(yyextra, '\\'); }
<STRING>\\\"    { helium_lexer_str_append_char(yyextra, '"'); }
<STRING>\\.     {
	helium_lexer_set_error(yyextra, "invalid escape sequence",
			       yyextra->token_line, yyextra->token_col);
	return 0;
}
<STRING>[^\\\n\"]+ { helium_lexer_str_append_text(yyextra, yytext, yyleng); }
<STRING>\n      {
	helium_lexer_set_error(yyextra, "unterminated string literal",
			       yyextra->token_line, yyextra->token_col);
	return 0;
}
<STRING><<EOF>> {
	helium_lexer_set_error(yyextra, "unterminated string literal",
			       yyextra->line, yyextra->col);
	return 0;
}

f"\""           { BEGIN(FSTRING); helium_lexer_fstring_start(yyextra);
                  helium_lexer_push_token(yyextra, HELIUM_TOK_FSTRING_START, NULL); }
<FSTRING>[^\\{\"\n]+ { helium_lexer_str_append_text(yyextra, yytext, yyleng); }
<FSTRING>\\n    { helium_lexer_str_append_char(yyextra, '\n'); }
<FSTRING>\\\\  { helium_lexer_str_append_char(yyextra, '\\'); }
<FSTRING>\\\"  { helium_lexer_str_append_char(yyextra, '"'); }
<FSTRING>\\.    {
	helium_lexer_set_error(yyextra, "invalid escape sequence in f-string",
			       yyextra->token_line, yyextra->token_col);
	return 0;
}
<FSTRING>"{"    {
	helium_lexer_str_flush(yyextra, HELIUM_TOK_FSTRING_PART);
	yyextra->fstr_depth = 1;
	helium_lexer_push_token(yyextra, HELIUM_TOK_LBRACE, NULL);
	BEGIN(INITIAL);
}
<FSTRING>"\""   {
	helium_lexer_str_flush(yyextra, HELIUM_TOK_FSTRING_PART);
	helium_lexer_push_token(yyextra, HELIUM_TOK_FSTRING_END, NULL);
	BEGIN(INITIAL);
}
<FSTRING>\n     {
	helium_lexer_set_error(yyextra, "unterminated f-string",
			       yyextra->token_line, yyextra->token_col);
	return 0;
}
<FSTRING><<EOF>> {
	helium_lexer_set_error(yyextra, "unterminated f-string",
			       yyextra->line, yyextra->col);
	return 0;
}

"{"             {
	if (yyextra->fstr_depth > 0)
		yyextra->fstr_depth++;
	helium_lexer_push_token(yyextra, HELIUM_TOK_LBRACE, NULL);
}
"}"             {
	if (yyextra->fstr_depth > 0) {
		yyextra->fstr_depth--;
		if (yyextra->fstr_depth == 0)
			BEGIN(FSTRING);
	}
	helium_lexer_push_token(yyextra, HELIUM_TOK_RBRACE, NULL);
}

"+"             { helium_lexer_push_token(yyextra, HELIUM_TOK_PLUS, NULL); }
"-"             { helium_lexer_push_token(yyextra, HELIUM_TOK_MINUS, NULL); }
"*"             { helium_lexer_push_token(yyextra, HELIUM_TOK_STAR, NULL); }
"/"             { helium_lexer_push_token(yyextra, HELIUM_TOK_SLASH, NULL); }
"%"             { helium_lexer_push_token(yyextra, HELIUM_TOK_PERCENT, NULL); }
"=="            { helium_lexer_push_token(yyextra, HELIUM_TOK_EQEQ, NULL); }
"!="            { helium_lexer_push_token(yyextra, HELIUM_TOK_NEQ, NULL); }
"<="            { helium_lexer_push_token(yyextra, HELIUM_TOK_LE, NULL); }
">="            { helium_lexer_push_token(yyextra, HELIUM_TOK_GE, NULL); }
"<"             { helium_lexer_push_token(yyextra, HELIUM_TOK_LT, NULL); }
">"             { helium_lexer_push_token(yyextra, HELIUM_TOK_GT, NULL); }
"&&"            { helium_lexer_push_token(yyextra, HELIUM_TOK_AND, NULL); }
"||"            { helium_lexer_push_token(yyextra, HELIUM_TOK_OR, NULL); }
"!"             { helium_lexer_push_token(yyextra, HELIUM_TOK_NOT, NULL); }
"="             { helium_lexer_push_token(yyextra, HELIUM_TOK_ASSIGN, NULL); }
":"             { helium_lexer_push_token(yyextra, HELIUM_TOK_COLON, NULL); }
";"             { helium_lexer_push_token(yyextra, HELIUM_TOK_SEMI, NULL); }
","             { helium_lexer_push_token(yyextra, HELIUM_TOK_COMMA, NULL); }
"."             { helium_lexer_push_token(yyextra, HELIUM_TOK_DOT, NULL); }
"->"            { helium_lexer_push_token(yyextra, HELIUM_TOK_ARROW, NULL); }
"=>"            { helium_lexer_push_token(yyextra, HELIUM_TOK_FATARROW, NULL); }
"|"             { helium_lexer_push_token(yyextra, HELIUM_TOK_PIPE, NULL); }
">>="           { helium_lexer_push_token(yyextra, HELIUM_TOK_BIND, NULL); }

"("             { helium_lexer_push_token(yyextra, HELIUM_TOK_LPAREN, NULL); }
")"             { helium_lexer_push_token(yyextra, HELIUM_TOK_RPAREN, NULL); }
"["             { helium_lexer_push_token(yyextra, HELIUM_TOK_LBRACKET, NULL); }
"]"             { helium_lexer_push_token(yyextra, HELIUM_TOK_RBRACKET, NULL); }

.               {
	helium_lexer_set_error(yyextra, "invalid character",
			       yyextra->token_line, yyextra->token_col);
	return 0;
}

<<EOF>>         { return 0; }

%%
