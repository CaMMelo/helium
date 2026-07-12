/* SPDX-License-Identifier: TBD */
/*
 * ast_printer.c - S-expression pretty printer for Helium ASTs.
 */

#include "ast_printer.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "parser.tab.h"

static void print_indent(FILE *out, int depth)
{
	int i;

	for (i = 0; i < depth; i++)
		fputs("  ", out);
}

static void print_type(FILE *out, struct helium_type *type);
static void print_expr(FILE *out, struct helium_expr *expr, int depth);
static void print_pattern(FILE *out, struct helium_pattern *pat, int depth);

static void print_type(FILE *out, struct helium_type *type)
{
	size_t i;

	if (!type) {
		fputs("?", out);
		return;
	}

	switch (type->kind) {
	case HELIUM_TYPE_NAMED:
		fprintf(out, "%s", type->name);
		if (type->arg_count > 0) {
			fputs("<", out);
			for (i = 0; i < type->arg_count; i++) {
				if (i > 0)
					fputs(", ", out);
				print_type(out, type->args[i]);
			}
			fputs(">", out);
		}
		break;
	case HELIUM_TYPE_FN:
		fputs("fn(", out);
		for (i = 0; i < type->param_count; i++) {
			if (i > 0)
				fputs(", ", out);
			print_type(out, type->params[i]);
		}
		fputs(") -> ", out);
		print_type(out, type->ret);
		break;
	case HELIUM_TYPE_ARRAY:
		fputs("[", out);
		print_type(out, type->elem_type);
		fprintf(out, "; %ld]", type->array_size);
		break;
	case HELIUM_TYPE_VAR:
		fprintf(out, "'%s", type->name);
		break;
	}
}

static void print_literal(FILE *out, struct helium_literal *lit)
{
	switch (lit->kind) {
	case HELIUM_LIT_INT:
		fprintf(out, "%s", lit->text);
		break;
	case HELIUM_LIT_FLOAT:
		fprintf(out, "%s", lit->text);
		break;
	case HELIUM_LIT_BOOL:
		fprintf(out, "%s", lit->text);
		break;
	case HELIUM_LIT_STRING:
		fprintf(out, "\"%s\"", lit->text);
		break;
	case HELIUM_LIT_UNIT:
		fputs("()", out);
		break;
	}
}

static const char *op_name(int op)
{
	switch (op) {
	case PLUS:
		return "+";
	case MINUS:
		return "-";
	case STAR:
		return "*";
	case SLASH:
		return "/";
	case PERCENT:
		return "%";
	case EQEQ:
		return "==";
	case NEQ:
		return "!=";
	case LT:
		return "<";
	case LE:
		return "<=";
	case GT:
		return ">";
	case GE:
		return ">=";
	case AND:
		return "&&";
	case OR:
		return "||";
	case NOT:
		return "!";
	case BIND:
		return ">>=";
	default:
		return "?";
	}
}

