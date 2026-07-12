/* SPDX-License-Identifier: TBD */
/*
 * parser.y - Bison grammar for Helium.
 */

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"

/* Token-array state used by the generated yylex(). */
static struct helium_token *parse_tokens;
static size_t parse_count;
static size_t parse_pos;
static const char *parse_filename;
static char *parse_error;

static struct helium_module *parse_result;

static int yylex(void);
static void yyerror(const char *msg);
%}

%code requires {
/* Helper for building constructor pattern field lists. */
struct pf_pair {
	char *name;
	struct helium_pattern *pat;
};
}

%locations
%define parse.error verbose

%union {
	char *string;
	long integer;
	struct helium_module *module;
	struct helium_top_decl *decl;
	struct helium_type *type;
	struct helium_expr *expr;
	struct helium_pattern *pattern;
	struct helium_match_arm *arm;
	struct helium_binding *binding;
	struct helium_param *param;
	struct helium_loop_binding *loop_binding;
	struct helium_record_field *record_field;
	struct helium_variant *variant;
	struct helium_import *import;
	struct helium_type_def *type_def;
	struct helium_field_init *field_init;
	struct helium_fstring_part *fstring_part;
	struct pf_pair pf;
	char **ident_list;
	struct helium_record_field **record_fields;
	struct helium_variant **variants;
	struct helium_type **types;
	struct helium_expr **exprs;
	struct helium_match_arm **arms;
	struct helium_loop_binding **loop_bindings;
	struct helium_param **params;
	struct helium_field_init **field_inits;
}

%token <string> IDENT INT FLOAT
%token <string> BOOL STRING
%token <string> FSTRING_PART FSTRING_END
%token IF ELSE MATCH TYPE
%token MODULE IMPORT LOOP RECUR
%token FN RETURN FOREIGN
%token PLUS MINUS STAR SLASH
%token PERCENT
%token EQEQ NEQ LT LE
%token GT GE
%token AND OR NOT
%token ASSIGN COLON SEMI COMMA
%token DOT ARROW FATARROW PIPE
%token BIND
%token LPAREN RPAREN LBRACKET
%token RBRACKET LBRACE RBRACE
%token FSTRING_START

%type <module> module decl_list
%type <string> module_name import_path opt_semi
%type <decl> decl import_decl type_decl binding_decl foreign_decl
%type <type_def> type_def record_type adt_type
%type <type> type fn_type array_type primary_type type_app return_type
%type <expr> expr annot_expr bind_expr or_expr and_expr eq_expr rel_expr
%type <expr> add_expr mul_expr unary_expr primary_expr block block_items
%type <expr> if_expr match_expr loop_expr record_lit array_lit
%type <expr> fstring fstring_body return_expr literal
%type <pattern> pattern opt_pattern_fields pattern_fields
%type <pf> pattern_field
%type <arm> arm
%type <binding> binding
%type <param> param
%type <loop_binding> loop_binding
%type <record_field> record_field
%type <variant> variant
%type <field_init> field_init
%type <fstring_part> fstring_part
%type <ident_list> ident_list type_decl_params
%type <record_fields> record_field_list opt_record_field_list
%type <variants> variant_list
%type <types> type_list
%type <exprs> expr_list
%type <arms> arm_list
%type <loop_bindings> loop_binding_list
%type <params> param_list
%type <field_inits> field_init_list

%left BIND
%left OR
%left AND
%left EQEQ NEQ
%precedence TYPE_IDENT
%left LT LE GT GE
%precedence TYPE_APP
%left PLUS MINUS
%left STAR SLASH PERCENT
%precedence UPLUS UMINUS NOT
%precedence CALL FIELD
%precedence PAREN_GROUP
%precedence LAMBDA_BODY

%start module

%%

module:
    /* empty */
    {
	$$ = helium_module(NULL, 1, 1);
	parse_result = $$;
    }
|   MODULE module_name SEMI
    {
	$$ = helium_module($2, @1.first_line, @1.first_column);
	parse_result = $$;
    }
|   MODULE module_name SEMI decl_list
    {
	$$ = helium_module($2, @1.first_line, @1.first_column);
	if ($4) {
		size_t i;

		for (i = 0; i < $4->decl_count; i++)
			helium_module_add_decl($$, $4->decls[i]);
		free($4->decls);
		free($4);
	}
	parse_result = $$;
    }
|   decl_list
    {
	$$ = helium_module(NULL, 1, 1);
	if ($1) {
		size_t i;

		for (i = 0; i < $1->decl_count; i++)
			helium_module_add_decl($$, $1->decls[i]);
		free($1->decls);
		free($1);
	}
	parse_result = $$;
    }
