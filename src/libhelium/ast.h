/* SPDX-License-Identifier: TBD */
/*
 * ast.h - Abstract syntax tree definitions for Helium.
 */

#ifndef HELIUM_AST_H
#define HELIUM_AST_H

#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

enum helium_type_kind {
	HELIUM_TYPE_NAMED,
	HELIUM_TYPE_FN,
	HELIUM_TYPE_ARRAY,
	HELIUM_TYPE_VAR,
};

struct helium_type {
	enum helium_type_kind kind;
	int line;
	int col;
	char *name;
	struct helium_type **args;
	size_t arg_count;
	size_t arg_capacity;
	struct helium_type **params;
	size_t param_count;
	size_t param_capacity;
	struct helium_type *ret;
	struct helium_type *elem_type;
	long array_size;
};

struct helium_type *helium_type_named(const char *name, int line, int col);
struct helium_type *helium_type_var(const char *name, int line, int col);
struct helium_type *helium_type_fn(int line, int col);
struct helium_type *helium_type_array(struct helium_type *elem, long size,
				      int line, int col);
void helium_type_add_arg(struct helium_type *type, struct helium_type *arg);
void helium_type_add_param(struct helium_type *type, struct helium_type *param);
void helium_type_set_ret(struct helium_type *type, struct helium_type *ret);
void helium_type_free(struct helium_type *type);

/* -------------------------------------------------------------------------- */
/* Expressions                                                                */
/* -------------------------------------------------------------------------- */

enum helium_literal_kind {
	HELIUM_LIT_INT,
	HELIUM_LIT_FLOAT,
	HELIUM_LIT_BOOL,
	HELIUM_LIT_STRING,
	HELIUM_LIT_UNIT,
};

enum helium_expr_kind {
	HELIUM_EXPR_LITERAL,
	HELIUM_EXPR_IDENT,
	HELIUM_EXPR_BINARY,
	HELIUM_EXPR_UNARY,
	HELIUM_EXPR_CALL,
	HELIUM_EXPR_BLOCK,
	HELIUM_EXPR_IF,
	HELIUM_EXPR_MATCH,
	HELIUM_EXPR_LOOP,
	HELIUM_EXPR_RECUR,
	HELIUM_EXPR_LAMBDA,
	HELIUM_EXPR_RECORD_LIT,
	HELIUM_EXPR_ARRAY_LIT,
	HELIUM_EXPR_FIELD,
	HELIUM_EXPR_ARRAY_GET,
	HELIUM_EXPR_ANNOT,
	HELIUM_EXPR_BIND,
	HELIUM_EXPR_RETURN,
	HELIUM_EXPR_FSTRING,
};

struct helium_literal {
	int kind;
	int line;
	int col;
	char *text;
};