static void print_expr(FILE *out, struct helium_expr *expr, int depth)
{
	size_t i;

	if (!expr) {
		fputs("()", out);
		return;
	}

	switch (expr->kind) {
	case HELIUM_EXPR_LITERAL:
		print_literal(out, expr->u.lit);
		break;
	case HELIUM_EXPR_IDENT:
		fprintf(out, "%s", expr->u.ident.name);
		break;
	case HELIUM_EXPR_BINARY:
		fprintf(out, "(%s ", op_name(expr->u.binary.op));
		print_expr(out, expr->u.binary.left, depth);
		fputs(" ", out);
		print_expr(out, expr->u.binary.right, depth);
		fputs(")", out);
		break;
	case HELIUM_EXPR_UNARY:
		fprintf(out, "(%s ", op_name(expr->u.unary.op));
		print_expr(out, expr->u.unary.operand, depth);
		fputs(")", out);
		break;
	case HELIUM_EXPR_CALL:
		fputs("(call ", out);
		print_expr(out, expr->u.call.func, depth);
		for (i = 0; i < expr->u.call.arg_count; i++) {
			fputs(" ", out);
			print_expr(out, expr->u.call.args[i], depth);
		}
		fputs(")", out);
		break;
	case HELIUM_EXPR_BLOCK:
		fputs("(block\n", out);
		for (i = 0; i < expr->u.block.binding_count; i++) {
			print_indent(out, depth + 1);
			fprintf(out, "(bind %s ",
				expr->u.block.bindings[i]->name);
			if (expr->u.block.bindings[i]->type) {
				print_type(out, expr->u.block.bindings[i]->type);
				fputs(" ", out);
			}
			print_expr(out, expr->u.block.bindings[i]->value,
				   depth + 1);
			fputs(")\n", out);
		}
		for (i = 0; i < expr->u.block.expr_count; i++) {
			print_indent(out, depth + 1);
			print_expr(out, expr->u.block.exprs[i], depth + 1);
			fputs("\n", out);
		}
		print_indent(out, depth);
		fputs(")", out);
		break;
	case HELIUM_EXPR_IF:
		fputs("(if ", out);
		print_expr(out, expr->u.if_expr.cond, depth);
		fputs("\n", out);
		print_expr(out, expr->u.if_expr.then_branch, depth + 1);
		fputs("\n", out);
		print_expr(out, expr->u.if_expr.else_branch, depth + 1);
		fputs(")", out);
		break;
	case HELIUM_EXPR_MATCH:
		fputs("(match ", out);
		print_expr(out, expr->u.match.value, depth);
		fputs("\n", out);
		for (i = 0; i < expr->u.match.arm_count; i++) {
			print_indent(out, depth + 1);
			print_pattern(out, expr->u.match.arms[i]->pattern,
				      depth + 1);
			fputs(" => ", out);
			print_expr(out, expr->u.match.arms[i]->expr, depth + 1);
			fputs("\n", out);
		}
		print_indent(out, depth);
		fputs(")", out);
		break;
	case HELIUM_EXPR_LOOP:
		fputs("(loop (", out);
		for (i = 0; i < expr->u.loop.binding_count; i++) {
			if (i > 0)
				fputs(" ", out);
			fprintf(out, "(%s ",
				expr->u.loop.bindings[i]->name);
			if (expr->u.loop.bindings[i]->type) {
				print_type(out, expr->u.loop.bindings[i]->type);
				fputs(" ", out);
			}
			print_expr(out, expr->u.loop.bindings[i]->init,
				   depth);
			fputs(")", out);
		}
		fputs(")\n", out);
		print_expr(out, expr->u.loop.body, depth + 1);
		fputs(")", out);
		break;
	case HELIUM_EXPR_RECUR:
		fputs("(recur", out);
		for (i = 0; i < expr->u.recur.arg_count; i++) {
			fputs(" ", out);
			print_expr(out, expr->u.recur.args[i], depth);
		}
		fputs(")", out);
		break;
	case HELIUM_EXPR_LAMBDA:
		fputs("(lambda ", out);
		if (expr->u.lambda.type_param_count > 0) {
			fputs("<", out);
			for (i = 0; i < expr->u.lambda.type_param_count; i++) {
				if (i > 0)
					fputs(", ", out);
				fprintf(out, "%s",
					expr->u.lambda.type_params[i]);
			}
			fputs("> ", out);
		}
		fputs("(", out);
		for (i = 0; i < expr->u.lambda.param_count; i++) {
			if (i > 0)
				fputs(", ", out);
			fprintf(out, "%s",
				expr->u.lambda.params[i]->name);
			if (expr->u.lambda.params[i]->type) {
				fputs(": ", out);
				print_type(out,
					   expr->u.lambda.params[i]->type);
			}
		}
		fputs(")", out);
		if (expr->u.lambda.ret_type) {
			fputs(" -> ", out);
			print_type(out, expr->u.lambda.ret_type);
		}
		fputs("\n", out);
		print_expr(out, expr->u.lambda.body, depth + 1);
		fputs(")", out);
		break;
	case HELIUM_EXPR_RECORD_LIT:
		fprintf(out, "(record %s", expr->u.record_lit.name);
		for (i = 0; i < expr->u.record_lit.field_count; i++) {
			fputs(" ", out);
			fprintf(out, "(%s ",
				expr->u.record_lit.fields[i]->name);
			print_expr(out, expr->u.record_lit.fields[i]->value,
				   depth);
			fputs(")", out);
		}
		fputs(")", out);
		break;
	case HELIUM_EXPR_ARRAY_LIT:
		fputs("(array", out);
		for (i = 0; i < expr->u.array_lit.item_count; i++) {
			fputs(" ", out);
			print_expr(out, expr->u.array_lit.items[i], depth);
		}
		fputs(")", out);
		break;
	case HELIUM_EXPR_FIELD:
		fputs("(field ", out);
		print_expr(out, expr->u.field.object, depth);
		fprintf(out, " %s)", expr->u.field.name);
		break;
	case HELIUM_EXPR_ANNOT:
		fputs("(annot ", out);
		print_expr(out, expr->u.annot.expr, depth);
		fputs(" ", out);
		print_type(out, expr->u.annot.type);
		fputs(")", out);
		break;
	case HELIUM_EXPR_BIND:
		fputs("(bind ", out);
		print_expr(out, expr->u.bind.left, depth);
		fputs(" ", out);
		print_expr(out, expr->u.bind.right, depth);
		fputs(")", out);
		break;
	case HELIUM_EXPR_RETURN:
		fputs("(return ", out);
		print_expr(out, expr->u.ret.expr, depth);
		fputs(")", out);
		break;
	case HELIUM_EXPR_FSTRING:
		fputs("(fstring", out);
		for (i = 0; i < expr->u.fstring.part_count; i++) {
			struct helium_fstring_part *part =
				expr->u.fstring.parts[i];

			fputs(" ", out);
			if (part->is_expr) {
				fputs("{", out);
				print_expr(out, part->u.expr, depth);
				fputs("}", out);
			} else {
				fprintf(out, "\"%s\"", part->u.text);
			}
		}
		fputs(")", out);
		break;
	}
}