;

module_name:
    IDENT
;

/* Lightweight list wrapper so actions can append declarations. */
decl_list:
    decl opt_semi
    {
	$$ = helium_module(NULL, 1, 1);
	helium_module_add_decl($$, $1);
    }
|   decl_list decl opt_semi
    {
	$$ = $1;
	helium_module_add_decl($$, $2);
    }
;

opt_semi:
    /* empty */
    {
	$$ = NULL;
    }
|   opt_semi SEMI
    {
	$$ = NULL;
    }
;


decl:
    import_decl
|   type_decl
|   binding_decl
|   foreign_decl
;

import_decl:
    IMPORT import_path opt_semi
    {
	$$ = helium_decl_import(helium_import($2, @1.first_line,
					      @1.first_column),
				@1.first_line, @1.first_column);
	free($2);
    }
;

import_path:
    IDENT
|   import_path DOT IDENT
    {
	char *buf;

	if (asprintf(&buf, "%s.%s", $1, $3) < 0)
		abort();
	free($1);
	free($3);
	$$ = buf;
    }
;

type_decl:
    TYPE IDENT type_decl_params ASSIGN type_def
    opt_semi
    {
	$$ = helium_decl_type($5);
	free($5->name);
	$5->name = $2;
	if ($3) {
		char **p = $3;

		while (*p)
			helium_type_def_add_param($5, *p++);
		free($3);
	}
    }
;

type_decl_params:
    /* empty */
    {
	$$ = NULL;
    }
|   LT ident_list GT
    {
	$$ = $2;
    }
;

ident_list:
    IDENT
    {
	char **arr = malloc(2 * sizeof(*arr));

	if (!arr)
		abort();
	arr[0] = $1;
	arr[1] = NULL;
	$$ = arr;
    }
|   ident_list COMMA IDENT
    {
	char **arr;
	int n = 0;

	while ($1[n])
		n++;
	arr = realloc($1, (n + 2) * sizeof(*arr));
	if (!arr)
		abort();
	arr[n] = $3;
	arr[n + 1] = NULL;
	$$ = arr;
    }
;

type_def:
    record_type
|   adt_type
;

record_type:
    LBRACE opt_record_field_list RBRACE
    {
	$$ = helium_type_def(NULL, HELIUM_TYPE_DEF_RECORD,
			     @1.first_line, @1.first_column);
	if ($2) {
		struct helium_record_field **f = $2;

		while (*f)
			helium_type_def_add_record_field($$, *f++);
		free($2);
	}
    }
;

opt_record_field_list:
    /* empty */
    {
	$$ = NULL;
    }
|   record_field_list
    {
	$$ = $1;
    }
;

record_field_list:
    record_field
    {
	struct helium_record_field **arr = malloc(2 * sizeof(*arr));

	if (!arr)
		abort();
	arr[0] = $1;
	arr[1] = NULL;
	$$ = arr;
    }
|   record_field_list COMMA record_field
    {
	struct helium_record_field **arr;
	int n = 0;

	while ($1[n])
		n++;
	arr = realloc($1, (n + 2) * sizeof(*arr));
	if (!arr)
		abort();
	arr[n] = $3;
	arr[n + 1] = NULL;
	$$ = arr;
    }
;

record_field:
    IDENT COLON type
    {
	$$ = helium_record_field($1, $3, @1.first_line, @1.first_column);
	free($1);
    }
;

adt_type:
    PIPE variant_list
    {
	$$ = helium_type_def(NULL, HELIUM_TYPE_DEF_ADT,
			     @1.first_line, @1.first_column);
	if ($2) {
		struct helium_variant **v = $2;

		while (*v)
			helium_type_def_add_variant($$, *v++);
		free($2);
	}
    }
|   variant_list
    {
	$$ = helium_type_def(NULL, HELIUM_TYPE_DEF_ADT,
			     @1.first_line, @1.first_column);
	if ($1) {
		struct helium_variant **v = $1;

		while (*v)
			helium_type_def_add_variant($$, *v++);
		free($1);
	}
    }
;

variant_list:
    variant
    {
	struct helium_variant **arr = malloc(2 * sizeof(*arr));

	if (!arr)
		abort();
	arr[0] = $1;
	arr[1] = NULL;
	$$ = arr;
    }