struct helium_expr {
	int kind;
	int line;
	int col;
	struct helium_type *inferred_type;
	union {
		struct helium_literal *lit;
		struct {
			char *name;
		} ident;
		struct {
			int op;
			struct helium_expr *left;
			struct helium_expr *right;
		} binary;
		struct {
			int op;
			struct helium_expr *operand;
		} unary;
		struct {
			struct helium_expr *func;
			struct helium_expr **args;
			size_t arg_count;
			size_t arg_capacity;
		} call;
		struct {
			struct helium_binding **bindings;
			size_t binding_count;
			size_t binding_capacity;
			struct helium_expr **exprs;
			size_t expr_count;
			size_t expr_capacity;
		} block;
		struct {
			struct helium_expr *cond;
			struct helium_expr *then_branch;
			struct helium_expr *else_branch;
		} if_expr;
		struct {
			struct helium_expr *value;
			struct helium_match_arm **arms;
			size_t arm_count;
			size_t arm_capacity;
		} match;
		struct {
			struct helium_loop_binding **bindings;
			size_t binding_count;
			size_t binding_capacity;
			struct helium_expr *body;
		} loop;
		struct {
			struct helium_expr **args;
			size_t arg_count;
			size_t arg_capacity;
		} recur;
		struct {
			char **type_params;
			size_t type_param_count;
			size_t type_param_capacity;
			struct helium_param **params;
			size_t param_count;
			size_t param_capacity;
			struct helium_type *ret_type;
			struct helium_expr *body;
		} lambda;
		struct {
			char *name;
			struct helium_field_init **fields;
			size_t field_count;
			size_t field_capacity;
		} record_lit;
		struct {
			struct helium_expr **items;
			size_t item_count;
			size_t item_capacity;
		} array_lit;
		struct {
			struct helium_expr *object;
			char *name;
		} field;
		struct {
			struct helium_expr *array;
			struct helium_expr *index;
		} array_get;
		struct {
			struct helium_expr *expr;
			struct helium_type *type;
		} annot;
		struct {
			struct helium_expr *left;
			struct helium_expr *right;
		} bind;
		struct {
			struct helium_expr *expr;
		} ret;
		struct {
			struct helium_fstring_part **parts;
			size_t part_count;
			size_t part_capacity;
		} fstring;
	} u;
};

struct helium_field_init {
	char *name;
	struct helium_expr *value;
	int line;
	int col;
};

struct helium_fstring_part {
	int is_expr;
	union {
		char *text;
		struct helium_expr *expr;
	} u;
	int line;
	int col;
};

struct helium_expr *helium_expr_literal(struct helium_literal *lit,
					int line, int col);
struct helium_expr *helium_expr_ident(const char *name, int line, int col);
struct helium_expr *helium_expr_binary(int op, struct helium_expr *left,
				       struct helium_expr *right, int line,
				       int col);
struct helium_expr *helium_expr_unary(int op, struct helium_expr *operand,
				      int line, int col);
struct helium_expr *helium_expr_call(struct helium_expr *func, int line,
				     int col);
struct helium_expr *helium_expr_block(int line, int col);
struct helium_expr *helium_expr_if(struct helium_expr *cond,
				   struct helium_expr *then_branch,
				   struct helium_expr *else_branch,
				   int line, int col);
struct helium_expr *helium_expr_match(struct helium_expr *value, int line,
				      int col);
struct helium_expr *helium_expr_loop(int line, int col);
struct helium_expr *helium_expr_recur(int line, int col);
struct helium_expr *helium_expr_lambda(int line, int col);
struct helium_expr *helium_expr_record_lit(const char *name, int line,
					   int col);
struct helium_expr *helium_expr_array_lit(int line, int col);
struct helium_expr *helium_expr_field(struct helium_expr *object,
				      const char *name, int line, int col);
struct helium_expr *helium_expr_array_get(struct helium_expr *array,
				      struct helium_expr *index, int line,
				      int col);
struct helium_expr *helium_expr_annot(struct helium_expr *expr,
				      struct helium_type *type, int line,
				      int col);
struct helium_expr *helium_expr_bind(struct helium_expr *left,
				     struct helium_expr *right, int line,
				     int col);
struct helium_expr *helium_expr_return(struct helium_expr *expr, int line,
				       int col);
struct helium_expr *helium_expr_fstring(int line, int col);

void helium_call_add_arg(struct helium_expr *call, struct helium_expr *arg);
void helium_block_add_binding(struct helium_expr *block,
			      struct helium_binding *binding);
void helium_block_add_expr(struct helium_expr *block, struct helium_expr *expr);
void helium_match_add_arm(struct helium_expr *match,
			  struct helium_match_arm *arm);
void helium_loop_add_binding(struct helium_expr *loop,
			     struct helium_loop_binding *binding);
void helium_recur_add_arg(struct helium_expr *recur, struct helium_expr *arg);
void helium_lambda_add_type_param(struct helium_expr *lambda,
				  const char *name);
