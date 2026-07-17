/* SPDX-License-Identifier: TBD */
/*
 * string.c - C implementation of std.string foreign functions.
 */

#include <stdint.h>
#include <string.h>

int8_t
string_equals(const char *a, const char *b)
{
	return strcmp(a ? a : "", b ? b : "") == 0;
}