|   variant_list PIPE variant
    {
	struct helium_variant **arr;
	int n = 0;

	while ($1[n])
		n++;
	arr = realloc($1, (n + 2) * sizeof(*arr));
	if (!arr)
		abort();
	arr[n] = $3;
	arr[n + 1] = NULL;
	$$ = arr;
    }
;

variant:
    IDENT
    {
	$$ = helium_variant($1, @1.first_line, @1.first_column);
	free($1);
    }
|   IDENT LBRACE opt_record_field_list
    RBRACE
    {
	$$ = helium_variant($1, @1.first_line, @1.first_column);
	free($1);
	if ($3) {
		struct helium_record_field **f = $3;

		while (*f)
			helium_variant_add_field($$, *f++);
		free($3);
	}
    }
|   IDENT LT type_list GT
    {
	$$ = helium_variant($1, @1.first_line, @1.first_column);
	free($1);
	if ($3) {
		struct helium_type **t = $3;

		while (*t)
			helium_variant_add_type_arg($$, *t++);
		free($3);
	}
    }
;

type_list:
    type
    {
	struct helium_type **arr = malloc(2 * sizeof(*arr));

	if (!arr)
		abort();
	arr[0] = $1;
	arr[1] = NULL;
	$$ = arr;
    }
|   type_list COMMA type
    {
	struct helium_type **arr;
	int n = 0;

	while ($1[n])
		n++;
	arr = realloc($1, (n + 2) * sizeof(*arr));
	if (!arr)
		abort();
	arr[n] = $3;
	arr[n + 1] = NULL;
	$$ = arr;
    }
;

binding_decl:
    IDENT ASSIGN expr opt_semi
    {
	$$ = helium_decl_binding(helium_binding($1, NULL, $3,
						@1.first_line,
						@1.first_column));
	free($1);
    }
|   IDENT COLON type ASSIGN expr opt_semi
    {
	$$ = helium_decl_binding(helium_binding($1, $3, $5,
						@1.first_line,
						@1.first_column));
	free($1);
    }
;

foreign_decl:
    FOREIGN IDENT COLON type opt_semi
    {
	$$ = helium_decl_foreign($2, $4, @1.first_line, @1.first_column);
	free($2);
    }
;

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

type:
    fn_type
|   array_type
|   primary_type
;

fn_type:
    FN LPAREN RPAREN ARROW type
    {
	$$ = helium_type_fn(@1.first_line, @1.first_column);
	helium_type_set_ret($$, $5);
    }
|   FN LPAREN type_list RPAREN
    ARROW type
    {
	struct helium_type **t;

	$$ = helium_type_fn(@1.first_line, @1.first_column);
	if ($3) {
		t = $3;
		while (*t)
			helium_type_add_param($$, *t++);
		free($3);
	}
	helium_type_set_ret($$, $6);
    }
;

array_type:
    LBRACKET type SEMI INT
    RBRACKET
    {
	char *end;
	long size = strtol($4, &end, 10);

	(void)end;
	$$ = helium_type_array($2, size, @1.first_line, @1.first_column);
	free($4);
    }
;

primary_type:
    type_app
|   LPAREN type RPAREN
    {
	$$ = $2;
    }
|   LPAREN RPAREN %prec PAREN_GROUP
    {
	$$ = helium_type_named("()", @1.first_line, @1.first_column);
    }
;

type_app:
    IDENT %prec TYPE_IDENT
    {
	$$ = helium_type_named($1, @1.first_line, @1.first_column);
	free($1);
    }
|   IDENT LT type_list GT %prec TYPE_APP
    {
	struct helium_type **t;

	$$ = helium_type_named($1, @1.first_line, @1.first_column);
	free($1);
	if ($3) {
		t = $3;
		while (*t)
			helium_type_add_arg($$, *t++);
		free($3);
	}
    }
;

return_type:
    /* empty */
    {
	$$ = NULL;
    }
|   COLON type
    {
	$$ = $2;
    }
;

/* -------------------------------------------------------------------------- */
/* Expressions                                                                */
/* -------------------------------------------------------------------------- */

expr:
    annot_expr
;

annot_expr:
    bind_expr
|   annot_expr COLON type
    {
	$$ = helium_expr_annot($1, $3, @2.first_line, @2.first_column);
    }
;

bind_expr:
    or_expr
|   bind_expr BIND or_expr
    {
	$$ = helium_expr_bind($1, $3, @2.first_line, @2.first_column);
    }
;

or_expr:
    and_expr
|   or_expr OR and_expr
    {
	$$ = helium_expr_binary(OR, $1, $3,
				@2.first_line, @2.first_column);
    }
;

and_expr:
    eq_expr
