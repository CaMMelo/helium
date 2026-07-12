/* SPDX-License-Identifier: TBD */
/*
 * ffi.h - Foreign function interface helpers for Helium.
 */

#ifndef HELIUM_FFI_H
#define HELIUM_FFI_H

/*
 * helium_ffi_read_link_flags - Read extra linker flags from a Heliumfile.
 *
 * Looks for a line of the form:
 *
 *     link = -lm -lpthread
 *
 * Lines beginning with '#' are ignored.  Returns a heap-allocated string
 * that must be freed by the caller, or NULL if no flags are found.
 */
char *helium_ffi_read_link_flags(const char *path);

#endif /* HELIUM_FFI_H */