static void print_pattern(FILE *out, struct helium_pattern *pat, int depth)
{
	size_t i;

	(void)depth;
	switch (pat->kind) {
	case HELIUM_PATTERN_WILD:
		fputs("_", out);
		break;
	case HELIUM_PATTERN_LITERAL:
		print_literal(out, pat->lit);
		break;
	case HELIUM_PATTERN_IDENT:
		fprintf(out, "%s", pat->name);
		break;
	case HELIUM_PATTERN_CONSTRUCTOR:
		fprintf(out, "(%s", pat->name);
		for (i = 0; i < pat->field_count; i++) {
			fputs(" ", out);
			if (pat->field_names && pat->field_names[i]) {
				fprintf(out, "(%s ", pat->field_names[i]);
				print_pattern(out, pat->fields[i], depth);
				fputs(")", out);
			} else {
				print_pattern(out, pat->fields[i], depth);
			}
		}
		fputs(")", out);
		break;
	}
}

void helium_ast_print(FILE *out, struct helium_module *module)
{
	size_t i;

	if (!module) {
		fputs("(module)\n", out);
		return;
	}

	fprintf(out, "(module%s\n", module->name ? " " : "");
	if (module->name)
		fprintf(out, " %s\n", module->name);
	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];

		print_indent(out, 1);
		switch (decl->kind) {
		case HELIUM_DECL_IMPORT:
			fprintf(out, "(import %s)\n", decl->u.import->path);
			break;
		case HELIUM_DECL_TYPE: {
			struct helium_type_def *def = decl->u.type_def;

			fprintf(out, "(type %s", def->name);
			if (def->param_count > 0) {
				size_t j;

				fputs(" <", out);
				for (j = 0; j < def->param_count; j++) {
					if (j > 0)
						fputs(", ", out);
					fprintf(out, "%s", def->params[j]);
				}
				fputs(">", out);
			}
			if (def->kind == HELIUM_TYPE_DEF_RECORD) {
				size_t j;

				fputs(" {", out);
				for (j = 0; j < def->u.record.field_count; j++) {
					struct helium_record_field *f =
						def->u.record.fields[j];

					if (j > 0)
						fputs(", ", out);
					fprintf(out, "%s: ", f->name);
					print_type(out, f->type);
				}
				fputs("}", out);
			} else {
				size_t j;

				for (j = 0; j < def->u.adt.variant_count; j++) {
					struct helium_variant *v =
						def->u.adt.variants[j];
					size_t k;

					fputs("\n", out);
					print_indent(out, 2);
					fprintf(out, "| %s", v->name);
					if (v->field_count > 0) {
						fputs(" {", out);
						for (k = 0; k < v->field_count; k++) {
							if (k > 0)
								fputs(", ", out);
							fprintf(out, "%s: ",
								v->fields[k]->name);
							print_type(out,
								   v->fields[k]->type);
						}
						fputs("}", out);
					}
					if (v->type_arg_count > 0) {
						fputs("<", out);
						for (k = 0; k < v->type_arg_count; k++) {
							if (k > 0)
								fputs(", ", out);
							print_type(out,
								   v->type_args[k]);
						}
						fputs(">", out);
					}
				}
			}
			fputs(")\n", out);
			break;
		}
		case HELIUM_DECL_BINDING:
			fprintf(out, "(bind %s ", decl->u.binding->name);
			if (decl->u.binding->type) {
				print_type(out, decl->u.binding->type);
				fputs(" ", out);
			}
			print_expr(out, decl->u.binding->value, 1);
			fputs(")\n", out);
			break;
		case HELIUM_DECL_FOREIGN:
			fprintf(out, "(foreign %s ", decl->u.foreign.name);
			print_type(out, decl->u.foreign.type);
			fputs(")\n", out);
			break;
		}
	}
	fputs(")\n", out);
}
