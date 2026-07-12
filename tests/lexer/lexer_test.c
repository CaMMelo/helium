/* SPDX-License-Identifier: TBD */
/*
 * lexer_test.c - Simple driver that tokenizes a Helium source file.
 *
 * Usage: lexer_test <file.hel>
 *
 * On success, prints one token per line:
 *     NAME line col
 *     NAME line col 'text'
 *
 * On failure, prints the error to stderr and exits with a non-zero status.
 */

#include <stdio.h>
#include <stdlib.h>

#include "token.h"

int main(int argc, char *argv[])
{
	FILE *f;
	struct helium_token *tokens;
	size_t count;
	char *error;
	size_t i;

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

	for (i = 0; i < count; i++) {
		const struct helium_token *t = &tokens[i];

		printf("%s %d %d", helium_token_name(t->type), t->line, t->col);
		if (t->text)
			printf(" '%s'", t->text);
		printf("\n");
	}

	helium_tokens_free(tokens, count);
	return EXIT_SUCCESS;
}