|   and_expr AND eq_expr
    {
	$$ = helium_expr_binary(AND, $1, $3,
				@2.first_line, @2.first_column);
    }
;

eq_expr:
    rel_expr
|   eq_expr EQEQ rel_expr
    {
	$$ = helium_expr_binary(EQEQ, $1, $3,
				@2.first_line, @2.first_column);
    }
|   eq_expr NEQ rel_expr
    {
	$$ = helium_expr_binary(NEQ, $1, $3,
				@2.first_line, @2.first_column);
    }
;

rel_expr:
    add_expr
|   rel_expr LT add_expr
    {
	$$ = helium_expr_binary(LT, $1, $3,
				@2.first_line, @2.first_column);
    }
|   rel_expr LE add_expr
    {
	$$ = helium_expr_binary(LE, $1, $3,
				@2.first_line, @2.first_column);
    }
|   rel_expr GT add_expr
    {
	$$ = helium_expr_binary(GT, $1, $3,
				@2.first_line, @2.first_column);
    }
|   rel_expr GE add_expr
    {
	$$ = helium_expr_binary(GE, $1, $3,
				@2.first_line, @2.first_column);
    }
;

add_expr:
    mul_expr
|   add_expr PLUS mul_expr
    {
	$$ = helium_expr_binary(PLUS, $1, $3,
				@2.first_line, @2.first_column);
    }
|   add_expr MINUS mul_expr
    {
	$$ = helium_expr_binary(MINUS, $1, $3,
				@2.first_line, @2.first_column);
    }
;

mul_expr:
    unary_expr
|   mul_expr STAR unary_expr
    {
	$$ = helium_expr_binary(STAR, $1, $3,
				@2.first_line, @2.first_column);
    }
|   mul_expr SLASH unary_expr
    {
	$$ = helium_expr_binary(SLASH, $1, $3,
				@2.first_line, @2.first_column);
    }
|   mul_expr PERCENT unary_expr
    {
	$$ = helium_expr_binary(PERCENT, $1, $3,
				@2.first_line, @2.first_column);
    }
;

unary_expr:
    primary_expr
|   PLUS unary_expr %prec UPLUS
    {
	$$ = helium_expr_unary(PLUS, $2,
			       @1.first_line, @1.first_column);
    }
|   MINUS unary_expr %prec UMINUS
    {
	$$ = helium_expr_unary(MINUS, $2,
			       @1.first_line, @1.first_column);
    }
|   NOT unary_expr
    {
	$$ = helium_expr_unary(NOT, $2,
			       @1.first_line, @1.first_column);
    }
|   record_lit
;

primary_expr:
    literal
|   IDENT
    {
	$$ = helium_expr_ident($1, @1.first_line, @1.first_column);
	free($1);
    }
|   LPAREN RPAREN %prec PAREN_GROUP
    {
	struct helium_literal *lit = helium_literal(HELIUM_LIT_UNIT, "()",
					    @1.first_line,
					    @1.first_column);

	$$ = helium_expr_literal(lit, @1.first_line, @1.first_column);
    }
|   LPAREN bind_expr RPAREN %prec PAREN_GROUP
    {
	$$ = $2;
    }
|   LPAREN RPAREN return_type block %prec LAMBDA_BODY
    {
	$$ = helium_expr_lambda(@1.first_line, @1.first_column);
	$$->u.lambda.body = $4;
    }
|   LPAREN param_list RPAREN return_type block %prec LAMBDA_BODY
    {
	struct helium_param **p;

	$$ = helium_expr_lambda(@1.first_line, @1.first_column);
	if ($2) {
		p = $2;
		while (*p)
			helium_lambda_add_param($$, *p++);
		free($2);
	}
	$$->u.lambda.ret_type = $4;
	$$->u.lambda.body = $5;
    }
|   LT ident_list GT LPAREN param_list RPAREN return_type block %prec LAMBDA_BODY
    {
	char **tp;
	struct helium_param **p;

	$$ = helium_expr_lambda(@1.first_line, @1.first_column);
	tp = $2;
	while (*tp)
		helium_lambda_add_type_param($$, *tp++);
	free($2);
	if ($5) {
		p = $5;
		while (*p)
			helium_lambda_add_param($$, *p++);
		free($5);
	}
	$$->u.lambda.ret_type = $7;
	$$->u.lambda.body = $8;
    }
|   block
|   if_expr
|   match_expr
|   loop_expr
|   array_lit
|   fstring
|   return_expr
|   RECUR LPAREN RPAREN
    {
	$$ = helium_expr_recur(@1.first_line, @1.first_column);
    }