void helium_lambda_add_param(struct helium_expr *lambda,
			     struct helium_param *param);
void helium_lambda_set_ret_type(struct helium_expr *lambda,
				struct helium_type *type);
void helium_lambda_set_body(struct helium_expr *lambda,
			    struct helium_expr *body);
void helium_record_lit_add_field(struct helium_expr *lit,
				 struct helium_field_init *field);
void helium_array_lit_add_item(struct helium_expr *lit,
			       struct helium_expr *item);
void helium_fstring_add_text(struct helium_expr *fstring, const char *text,
			     int line, int col);
void helium_fstring_add_expr(struct helium_expr *fstring,
			     struct helium_expr *expr);

struct helium_literal *helium_literal(int kind, const char *text, int line,
				      int col);
void helium_literal_free(struct helium_literal *lit);

struct helium_field_init *helium_field_init(const char *name,
					    struct helium_expr *value,
					    int line, int col);
void helium_field_init_free(struct helium_field_init *field);

struct helium_fstring_part *helium_fstring_part_text(const char *text,
						     int line, int col);
struct helium_fstring_part *helium_fstring_part_expr(struct helium_expr *expr);
void helium_fstring_part_free(struct helium_fstring_part *part);

void helium_expr_free(struct helium_expr *expr);

/* -------------------------------------------------------------------------- */
/* Patterns                                                                   */
/* -------------------------------------------------------------------------- */

enum helium_pattern_kind {
	HELIUM_PATTERN_WILD,
	HELIUM_PATTERN_LITERAL,
	HELIUM_PATTERN_IDENT,
	HELIUM_PATTERN_CONSTRUCTOR,
};

struct helium_pattern {
	int kind;
	int line;
	int col;
	char *name;
	struct helium_literal *lit;
	struct helium_pattern **fields;
	size_t field_count;
	size_t field_capacity;
	char **field_names;
	size_t field_name_count;
	size_t field_name_capacity;
};

struct helium_pattern *helium_pattern_wild(int line, int col);
struct helium_pattern *helium_pattern_literal(struct helium_literal *lit,
					      int line, int col);
struct helium_pattern *helium_pattern_ident(const char *name, int line,
					    int col);
struct helium_pattern *helium_pattern_constructor(const char *name, int line,
						  int col);
void helium_pattern_add_field(struct helium_pattern *pat,
			      const char *name, struct helium_pattern *field);
struct helium_pattern *helium_pattern_copy(struct helium_pattern *pat);
void helium_pattern_free(struct helium_pattern *pat);

/* -------------------------------------------------------------------------- */
/* Match arms, loop bindings, parameters                                      */
/* -------------------------------------------------------------------------- */

struct helium_match_arm {
	struct helium_pattern *pattern;
	struct helium_expr *expr;
	int line;
	int col;
};

struct helium_match_arm *helium_match_arm(struct helium_pattern *pattern,
					  struct helium_expr *expr,
					  int line, int col);
void helium_match_arm_free(struct helium_match_arm *arm);

struct helium_loop_binding {
	char *name;
	struct helium_type *type;
	struct helium_expr *init;
	int line;
	int col;
};

struct helium_loop_binding *helium_loop_binding(const char *name,
						struct helium_type *type,
						struct helium_expr *init,
						int line, int col);
void helium_loop_binding_free(struct helium_loop_binding *binding);

struct helium_param {
	char *name;
	struct helium_type *type;
	int line;
	int col;
};

struct helium_param *helium_param(const char *name, struct helium_type *type,
				  int line, int col);
void helium_param_free(struct helium_param *param);

/* -------------------------------------------------------------------------- */
/* Type definitions                                                           */
/* -------------------------------------------------------------------------- */

enum helium_type_def_kind {
	HELIUM_TYPE_DEF_RECORD,
	HELIUM_TYPE_DEF_ADT,
};

struct helium_record_field {
	char *name;
	struct helium_type *type;
	int line;
	int col;
};

