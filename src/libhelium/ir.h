/* SPDX-License-Identifier: TBD */
/*
 * ir.h - Monomorphic intermediate representation for Helium.
 */

#ifndef HELIUM_IR_H
#define HELIUM_IR_H

#include <stdio.h>

#include "ast.h"

/* IR-specific f-string part; the AST part holds expressions, the IR part
 * holds instructions.
 */
struct helium_ir_fstring_part {
	int is_expr;
	union {
		char *text;
		struct helium_ir_instr *expr;
	} u;
	int line;
	int col;
};

/* -------------------------------------------------------------------------- */
/* Program, functions, and types                                              */
/* -------------------------------------------------------------------------- */

enum helium_ir_type_kind {
	HELIUM_IR_TYPE_RECORD,
	HELIUM_IR_TYPE_ADT,
};

struct helium_ir_field {
	char *name;
	struct helium_type *type;
	int line;
	int col;
};

struct helium_ir_variant {
	char *name;
	struct helium_ir_field **fields;
	size_t field_count;
	size_t field_capacity;
	int line;
	int col;
};

struct helium_ir_type {
	char *name;
	int kind;
	int line;
	int col;
	struct helium_ir_field **fields;
	size_t field_count;
	size_t field_capacity;
	struct helium_ir_variant **variants;
	size_t variant_count;
	size_t variant_capacity;
};

struct helium_ir_param {
	char *name;
	struct helium_type *type;
	int line;
	int col;
};

struct helium_ir_block;
struct helium_ir_instr;

struct helium_ir_function {
	char *name;
	struct helium_ir_param **params;
	size_t param_count;
	size_t param_capacity;
	struct helium_type *ret_type;
	struct helium_ir_block *body;
	int is_foreign;
	int is_closure;
	char **capture_names;
	struct helium_type **capture_types;
	size_t capture_count;
	size_t capture_capacity;
	int line;
	int col;
};

struct helium_ir_program {
	struct helium_ir_function **functions;
	size_t function_count;
	size_t function_capacity;
	struct helium_ir_type **types;
	size_t type_count;
	size_t type_capacity;
	char *main_name;
};

/* -------------------------------------------------------------------------- */
/* Instructions and blocks                                                    */
/* -------------------------------------------------------------------------- */

enum helium_ir_instr_kind {
	HELIUM_IR_INSTR_LITERAL,
	HELIUM_IR_INSTR_IDENT,
	HELIUM_IR_INSTR_CALL,
	HELIUM_IR_INSTR_TAIL_CALL,
	HELIUM_IR_INSTR_FOREIGN_CALL,
	HELIUM_IR_INSTR_RECORD_ALLOC,
	HELIUM_IR_INSTR_RECORD_GET,
	HELIUM_IR_INSTR_RECORD_SET,
	HELIUM_IR_INSTR_VARIANT_ALLOC,
	HELIUM_IR_INSTR_ARRAY_ALLOC,
	HELIUM_IR_INSTR_ARRAY_GET,
	HELIUM_IR_INSTR_IF,
	HELIUM_IR_INSTR_MATCH,
	HELIUM_IR_INSTR_LOOP,
	HELIUM_IR_INSTR_RECUR,
	HELIUM_IR_INSTR_LET,
	HELIUM_IR_INSTR_RETURN,
	HELIUM_IR_INSTR_RETAIN,
	HELIUM_IR_INSTR_RELEASE,
	HELIUM_IR_INSTR_BLOCK,
	HELIUM_IR_INSTR_BINARY,
	HELIUM_IR_INSTR_UNARY,
	HELIUM_IR_INSTR_FSTRING,
	HELIUM_IR_INSTR_CLOSURE_ALLOC,
	HELIUM_IR_INSTR_CLOSURE_CALL,
};

struct helium_ir_match_arm {
	struct helium_pattern *pattern;
	struct helium_ir_block *body;
	int line;
	int col;
};

struct helium_ir_let_binding {
	char *name;
	struct helium_type *type;
	struct helium_ir_instr *value;
	int line;
	int col;
};

struct helium_ir_block {
	struct helium_ir_instr **instrs;
	size_t instr_count;
	size_t instr_capacity;
};