|   RECUR LPAREN expr_list RPAREN
    {
	struct helium_expr **e;

	$$ = helium_expr_recur(@1.first_line, @1.first_column);
	if ($3) {
		e = $3;
		while (*e)
			helium_recur_add_arg($$, *e++);
		free($3);
	}
    }
|   primary_expr LPAREN RPAREN %prec CALL
    {
	$$ = helium_expr_call($1, @1.first_line, @1.first_column);
    }
|   primary_expr LPAREN expr_list RPAREN %prec CALL
    {
	struct helium_expr **e;

	$$ = helium_expr_call($1, @1.first_line, @1.first_column);
	if ($3) {
		e = $3;
		while (*e)
			helium_call_add_arg($$, *e++);
		free($3);
	}
    }
|   primary_expr DOT IDENT %prec FIELD
    {
	$$ = helium_expr_field($1, $3, @2.first_line, @2.first_column);
	free($3);
    }
;

literal:
    INT
    {
	struct helium_literal *lit = helium_literal(HELIUM_LIT_INT, $1,
						    @1.first_line,
						    @1.first_column);

	$$ = helium_expr_literal(lit, @1.first_line, @1.first_column);
	free($1);
    }
|   FLOAT
    {
	struct helium_literal *lit = helium_literal(HELIUM_LIT_FLOAT, $1,
						    @1.first_line,
						    @1.first_column);

	$$ = helium_expr_literal(lit, @1.first_line, @1.first_column);
	free($1);
    }
|   BOOL
    {
	struct helium_literal *lit = helium_literal(HELIUM_LIT_BOOL, $1,
						    @1.first_line,
						    @1.first_column);

	$$ = helium_expr_literal(lit, @1.first_line, @1.first_column);
	free($1);
    }
|   STRING
    {
	struct helium_literal *lit = helium_literal(HELIUM_LIT_STRING, $1,
						    @1.first_line,
						    @1.first_column);

	$$ = helium_expr_literal(lit, @1.first_line, @1.first_column);
	free($1);
    }
;

expr_list:
    expr
    {
	struct helium_expr **arr = malloc(2 * sizeof(*arr));

	if (!arr)
		abort();
	arr[0] = $1;
	arr[1] = NULL;
	$$ = arr;
    }
|   expr_list COMMA expr
    {
	struct helium_expr **arr;
	int n = 0;

	while ($1[n])
		n++;
	arr = realloc($1, (n + 2) * sizeof(*arr));
	if (!arr)
		abort();
	arr[n] = $3;
	arr[n + 1] = NULL;
	$$ = arr;
    }
;

block:
    LBRACE RBRACE
    {
	$$ = helium_expr_block(@1.first_line, @1.first_column);
    }
|   LBRACE block_items RBRACE
    {
	$$ = $2;
    }
;

block_items:
    binding
    {
	$$ = helium_expr_block(@1.first_line, @1.first_column);
	helium_block_add_binding($$, $1);
    }
|   expr
    {
	$$ = helium_expr_block(@1.first_line, @1.first_column);
	helium_block_add_expr($$, $1);
    }
|   block_items SEMI binding
    {
	$$ = $1;
	helium_block_add_binding($$, $3);
    }
|   block_items SEMI expr
    {
	$$ = $1;
	helium_block_add_expr($$, $3);
    }
|   block_items SEMI
    {
	$$ = $1;
    }
;

binding:
    IDENT ASSIGN expr
    {
	$$ = helium_binding($1, NULL, $3, @1.first_line, @1.first_column);
	free($1);
    }
|   IDENT COLON type ASSIGN expr
    {
	$$ = helium_binding($1, $3, $5, @1.first_line, @1.first_column);
	free($1);
    }
;

if_expr:
    IF LPAREN expr RPAREN block
    ELSE block
    {
	$$ = helium_expr_if($3, $5, $7, @1.first_line, @1.first_column);
    }
|   IF LPAREN expr RPAREN block
    ELSE if_expr
    {
	$$ = helium_expr_if($3, $5, $7, @1.first_line, @1.first_column);
    }
;

match_expr:
    MATCH primary_expr LBRACE RBRACE
    {
	$$ = helium_expr_match($2, @1.first_line, @1.first_column);
    }
|   MATCH primary_expr LBRACE arm_list RBRACE
    {
	$$ = helium_expr_match($2, @1.first_line, @1.first_column);
	if ($4) {
		struct helium_match_arm **a = $4;

		while (*a)
			helium_match_add_arm($$, *a++);
		free($4);
	}
    }
