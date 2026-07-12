/* SPDX-License-Identifier: TBD */
/*
 * modules.h - Module resolution and interface files for Helium.
 */

#ifndef HELIUM_MODULES_H
#define HELIUM_MODULES_H

#include "ast.h"

struct helium_env;

/* -------------------------------------------------------------------------- */
/* Module interface files (.hei)                                              */
/* -------------------------------------------------------------------------- */

struct helium_interface_export {
	char *name;
	struct helium_type *type;
};

struct helium_module_interface {
	char *name;
	char *source_path;
	char *object_path;
	struct helium_interface_export **exports;
	size_t export_count;
	size_t export_capacity;
};

struct helium_module_interface *helium_module_interface_new(const char *name,
						    const char *source_path,
						    const char *object_path);
void helium_module_interface_free(struct helium_module_interface *iface);
void helium_module_interface_add_export(struct helium_module_interface *iface,
					const char *name,
					struct helium_type *type);
struct helium_type *helium_module_interface_lookup(
				struct helium_module_interface *iface,
				const char *name);

/*
 * Load a .hei interface file from disk.  On error returns NULL and sets
 * *error to a heap-allocated message.
 */
struct helium_module_interface *helium_module_interface_load(
					const char *path, char **error);

/*
 * Emit a .hei interface file describing the exports of a module.
 * The typed module's environment is used to obtain the final types.
 * Returns 0 on success, -1 on error.
 */
int helium_module_interface_emit(struct helium_module *module,
				 struct helium_env *env,
				 const char *path, char **error);

/* -------------------------------------------------------------------------- */
/* Resolution                                                                 */
/* -------------------------------------------------------------------------- */

/*
 * Search paths used when resolving an import path to a source/interface file.
 */
struct helium_search_path {
	char **roots;
	size_t count;
	size_t capacity;
};

struct helium_search_path *helium_search_path_new(void);
void helium_search_path_free(struct helium_search_path *sp);
void helium_search_path_add(struct helium_search_path *sp, const char *root);

/*
 * Resolve an import path such as "std.io" against the search path.
 * On success fills *source_path and *module_name with heap-allocated strings
 * and returns 0.  On failure returns -1 and sets *error.
 */
int helium_module_resolve(const char *import_path,
			  struct helium_search_path *sp,
			  char **source_path,
			  char **module_name,
			  char **error);

/*
 * Build a search path for a project given the directory containing the
 * entry source file.  Adds <root>/lib and <cwd>/.helium/<name>/<version>
 * entries for each known package name found in the cache.
 */
struct helium_search_path *helium_search_path_for_project(
					const char *source_path, char **error);

/* -------------------------------------------------------------------------- */
/* Import context                                                             */
/* -------------------------------------------------------------------------- */

struct helium_import_info {
	char *path;
	char *prefix;
	char *source_path;
	char *object_path;
	char *interface_path;
	struct helium_module_interface *iface;
};

struct helium_import_context {
	struct helium_import_info **imports;
	size_t count;
	size_t capacity;
	char **object_paths;
	size_t object_count;
	size_t object_capacity;
};

struct helium_import_context *helium_import_context_new(void);
void helium_import_context_free(struct helium_import_context *ctx);
struct helium_import_info *helium_import_context_add(
				struct helium_import_context *ctx,
				const char *path,
				const char *prefix,
				const char *source_path,
				const char *object_path,
				const char *interface_path);
struct helium_import_info *helium_import_context_lookup(
				struct helium_import_context *ctx,
				const char *prefix);

/* -------------------------------------------------------------------------- */
/* AST rewriting                                                              */
/* -------------------------------------------------------------------------- */

/*
 * Rewrite qualified access such as "io.println" to a single identifier
 * "io_println" for every import prefix recorded in the context.
 */
void helium_rewrite_qualified_access(struct helium_module *module,
				     struct helium_import_context *ctx);

/*
 * Prefix all top-level binding names and their references inside a module
 * with "<module_name>_".  This is applied to imported modules before they
 * are compiled separately.
 */
void helium_prefix_module_names(struct helium_module *module,
				const char *module_name);

#endif /* HELIUM_MODULES_H */