struct helium_ir_instr {
	int kind;
	int line;
	int col;
	struct helium_type *type;
	union {
		struct {
			struct helium_literal *lit;
		} literal;
		struct {
			char *name;
		} ident;
		struct {
			char *name;
			struct helium_ir_instr **args;
			size_t arg_count;
			size_t arg_capacity;
		} call;
		struct {
			char *name;
			struct helium_ir_instr **args;
			size_t arg_count;
			size_t arg_capacity;
		} foreign_call;
		struct {
			char *type_name;
			struct helium_ir_instr **field_values;
			size_t field_count;
			size_t field_capacity;
		} record_alloc;
		struct {
			struct helium_ir_instr *object;
			char *field_name;
		} record_get;
		struct {
			struct helium_ir_instr *object;
			char *field_name;
			struct helium_ir_instr *value;
		} record_set;
		struct {
			char *type_name;
			char *variant_name;
			struct helium_ir_instr **field_values;
			size_t field_count;
			size_t field_capacity;
		} variant_alloc;
		struct {
			struct helium_type *elem_type;
			long size;
			struct helium_ir_instr **items;
			size_t item_count;
			size_t item_capacity;
		} array_alloc;
		struct {
			struct helium_ir_instr *array;
			struct helium_ir_instr *index;
		} array_get;
		struct {
			struct helium_ir_instr *cond;
			struct helium_ir_block *then_branch;
			struct helium_ir_block *else_branch;
		} if_expr;
		struct {
			struct helium_ir_instr *value;
			struct helium_ir_match_arm **arms;
			size_t arm_count;
			size_t arm_capacity;
		} match;
		struct {
			struct helium_ir_let_binding **bindings;
			size_t binding_count;
			size_t binding_capacity;
			struct helium_ir_block *body;
		} loop;
		struct {
			struct helium_ir_instr **args;
			size_t arg_count;
			size_t arg_capacity;
		} recur;
		struct {
			char *name;
			struct helium_type *type;
			struct helium_ir_instr *value;
		} let;
		struct {
			struct helium_ir_instr *value;
		} ret;
		struct {
			struct helium_ir_instr *value;
		} retain;
		struct {
			struct helium_ir_instr *value;
		} release;
		struct {
			struct helium_ir_block *block;
		} block;
		struct {
			int op;
			struct helium_ir_instr *left;
			struct helium_ir_instr *right;
		} binary;
		struct {
			int op;
			struct helium_ir_instr *operand;
		} unary;
		struct {
			struct helium_ir_fstring_part **parts;
			size_t part_count;
			size_t part_capacity;
		} fstring;
		struct {
			char *func_name;
			char **capture_names;
			struct helium_ir_instr **capture_values;
			size_t name_count;
			size_t name_capacity;
			size_t value_count;
			size_t value_capacity;
		} closure_alloc;
		struct {
			struct helium_ir_instr *closure;
			struct helium_ir_instr **args;
			size_t arg_count;
			size_t arg_capacity;
		} closure_call;
	} u;
};

/* -------------------------------------------------------------------------- */
/* Constructors                                                               */
/* -------------------------------------------------------------------------- */

struct helium_ir_program *helium_ir_program_new(void);
void helium_ir_program_add_function(struct helium_ir_program *prog,
				    struct helium_ir_function *func);
void helium_ir_program_add_type(struct helium_ir_program *prog,
				struct helium_ir_type *type);
void helium_ir_program_free(struct helium_ir_program *prog);

struct helium_ir_function *helium_ir_function_new(const char *name,
					  struct helium_type *ret_type,
					  int line, int col);
void helium_ir_function_add_param(struct helium_ir_function *func,
				  struct helium_ir_param *param);
void helium_ir_function_set_body(struct helium_ir_function *func,
				 struct helium_ir_block *body);
void helium_ir_function_add_capture(struct helium_ir_function *func,
				    const char *name,
				    struct helium_type *type);
void helium_ir_function_free(struct helium_ir_function *func);

struct helium_ir_param *helium_ir_param_new(const char *name,
					    struct helium_type *type,
					    int line, int col);
void helium_ir_param_free(struct helium_ir_param *param);

struct helium_ir_type *helium_ir_type_new(const char *name, int kind,
					  int line, int col);
void helium_ir_type_add_field(struct helium_ir_type *type,
			      struct helium_ir_field *field);
void helium_ir_type_add_variant(struct helium_ir_type *type,
				struct helium_ir_variant *variant);
void helium_ir_type_free(struct helium_ir_type *type);

struct helium_ir_field *helium_ir_field_new(const char *name,
					    struct helium_type *type,
					    int line, int col);
void helium_ir_field_free(struct helium_ir_field *field);

struct helium_ir_variant *helium_ir_variant_new(const char *name, int line,
						int col);
void helium_ir_variant_add_field(struct helium_ir_variant *variant,
				 struct helium_ir_field *field);
void helium_ir_variant_free(struct helium_ir_variant *variant);

struct helium_ir_block *helium_ir_block_new(void);
void helium_ir_block_add_instr(struct helium_ir_block *block,
			       struct helium_ir_instr *instr);
void helium_ir_block_free(struct helium_ir_block *block);

struct helium_ir_instr *helium_ir_instr_literal(struct helium_literal *lit,
					int line, int col);
struct helium_ir_instr *helium_ir_instr_ident(const char *name, int line,
					      int col);
struct helium_ir_instr *helium_ir_instr_call(const char *name, int line,
					     int col);
struct helium_ir_instr *helium_ir_instr_tail_call(const char *name, int line,
						  int col);
struct helium_ir_instr *helium_ir_instr_foreign_call(const char *name, int line,
						     int col);
void helium_ir_call_add_arg(struct helium_ir_instr *call,
			    struct helium_ir_instr *arg);