struct helium_record_field *helium_record_field(const char *name,
						struct helium_type *type,
						int line, int col);
void helium_record_field_free(struct helium_record_field *field);

struct helium_variant {
	char *name;
	struct helium_record_field **fields;
	size_t field_count;
	size_t field_capacity;
	struct helium_type **type_args;
	size_t type_arg_count;
	size_t type_arg_capacity;
	int line;
	int col;
};

struct helium_variant *helium_variant(const char *name, int line, int col);
void helium_variant_add_field(struct helium_variant *variant,
			      struct helium_record_field *field);
void helium_variant_add_type_arg(struct helium_variant *variant,
				 struct helium_type *arg);
void helium_variant_free(struct helium_variant *variant);

struct helium_type_def {
	char *name;
	char **params;
	size_t param_count;
	size_t param_capacity;
	int kind;
	union {
		struct {
			struct helium_record_field **fields;
			size_t field_count;
			size_t field_capacity;
		} record;
		struct {
			struct helium_variant **variants;
			size_t variant_count;
			size_t variant_capacity;
		} adt;
	} u;
	int line;
	int col;
};

struct helium_type_def *helium_type_def(const char *name, int kind, int line,
					int col);
void helium_type_def_add_param(struct helium_type_def *def,
			       const char *param);
void helium_type_def_add_record_field(struct helium_type_def *def,
				      struct helium_record_field *field);
void helium_type_def_add_variant(struct helium_type_def *def,
				 struct helium_variant *variant);
void helium_type_def_free(struct helium_type_def *def);

/* -------------------------------------------------------------------------- */
/* Bindings and imports                                                       */
/* -------------------------------------------------------------------------- */

struct helium_binding {
	char *name;
	struct helium_type *type;
	struct helium_expr *value;
	int line;
	int col;
};

struct helium_binding *helium_binding(const char *name,
				      struct helium_type *type,
				      struct helium_expr *value,
				      int line, int col);
void helium_binding_free(struct helium_binding *binding);

struct helium_import {
	char *path;
	int line;
	int col;
};

struct helium_import *helium_import(const char *path, int line, int col);
void helium_import_free(struct helium_import *import);

/* -------------------------------------------------------------------------- */
/* Top-level declarations                                                     */
/* -------------------------------------------------------------------------- */

enum helium_decl_kind {
	HELIUM_DECL_IMPORT,
	HELIUM_DECL_TYPE,
	HELIUM_DECL_BINDING,
	HELIUM_DECL_FOREIGN,
};

struct helium_top_decl {
	int kind;
	int line;
	int col;
	union {
		struct helium_import *import;
		struct helium_type_def *type_def;
		struct helium_binding *binding;
		struct {
			char *name;
			struct helium_type *type;
			char **type_params;
			size_t type_param_count;
			int injected;
		} foreign;
	} u;
};

struct helium_top_decl *helium_decl_import(struct helium_import *import,
					   int line, int col);
struct helium_top_decl *helium_decl_type(struct helium_type_def *type_def);
struct helium_top_decl *helium_decl_binding(struct helium_binding *binding);
struct helium_top_decl *helium_decl_foreign(const char *name,
					    char **type_params,
					    size_t type_param_count,
					    struct helium_type *type,
					    int line, int col);
void helium_top_decl_free(struct helium_top_decl *decl);

/* -------------------------------------------------------------------------- */
/* Module                                                                     */
/* -------------------------------------------------------------------------- */

struct helium_module {
	char *name;
	struct helium_top_decl **decls;
	size_t decl_count;
	size_t decl_capacity;
	int line;
	int col;
};

struct helium_module *helium_module(const char *name, int line, int col);
void helium_module_add_decl(struct helium_module *module,
			    struct helium_top_decl *decl);
void helium_module_free(struct helium_module *module);

#endif /* HELIUM_AST_H */
