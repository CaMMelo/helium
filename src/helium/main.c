/* SPDX-License-Identifier: TBD */
/*
 * main.c - Helium compiler driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"

extern const char *helium_version(void);

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s <input.hel> -o <output>\n", prog);
}

int main(int argc, char *argv[])
{
	const char *input = NULL;
	const char *output = NULL;
	char *error;
	int i;

	if (argc < 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
			  strcmp(argv[1], "--version") == 0)) {
		printf("Helium compiler bootstrap version %s\n", helium_version());
		return EXIT_SUCCESS;
	}

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return EXIT_FAILURE;
			}
			output = argv[++i];
		} else if (argv[i][0] != '-') {
			input = argv[i];
		} else {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (!input || !output) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	if (helium_compile_file(input, output, NULL, &error) < 0) {
		fprintf(stderr, "error: %s\n", error ? error : "compilation failed");
		free(error);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
