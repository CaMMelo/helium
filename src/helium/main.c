/* SPDX-License-Identifier: TBD */
/*
 * main.c - Helium compiler driver (bootstrap placeholder).
 */

#include <stdio.h>
#include <stdlib.h>

extern const char *helium_version(void);

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	printf("Helium compiler bootstrap version %s\n", helium_version());
	printf("Full compiler implementation is tracked in specs/SPEC-011-driver-cli.md\n");
	return EXIT_SUCCESS;
}