;

arm_list:
    arm
    {
	struct helium_match_arm **arr = malloc(2 * sizeof(*arr));

	if (!arr)
		abort();
	arr[0] = $1;
	arr[1] = NULL;
	$$ = arr;
    }
|   arm_list COMMA arm
    {
	struct helium_match_arm **arr;
	int n = 0;

	while ($1[n])
		n++;
	arr = realloc($1, (n + 2) * sizeof(*arr));
	if (!arr)
		abort();
	arr[n] = $3;
	arr[n + 1] = NULL;
	$$ = arr;
    }
|   arm_list COMMA
    {
	$$ = $1;
    }
;

arm:
    pattern FATARROW expr
    {
	$$ = helium_match_arm($1, $3, @2.first_line, @2.first_column);
    }
;

pattern:
    IDENT
    {
	if (strcmp($1, "_") == 0) {
		$$ = helium_pattern_wild(@1.first_line, @1.first_column);
		free($1);
	} else if ($1[0] >= 'A' && $1[0] <= 'Z') {
		$$ = helium_pattern_constructor($1, @1.first_line,
						@1.first_column);
		free($1);
	} else {
		$$ = helium_pattern_ident($1, @1.first_line, @1.first_column);
		free($1);
	}
    }
|   INT
    {
	struct helium_literal *lit = helium_literal(HELIUM_LIT_INT, $1,
						    @1.first_line,
						    @1.first_column);

	$$ = helium_pattern_literal(lit, @1.first_line, @1.first_column);
	free($1);
    }
|   FLOAT
    {
	struct helium_literal *lit = helium_literal(HELIUM_LIT_FLOAT, $1,
						    @1.first_line,
						    @1.first_column);

	$$ = helium_pattern_literal(lit, @1.first_line, @1.first_column);
	free($1);
    }
|   BOOL
    {
	struct helium_literal *lit = helium_literal(HELIUM_LIT_BOOL, $1,
						    @1.first_line,
						    @1.first_column);

	$$ = helium_pattern_literal(lit, @1.first_line, @1.first_column);
	free($1);
    }
|   STRING
    {
	struct helium_literal *lit = helium_literal(HELIUM_LIT_STRING, $1,
						    @1.first_line,
						    @1.first_column);

	$$ = helium_pattern_literal(lit, @1.first_line, @1.first_column);
	free($1);
    }
|   IDENT LBRACE opt_pattern_fields
    RBRACE
    {
	if ($3) {
		$$ = $3;
		free($$->name);
		$$->name = $1;
	} else {
		$$ = helium_pattern_constructor($1, @1.first_line,
						@1.first_column);
		free($1);
	}
    }
;

opt_pattern_fields:
    /* empty */
    {
	$$ = NULL;
    }
|   pattern_fields
    {
	$$ = $1;
    }
;

pattern_fields:
    pattern_field
    {
	$$ = helium_pattern_constructor(NULL, @1.first_line,
					@1.first_column);
	helium_pattern_add_field($$, $1.name, $1.pat);
    }
|   pattern_fields COMMA pattern_field
    {
	$$ = $1;
	helium_pattern_add_field($$, $3.name, $3.pat);
    }
;

pattern_field:
    IDENT COLON pattern
    {
	$$.name = $1;
	$$.pat = $3;
    }
;

loop_expr:
    LOOP LPAREN RPAREN block
    {
	$$ = helium_expr_loop(@1.first_line, @1.first_column);
	$$->u.loop.body = $4;
    }
|   LOOP LPAREN loop_binding_list RPAREN block
    {
	$$ = helium_expr_loop(@1.first_line, @1.first_column);
	if ($3) {
		struct helium_loop_binding **b = $3;

		while (*b)
			helium_loop_add_binding($$, *b++);
		free($3);
	}
	$$->u.loop.body = $5;
    }
;

loop_binding_list:
    loop_binding
    {
	struct helium_loop_binding **arr = malloc(2 * sizeof(*arr));

	if (!arr)
		abort();
	arr[0] = $1;
	arr[1] = NULL;
	$$ = arr;
    }
|   loop_binding_list COMMA loop_binding
    {
	struct helium_loop_binding **arr;
	int n = 0;

	while ($1[n])
		n++;
	arr = realloc($1, (n + 2) * sizeof(*arr));
	if (!arr)
		abort();
	arr[n] = $3;
	arr[n + 1] = NULL;
	$$ = arr;
    }
;

