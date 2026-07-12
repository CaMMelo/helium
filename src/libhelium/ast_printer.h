/* SPDX-License-Identifier: TBD */
/*
 * ast_printer.h - Pretty printer for Helium ASTs.
 */

#ifndef HELIUM_AST_PRINTER_H
#define HELIUM_AST_PRINTER_H

#include <stdio.h>

struct helium_module;

void helium_ast_print(FILE *out, struct helium_module *module);

#endif /* HELIUM_AST_PRINTER_H */