struct helium_ir_instr *helium_ir_instr_record_alloc(const char *type_name,
					     int line, int col);
void helium_ir_record_alloc_add_field(struct helium_ir_instr *alloc,
				      struct helium_ir_instr *value);
struct helium_ir_instr *helium_ir_instr_record_get(struct helium_ir_instr *object,
					   const char *field_name,
					   int line, int col);
struct helium_ir_instr *helium_ir_instr_record_set(struct helium_ir_instr *object,
					   const char *field_name,
					   struct helium_ir_instr *value,
					   int line, int col);
struct helium_ir_instr *helium_ir_instr_variant_alloc(const char *type_name,
					      const char *variant_name,
					      int line, int col);
void helium_ir_variant_alloc_add_field(struct helium_ir_instr *alloc,
				       struct helium_ir_instr *value);
struct helium_ir_instr *helium_ir_instr_array_alloc(struct helium_type *elem_type,
					    long size, int line,
					    int col);
void helium_ir_array_alloc_add_item(struct helium_ir_instr *alloc,
				    struct helium_ir_instr *item);
struct helium_ir_instr *helium_ir_instr_array_get(struct helium_ir_instr *array,
					  struct helium_ir_instr *index,
					  int line, int col);
struct helium_ir_instr *helium_ir_instr_if(struct helium_ir_instr *cond,
					   int line, int col);
void helium_ir_if_set_then(struct helium_ir_instr *instr,
			   struct helium_ir_block *block);
void helium_ir_if_set_else(struct helium_ir_instr *instr,
			   struct helium_ir_block *block);
struct helium_ir_instr *helium_ir_instr_match(struct helium_ir_instr *value,
					      int line, int col);
void helium_ir_match_add_arm(struct helium_ir_instr *match,
			     struct helium_ir_match_arm *arm);
struct helium_ir_match_arm *helium_ir_match_arm(struct helium_pattern *pattern,
						struct helium_ir_block *body,
						int line, int col);
void helium_ir_match_arm_free(struct helium_ir_match_arm *arm);
struct helium_ir_instr *helium_ir_instr_loop(int line, int col);
void helium_ir_loop_add_binding(struct helium_ir_instr *loop,
				struct helium_ir_let_binding *binding);
void helium_ir_loop_set_body(struct helium_ir_instr *loop,
			     struct helium_ir_block *body);
struct helium_ir_instr *helium_ir_instr_recur(int line, int col);
void helium_ir_recur_add_arg(struct helium_ir_instr *recur,
			     struct helium_ir_instr *arg);
struct helium_ir_instr *helium_ir_instr_let(const char *name,
					    struct helium_type *type,
					    struct helium_ir_instr *value,
					    int line, int col);
struct helium_ir_instr *helium_ir_instr_return(struct helium_ir_instr *value,
					       int line, int col);
struct helium_ir_instr *helium_ir_instr_retain(struct helium_ir_instr *value,
					       int line, int col);
struct helium_ir_instr *helium_ir_instr_release(struct helium_ir_instr *value,
						int line, int col);
struct helium_ir_instr *helium_ir_instr_block(int line, int col);
void helium_ir_instr_block_add_instr(struct helium_ir_instr *block,
				     struct helium_ir_instr *instr);
struct helium_ir_instr *helium_ir_instr_binary(int op,
					       struct helium_ir_instr *left,
					       struct helium_ir_instr *right,
					       int line, int col);
struct helium_ir_instr *helium_ir_instr_unary(int op,
				      struct helium_ir_instr *operand,
				      int line, int col);
struct helium_ir_instr *helium_ir_instr_fstring(int line, int col);
void helium_ir_fstring_add_text(struct helium_ir_instr *fstring,
				const char *text, int line, int col);
void helium_ir_fstring_add_expr(struct helium_ir_instr *fstring,
				struct helium_ir_instr *expr);
struct helium_ir_instr *helium_ir_instr_closure_alloc(const char *func_name,
				      int line, int col);
void helium_ir_closure_alloc_add_capture(struct helium_ir_instr *alloc,
					 const char *name,
					 struct helium_ir_instr *value);
struct helium_ir_instr *helium_ir_instr_closure_call(
				struct helium_ir_instr *closure,
				int line, int col);
void helium_ir_closure_call_add_arg(struct helium_ir_instr *call,
				    struct helium_ir_instr *arg);
void helium_ir_instr_free(struct helium_ir_instr *instr);

struct helium_ir_let_binding *helium_ir_let_binding_new(const char *name,
						struct helium_type *type,
						struct helium_ir_instr *value,
						int line, int col);
void helium_ir_let_binding_free(struct helium_ir_let_binding *binding);

/* -------------------------------------------------------------------------- */
/* Printing                                                                     */
/* -------------------------------------------------------------------------- */

void helium_ir_program_print(struct helium_ir_program *prog, FILE *out);

#endif /* HELIUM_IR_H */