loop_binding:
    IDENT ASSIGN expr
    {
	$$ = helium_loop_binding($1, NULL, $3,
				 @1.first_line, @1.first_column);
	free($1);
    }
|   IDENT COLON type ASSIGN expr
    {
	$$ = helium_loop_binding($1, $3, $5,
				 @1.first_line, @1.first_column);
	free($1);
    }
;


param_list:
    param
    {
	struct helium_param **arr = malloc(2 * sizeof(*arr));

	if (!arr)
		abort();
	arr[0] = $1;
	arr[1] = NULL;
	$$ = arr;
    }
|   param_list COMMA param
    {
	struct helium_param **arr;
	int n = 0;

	while ($1[n])
		n++;
	arr = realloc($1, (n + 2) * sizeof(*arr));
	if (!arr)
		abort();
	arr[n] = $3;
	arr[n + 1] = NULL;
	$$ = arr;
    }
;

param:
    IDENT COLON type
    {
	$$ = helium_param($1, $3, @1.first_line, @1.first_column);
	free($1);
    }
|   IDENT
    {
	$$ = helium_param($1, NULL, @1.first_line, @1.first_column);
	free($1);
    }
;

record_lit:
    IDENT LBRACE RBRACE
    {
	$$ = helium_expr_record_lit($1, @1.first_line, @1.first_column);
	free($1);
    }
|   IDENT LBRACE field_init_list RBRACE
    {
	$$ = helium_expr_record_lit($1, @1.first_line, @1.first_column);
	free($1);
	if ($3) {
		struct helium_field_init **f = $3;

		while (*f)
			helium_record_lit_add_field($$, *f++);
		free($3);
	}
    }
;

field_init_list:
    field_init
    {
	struct helium_field_init **arr = malloc(2 * sizeof(*arr));

	if (!arr)
		abort();
	arr[0] = $1;
	arr[1] = NULL;
	$$ = arr;
    }
|   field_init_list COMMA field_init
    {
	struct helium_field_init **arr;
	int n = 0;

	while ($1[n])
		n++;
	arr = realloc($1, (n + 2) * sizeof(*arr));
	if (!arr)
		abort();
	arr[n] = $3;
	arr[n + 1] = NULL;
	$$ = arr;
    }
;

field_init:
    IDENT COLON expr
    {
	$$ = helium_field_init($1, $3, @1.first_line, @1.first_column);
	free($1);
    }
;

array_lit:
    LBRACKET RBRACKET
    {
	$$ = helium_expr_array_lit(@1.first_line, @1.first_column);
    }
|   LBRACKET expr_list RBRACKET
    {
	struct helium_expr **e;

	$$ = helium_expr_array_lit(@1.first_line, @1.first_column);
	if ($2) {
		e = $2;
		while (*e)
			helium_array_lit_add_item($$, *e++);
		free($2);
	}
    }
;

fstring:
    FSTRING_START FSTRING_END
    {
	$$ = helium_expr_fstring(@1.first_line, @1.first_column);
    }
|   FSTRING_START fstring_body FSTRING_END
    {
	$$ = $2;
    }
;

fstring_body:
    fstring_part
    {
	$$ = helium_expr_fstring(@1.first_line, @1.first_column);
	if ($1->is_expr)
		helium_fstring_add_expr($$, $1->u.expr);
	else
		helium_fstring_add_text($$, $1->u.text,
					$1->line, $1->col);
	free($1);
    }
|   fstring_body fstring_part
    {
	$$ = $1;
	if ($2->is_expr)
		helium_fstring_add_expr($$, $2->u.expr);
	else
		helium_fstring_add_text($$, $2->u.text,
					$2->line, $2->col);
	free($2);
    }
;

fstring_part:
    FSTRING_PART
    {
	$$ = helium_fstring_part_text($1, @1.first_line, @1.first_column);
	free($1);
    }
|   LBRACE expr RBRACE
    {
	$$ = helium_fstring_part_expr($2);
    }
;

return_expr:
    RETURN expr
    {
	$$ = helium_expr_return($2, @1.first_line, @1.first_column);
    }
;

%%

#include "token.h"

