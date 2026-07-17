/* SPDX-License-Identifier: TBD */
/*
 * io.c - C implementation of the std.io foreign functions.
 *
 * These are ordinary C functions called through the FFI.  They follow the
 * naming convention "<module>_<function>" because the compiler prefixes each
 * module's top-level names with the module name when emitting object code.
 */

#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

int64_t
io_println(const char *s)
{
	puts(s ? s : "");
	return 0;
}

int64_t
io_prints(const char *s)
{
	fputs(s ? s : "", stdout);
	return 0;
}

int64_t
io_print_int(int32_t n)
{
	printf("%d", n);
	return 0;
}

int64_t
io_print_bool(int8_t b)
{
	printf("%s", b ? "true" : "false");
	return 0;
}

char *
io_read_line(void)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t n;

	n = getline(&line, &cap, stdin);
	if (n < 0) {
		free(line);
		line = malloc(1);
		if (!line)
			abort();
		line[0] = '\0';
	} else if ((size_t)n > 0 && line[n - 1] == '\n') {
		line[n - 1] = '\0';
	}
	return line;
}
