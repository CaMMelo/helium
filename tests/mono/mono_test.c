/* SPDX-License-Identifier: TBD */
/*
 * mono_test.c - Driver that type-checks and monomorphizes a Helium file.
 *
 * Usage: mono_test <file.hel>
 *
 * On success, prints the monomorphic IR and exits with status 0.
 * On failure, prints the error to stderr and exits with a non-zero status.
 */

#include <stdio.h>
#include <stdlib.h>

#include "inference.h"
#include "ir.h"
#include "mono.h"
#include "parser.h"
#include "token.h"

int main(int argc, char *argv[])
{
	FILE *f;
	struct helium_token *tokens;
	size_t count;
	char *error;
	struct helium_module *module;
	struct helium_typed_module *typed;
	struct helium_ir_program *prog;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <file.hel>\n", argv[0]);
		return EXIT_FAILURE;
	}

	f = fopen(argv[1], "r");
	if (!f) {
		perror(argv[1]);
		return EXIT_FAILURE;
	}

	if (helium_lex_file(f, &tokens, &count, &error) < 0) {
		fprintf(stderr, "%s\n", error ? error : "lexer error");
		free(error);
		fclose(f);
		return EXIT_FAILURE;
	}

	fclose(f);

	module = helium_parse_tokens(tokens, count, argv[1], &error);
	helium_tokens_free(tokens, count);

	if (!module) {
		fprintf(stderr, "%s\n", error ? error : "parse error");
		free(error);
		return EXIT_FAILURE;
	}

	if (helium_infer_module(module, &typed, &error) < 0) {
		fprintf(stderr, "%s\n", error ? error : "type error");
		free(error);
		helium_module_free(module);
		return EXIT_FAILURE;
	}

	prog = helium_monomorphize(typed, &error);
	helium_typed_module_free(typed);

	if (!prog) {
		fprintf(stderr, "%s\n", error ? error : "monomorphization error");
		free(error);
		helium_module_free(module);
		return EXIT_FAILURE;
	}

	helium_ir_program_print(prog, stdout);
	helium_ir_program_free(prog);
	helium_module_free(module);
	return EXIT_SUCCESS;
}