/* Lexical interface: pull tokens from the pre-computed token array. */
/* Map lexer token codes to bison token codes. */
static int map_token(int type)
{
	switch (type) {
	case HELIUM_TOK_EOF:
		return 0;
	case HELIUM_TOK_IF:
		return IF;
	case HELIUM_TOK_ELSE:
		return ELSE;
	case HELIUM_TOK_MATCH:
		return MATCH;
	case HELIUM_TOK_TYPE:
		return TYPE;
	case HELIUM_TOK_MODULE:
		return MODULE;
	case HELIUM_TOK_IMPORT:
		return IMPORT;
	case HELIUM_TOK_LOOP:
		return LOOP;
	case HELIUM_TOK_RECUR:
		return RECUR;
	case HELIUM_TOK_FN:
		return FN;
	case HELIUM_TOK_RETURN:
		return RETURN;
	case HELIUM_TOK_FOREIGN:
		return FOREIGN;
	case HELIUM_TOK_IDENT:
		return IDENT;
	case HELIUM_TOK_INT:
		return INT;
	case HELIUM_TOK_FLOAT:
		return FLOAT;
	case HELIUM_TOK_BOOL:
		return BOOL;
	case HELIUM_TOK_STRING:
		return STRING;
	case HELIUM_TOK_FSTRING_START:
		return FSTRING_START;
	case HELIUM_TOK_FSTRING_PART:
		return FSTRING_PART;
	case HELIUM_TOK_FSTRING_END:
		return FSTRING_END;
	case HELIUM_TOK_PLUS:
		return PLUS;
	case HELIUM_TOK_MINUS:
		return MINUS;
	case HELIUM_TOK_STAR:
		return STAR;
	case HELIUM_TOK_SLASH:
		return SLASH;
	case HELIUM_TOK_PERCENT:
		return PERCENT;
	case HELIUM_TOK_EQEQ:
		return EQEQ;
	case HELIUM_TOK_NEQ:
		return NEQ;
	case HELIUM_TOK_LT:
		return LT;
	case HELIUM_TOK_LE:
		return LE;
	case HELIUM_TOK_GT:
		return GT;
	case HELIUM_TOK_GE:
		return GE;
	case HELIUM_TOK_AND:
		return AND;
	case HELIUM_TOK_OR:
		return OR;
	case HELIUM_TOK_NOT:
		return NOT;
	case HELIUM_TOK_ASSIGN:
		return ASSIGN;
	case HELIUM_TOK_COLON:
		return COLON;
	case HELIUM_TOK_SEMI:
		return SEMI;
	case HELIUM_TOK_COMMA:
		return COMMA;
	case HELIUM_TOK_DOT:
		return DOT;
	case HELIUM_TOK_ARROW:
		return ARROW;
	case HELIUM_TOK_FATARROW:
		return FATARROW;
	case HELIUM_TOK_PIPE:
		return PIPE;
	case HELIUM_TOK_BIND:
		return BIND;
	case HELIUM_TOK_LPAREN:
		return LPAREN;
	case HELIUM_TOK_RPAREN:
		return RPAREN;
	case HELIUM_TOK_LBRACKET:
		return LBRACKET;
	case HELIUM_TOK_RBRACKET:
		return RBRACKET;
	case HELIUM_TOK_LBRACE:
		return LBRACE;
	case HELIUM_TOK_RBRACE:
		return RBRACE;
	default:
		return 0;
	}
}

int yylex(void)
{
	struct helium_token *tok;
	int type;

	if (parse_pos >= parse_count)
		return 0;

	tok = &parse_tokens[parse_pos++];
	yylloc.first_line = tok->line;
	yylloc.first_column = tok->col;
	yylloc.last_line = tok->line;
	yylloc.last_column = tok->col + (tok->text ? (int)strlen(tok->text) : 1) - 1;

	type = map_token(tok->type);
	switch (type) {
	case IDENT:
	case INT:
	case FLOAT:
	case BOOL:
	case STRING:
	case FSTRING_PART:
	case FSTRING_END:
		yylval.string = tok->text ? strdup(tok->text) : NULL;
		break;
	default:
		break;
	}

	return type;
}

void yyerror(const char *msg)
{
	if (parse_error)
		return;

	if (asprintf(&parse_error, "%s:%d:%d: %s",
		     parse_filename ? parse_filename : "<stdin>",
		     yylloc.first_line, yylloc.first_column, msg) < 0)
		parse_error = strdup(msg);
}

struct helium_module *helium_parse_tokens(struct helium_token *tokens,
					  size_t count,
					  const char *filename,
					  char **error)
{
	parse_tokens = tokens;
	parse_count = count;
	parse_pos = 0;
	parse_filename = filename;
	parse_error = NULL;
	parse_result = NULL;

	if (yyparse() != 0) {
		if (error)
			*error = parse_error ? parse_error :
				 strdup("parse error");
		parse_error = NULL;
		helium_module_free(parse_result);
		return NULL;
	}

	if (error)
		*error = NULL;
	return parse_result;
}
