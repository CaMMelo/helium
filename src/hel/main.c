/* SPDX-License-Identifier: TBD */
/*
 * main.c - hel package manager command-line interface.
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "compiler.h"
#include "modules.h"
#include "lock.h"
#include "manifest.h"
#include "util.h"

#define MANIFEST_NAME "Heliumfile"
#define LOCK_NAME "Heliumfile.lock"
#define DEFAULT_EDITION "2025"
#define DEFAULT_VERSION "0.1.0"

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s <command> [args]\n", prog);
	fprintf(stderr, "Commands:\n");
	fprintf(stderr, "  init [name]        Create a new project\n");
	fprintf(stderr, "  build              Compile the project\n");
	fprintf(stderr, "  run                Build and run the project\n");
	fprintf(stderr, "  test               Build and run the test suite\n");
	fprintf(stderr, "  add <pkg>[@ver]    Add a dependency\n");
	fprintf(stderr, "  remove <pkg>       Remove a dependency\n");
	fprintf(stderr, "  update [pkg]       Update dependencies\n");
}

static char *project_root(void)
{
	char cwd[4096];

	if (getcwd(cwd, sizeof(cwd)))
		return hel_xstrdup(cwd);
	return hel_xstrdup(".");
}

static char *compiler_path(void)
{
	const char *env = getenv("HELIUM");
	char *hel_dir;
	char *path;

	if (env)
		return hel_xstrdup(env);
	hel_dir = hel_get_executable_dir();
	path = hel_path_join(hel_dir, "helium");
	free(hel_dir);
	return path;
}

static char *repo_root(void)
{
	char *hel_dir = hel_get_executable_dir();
	char *build_dir = hel_dirname(hel_dir);
	char *repo = hel_dirname(build_dir);

	free(hel_dir);
	free(build_dir);
	return repo;
}

static int run_command(const char *cwd, char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "error: fork failed\n");
		return 1;
	}
	if (pid == 0) {
		if (cwd && chdir(cwd) != 0) {
			fprintf(stderr, "error: cannot chdir to %s\n", cwd);
			_exit(127);
		}
		execvp(argv[0], argv);
		fprintf(stderr, "error: cannot execute %s\n", argv[0]);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return 1;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return 1;
}

struct cache_path_list {
	char **paths;
	size_t count;
	size_t capacity;
};

static void cache_path_list_add(struct cache_path_list *list, char *path)
{
	if (list->count == list->capacity) {
		size_t newcap = list->capacity ? list->capacity * 2 : 4;
		char **tmp = hel_xrealloc(list->paths,
					  newcap * sizeof(char *));

		list->paths = tmp;
		list->capacity = newcap;
	}
	list->paths[list->count++] = path;
}

static void cache_path_list_free(struct cache_path_list *list)
{
	size_t i;

	for (i = 0; i < list->count; i++)
		free(list->paths[i]);
	free(list->paths);
}

static void collect_cache_paths(const char *cache_root,
				struct cache_path_list *list)
{
	DIR *d;
	struct dirent *ent;

	d = opendir(cache_root);
	if (!d)
		return;
	while ((ent = readdir(d)) != NULL) {
		char *pkg_dir;
		DIR *pd;
		struct dirent *vent;

		if (ent->d_name[0] == '.')
			continue;
		pkg_dir = hel_path_join(cache_root, ent->d_name);
		pd = opendir(pkg_dir);
		if (!pd) {
			free(pkg_dir);
			continue;
		}
		while ((vent = readdir(pd)) != NULL) {
			char *ver_dir;

			if (vent->d_name[0] == '.')
				continue;
			ver_dir = hel_path_join(pkg_dir, vent->d_name);
			if (hel_is_dir(ver_dir))
				cache_path_list_add(list, ver_dir);
			else
				free(ver_dir);
		}
		closedir(pd);
		free(pkg_dir);
	}
	closedir(d);
}

static const char **build_module_paths(const char *extra_path,
				       const struct cache_path_list *cache)
{
	const char **paths;
	size_t count = (extra_path ? 1 : 0) + cache->count;
	size_t i;

	paths = hel_xalloc((count + 1) * sizeof(*paths));
	if (extra_path)
		paths[0] = extra_path;
	for (i = 0; i < cache->count; i++)
		paths[(extra_path ? 1 : 0) + i] = cache->paths[i];
	paths[count] = NULL;
	return paths;
}

static void free_module_paths(const char **paths)
{
	free(paths);
}

/* -------------------------------------------------------------------------- */
/* Local library compilation and installation                                 */
/* -------------------------------------------------------------------------- */

struct local_module {
	char *source_path;
	char *rel_path;
	char *pkg;
	char *name;
	char *build_obj;
	char *build_hei;
	char *cache_dir;
};

struct local_module_list {
	struct local_module *modules;
	size_t count;
	size_t capacity;
};

static char *replace_extension(const char *path, const char *new_ext)
{
	const char *dot = strrchr(path, '.');
	char *out;

	if (!dot)
		return hel_xstrdup(new_ext);
	if (asprintf(&out, "%.*s%s", (int)(dot - path), path, new_ext) < 0)
		abort();
	return out;
}

static char *cache_file_path(const char *cache_dir, const char *name,
			     const char *ext)
{
	char *out;

	if (asprintf(&out, "%s/%s%s", cache_dir, name, ext) < 0)
		abort();
	return out;
}

static void local_module_list_add(struct local_module_list *list,
				  const char *source_path,
				  const char *rel_path,
				  const char *pkg,
				  const char *name,
				  const char *build_obj,
				  const char *build_hei,
				  const char *cache_dir)
{
	struct local_module *m;

	if (list->count == list->capacity) {
		size_t newcap = list->capacity ? list->capacity * 2 : 4;

		list->modules = hel_xrealloc(list->modules,
					     newcap * sizeof(*list->modules));
		list->capacity = newcap;
	}
	m = &list->modules[list->count++];
	memset(m, 0, sizeof(*m));
	m->source_path = hel_xstrdup(source_path);
	m->rel_path = hel_xstrdup(rel_path);
	m->pkg = hel_xstrdup(pkg);
	m->name = hel_xstrdup(name);
	m->build_obj = hel_xstrdup(build_obj);
	m->build_hei = hel_xstrdup(build_hei);
	m->cache_dir = hel_xstrdup(cache_dir);
}

static void local_module_list_free(struct local_module_list *list)
{
	size_t i;

	for (i = 0; i < list->count; i++) {
		struct local_module *m = &list->modules[i];

		free(m->source_path);
		free(m->rel_path);
		free(m->pkg);
		free(m->name);
		free(m->build_obj);
		free(m->build_hei);
		free(m->cache_dir);
	}
	free(list->modules);
}

static void scan_lib_dir_recursive(const char *lib_dir, const char *rel_prefix,
				   const char *build_lib_dir,
				   const char *cache_root,
				   const char *version,
				   struct local_module_list *list)
{
	DIR *d;
	struct dirent *ent;

	d = opendir(lib_dir);
	if (!d)
		return;

	while ((ent = readdir(d)) != NULL) {
		char *full_path;
		char *rel_path;
		char *build_obj;
		char *build_hei;
		char *cache_dir;
		char *name;
		char *pkg;
		const char *dot;

		if (ent->d_name[0] == '.')
			continue;

		full_path = hel_path_join(lib_dir, ent->d_name);
		if (rel_prefix && rel_prefix[0]) {
			rel_path = hel_path_join(rel_prefix, ent->d_name);
		} else {
			rel_path = hel_xstrdup(ent->d_name);
		}

		if (hel_is_dir(full_path)) {
			scan_lib_dir_recursive(full_path, rel_path, build_lib_dir,
					       cache_root, version, list);
			free(rel_path);
			free(full_path);
			continue;
		}

		dot = strrchr(ent->d_name, '.');
		if (!dot || strcmp(dot, ".hel") != 0) {
			free(rel_path);
			free(full_path);
			continue;
		}

		name = hel_xstrndup(ent->d_name, dot - ent->d_name);
		{
			const char *slash = strchr(rel_path, '/');

			pkg = slash ? hel_xstrndup(rel_path, slash - rel_path)
				    : hel_xstrdup(name);
		}

		build_obj = hel_path_join(build_lib_dir,
					  replace_extension(rel_path, ".o"));
		build_hei = hel_path_join(build_lib_dir,
					  replace_extension(rel_path, ".hei"));
		cache_dir = hel_path_join(cache_root, pkg);
		{
			char *tmp = cache_dir;

			cache_dir = hel_path_join(tmp, version);
			free(tmp);
		}

		local_module_list_add(list, full_path, rel_path, pkg, name,
				      build_obj, build_hei, cache_dir);

		free(name);
		free(pkg);
		free(build_obj);
		free(build_hei);
		free(cache_dir);
		free(rel_path);
		free(full_path);
	}
	closedir(d);
}

static int ensure_dir(const char *path)
{
	char *dir = hel_dirname(path);
	int rc = hel_mkdir_p(dir);

	free(dir);
	return rc;
}

static int install_local_module(const struct local_module *m, char **error)
{
	char *cache_hel;
	char *cache_hei;
	char *cache_obj;
	int rc = 0;

	if (hel_mkdir_p(m->cache_dir) != 0) {
		if (error) {
			*error = hel_xstrdup("cannot create cache directory");
		}
		return -1;
	}

	cache_hel = cache_file_path(m->cache_dir, m->name, ".hel");
	cache_hei = cache_file_path(m->cache_dir, m->name, ".hei");
	cache_obj = cache_file_path(m->cache_dir, m->name, ".o");

	if (hel_copy_file(m->source_path, cache_hel) != 0 ||
	    hel_copy_file(m->build_hei, cache_hei) != 0 ||
	    hel_copy_file_binary(m->build_obj, cache_obj) != 0) {
		if (error)
			*error = hel_xstrdup("failed to install local module");
		rc = -1;
	}

	free(cache_hel);
	free(cache_hei);
	free(cache_obj);
	return rc;
}

static int cmp_string(const void *a, const void *b);

/* -------------------------------------------------------------------------- */
/* Package C sources (csrc)                                                    */
/* -------------------------------------------------------------------------- */

static int read_dir_sorted(const char *path, struct cache_path_list *names)
{
	DIR *d;
	struct dirent *ent;

	d = opendir(path);
	if (!d)
		return -1;
	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		cache_path_list_add(names, hel_xstrdup(ent->d_name));
	}
	closedir(d);
	qsort(names->paths, names->count, sizeof(char *), cmp_string);
	return 0;
}

static void collect_c_sources(const char *csrc_dir, const char *rel_prefix,
			      struct cache_path_list *files)
{
	struct cache_path_list names = { NULL, 0, 0 };
	size_t i;

	if (read_dir_sorted(csrc_dir, &names) != 0)
		return;
	for (i = 0; i < names.count; i++) {
		char *full_path = hel_path_join(csrc_dir, names.paths[i]);
		char *rel_path;

		if (rel_prefix)
			rel_path = hel_path_join(rel_prefix, names.paths[i]);
		else
			rel_path = hel_xstrdup(names.paths[i]);

		if (hel_is_dir(full_path)) {
			collect_c_sources(full_path, rel_path, files);
			free(rel_path);
		} else {
			const char *dot = strrchr(names.paths[i], '.');

			if (dot && strcmp(dot, ".c") == 0)
				cache_path_list_add(files, rel_path);
			else
				free(rel_path);
		}
		free(full_path);
	}
	cache_path_list_free(&names);
}

static int compile_c_source(const char *root, const char *pkg,
			    const char *rel_source, char **obj_out,
			    char **error)
{
	char *obj_rel = replace_extension(rel_source, ".o");
	char *src = NULL;
	char *obj = NULL;
	char *obj_abs;
	char *include = NULL;
	char *argv[12];
	int rc = -1;

	if (asprintf(&src, "lib/%s/csrc/%s", pkg, rel_source) < 0 ||
	    asprintf(&obj, "build/lib/%s/csrc/%s", pkg, obj_rel) < 0 ||
	    asprintf(&include, "-Ilib/%s/csrc", pkg) < 0)
		abort();
	obj_abs = hel_path_join(root, obj);

	if (ensure_dir(obj_abs) != 0) {
		if (error)
			hel_xasprintf(error, "cannot create directory for %s", obj);
		goto out;
	}

	argv[0] = "cc";
	argv[1] = "-std=c11";
	argv[2] = "-O2";
	argv[3] = "-g";
	argv[4] = "-Wall";
	argv[5] = "-Wextra";
	argv[6] = include;
	argv[7] = "-c";
	argv[8] = src;
	argv[9] = "-o";
	argv[10] = obj;
	argv[11] = NULL;

	if (run_command(root, argv) != 0) {
		if (error)
			hel_xasprintf(error, "failed to compile %s", src);
		goto out;
	}

	*obj_out = obj;
	obj = NULL;
	rc = 0;
out:
	free(obj);
	free(obj_abs);
	free(include);
	free(src);
	free(obj_rel);
	return rc;
}

static int create_package_archive(const char *root, const char *pkg,
				  const struct cache_path_list *objects,
				  char **error)
{
	char *archive = NULL;
	char **argv;
	size_t i;
	int argc = 0;
	int rc = -1;

	if (asprintf(&archive, "build/lib/%s/lib%s.a", pkg, pkg) < 0)
		abort();
	argv = hel_xalloc((objects->count + 4) * sizeof(*argv));
	argv[argc++] = "ar";
	argv[argc++] = "rcs";
	argv[argc++] = archive;
	for (i = 0; i < objects->count; i++)
		argv[argc++] = objects->paths[i];
	argv[argc] = NULL;

	if (run_command(root, argv) != 0) {
		if (error)
			hel_xasprintf(error,
				      "failed to create archive lib%s.a", pkg);
		goto out;
	}
	rc = 0;
out:
	free(argv);
	free(archive);
	return rc;
}

static int install_package_archive(const char *root, const char *pkg,
				   const char *version, char **error)
{
	char *build_archive = NULL;
	char *cache_root = hel_path_join(root, ".helium");
	char *cache_dir = hel_path_join(cache_root, pkg);
	char *cache_archive = NULL;
	int rc = -1;

	{
		char *tmp = cache_dir;

		cache_dir = hel_path_join(tmp, version);
		free(tmp);
	}
	if (asprintf(&build_archive, "%s/build/lib/%s/lib%s.a",
		     root, pkg, pkg) < 0 ||
	    asprintf(&cache_archive, "%s/lib%s.a", cache_dir, pkg) < 0)
		abort();

	if (hel_mkdir_p(cache_dir) != 0 ||
	    hel_copy_file_binary(build_archive, cache_archive) != 0) {
		if (error)
			hel_xasprintf(error, "failed to install lib%s.a", pkg);
		goto out;
	}
	rc = 0;
out:
	free(cache_archive);
	free(build_archive);
	free(cache_dir);
	free(cache_root);
	return rc;
}

static int build_package_csrc(const char *root, const char *pkg,
			      const char *version, char **error)
{
	char *csrc_dir = NULL;
	struct cache_path_list sources = { NULL, 0, 0 };
	struct cache_path_list objects = { NULL, 0, 0 };
	size_t i;
	int rc = -1;

	if (asprintf(&csrc_dir, "%s/lib/%s/csrc", root, pkg) < 0)
		abort();
	if (!hel_is_dir(csrc_dir)) {
		rc = 0;
		goto out;
	}

	collect_c_sources(csrc_dir, NULL, &sources);
	if (sources.count == 0) {
		rc = 0;
		goto out;
	}

	for (i = 0; i < sources.count; i++) {
		char *obj = NULL;

		if (compile_c_source(root, pkg, sources.paths[i], &obj,
				     error) != 0)
			goto out;
		cache_path_list_add(&objects, obj);
	}

	if (create_package_archive(root, pkg, &objects, error) != 0)
		goto out;
	if (install_package_archive(root, pkg, version, error) != 0)
		goto out;
	rc = 1;
out:
	cache_path_list_free(&objects);
	cache_path_list_free(&sources);
	free(csrc_dir);
	return rc;
}

static int build_local_csrc(const char *root, const char *version, char **error)
{
	char *lib_dir = hel_path_join(root, "lib");
	struct cache_path_list entries = { NULL, 0, 0 };
	size_t i;
	int installed = 0;

	if (read_dir_sorted(lib_dir, &entries) != 0) {
		free(lib_dir);
		return 0;
	}
	for (i = 0; i < entries.count; i++) {
		char *pkg_dir = hel_path_join(lib_dir, entries.paths[i]);
		int rc = 0;

		if (hel_is_dir(pkg_dir))
			rc = build_package_csrc(root, entries.paths[i],
						version, error);
		free(pkg_dir);
		if (rc < 0) {
			installed = -1;
			break;
		}
		installed += rc;
	}
	cache_path_list_free(&entries);
	free(lib_dir);
	return installed;
}

static int compile_local_modules(const char *root, const char *version,
				 char **error)
{
	char *lib_dir = hel_path_join(root, "lib");
	char *build_lib_dir = hel_path_join(root, "build/lib");
	char *cache_root = hel_path_join(root, ".helium");
	struct local_module_list list = { NULL, 0, 0 };
	struct cache_path_list cache = { NULL, 0, 0 };
	const char **module_paths;
	size_t i;
	int rc = 0;
	int installed = 0;

	if (!hel_is_dir(lib_dir)) {
		free(lib_dir);
		free(build_lib_dir);
		free(cache_root);
		return 0;
	}

	scan_lib_dir_recursive(lib_dir, NULL, build_lib_dir, cache_root,
			       version, &list);

	/*
	 * Copy source stubs into .helium/ first so that later module
	 * compilations can resolve imports of other local libraries.
	 */
	for (i = 0; i < list.count; i++) {
		struct local_module *m = &list.modules[i];
		char *cache_hel = cache_file_path(m->cache_dir, m->name, ".hel");

		if (hel_mkdir_p(m->cache_dir) != 0 ||
		    hel_copy_file(m->source_path, cache_hel) != 0) {
			if (error)
				*error = hel_xstrdup("failed to stage local module");
			free(cache_hel);
			rc = -1;
			goto out;
		}
		free(cache_hel);
	}

	collect_cache_paths(cache_root, &cache);
	module_paths = build_module_paths(lib_dir, &cache);

	for (i = 0; i < list.count && rc == 0; i++) {
		struct local_module *m = &list.modules[i];
		struct helium_module_interface *iface = NULL;

		if (ensure_dir(m->build_obj) != 0 || ensure_dir(m->build_hei) != 0) {
			if (error)
				*error = hel_xstrdup("cannot create build/lib directory");
			rc = -1;
			break;
		}

		if (helium_compile_module(m->source_path, m->build_obj, m->build_hei,
					  module_paths, &iface, error) != 0) {
			rc = -1;
			break;
		}
		if (iface)
			helium_module_interface_free(iface);

		if (install_local_module(m, error) != 0) {
			rc = -1;
			break;
		}
		installed++;
	}

	if (rc == 0) {
		int csrc = build_local_csrc(root, version, error);

		if (csrc < 0)
			rc = -1;
		else
			installed += csrc;
	}

	free_module_paths(module_paths);
	cache_path_list_free(&cache);
out:
	local_module_list_free(&list);
	free(lib_dir);
	free(build_lib_dir);
	free(cache_root);
	return rc == 0 ? installed : -1;
}

static int run_compiler(const char *root, const char *src,
			const char *out)
{
	char *cc = compiler_path();
	char *repo = repo_root();
	char *cache_root = hel_path_join(root, ".helium");
	char *runtime_src = hel_path_join(repo, "src/runtime/helium_runtime.c");
	struct cache_path_list cache = { NULL, 0, 0 };
	const char **module_paths;
	struct helium_compile_options opts;
	char *extra_libs = NULL;
	char *error = NULL;
	size_t i;
	int rc;

	if (!hel_path_exists(cc)) {
		fprintf(stderr, "error: compiler not found: %s\n", cc);
		free(cc);
		free(repo);
		free(cache_root);
		free(runtime_src);
		return 1;
	}

	collect_cache_paths(cache_root, &cache);
	qsort(cache.paths, cache.count, sizeof(char *), cmp_string);
	module_paths = build_module_paths(NULL, &cache);

	/*
	 * Link every cached package archive (.helium/<pkg>/<version>/lib<pkg>.a)
	 * into the final binary.  The compiler appends the extra_libs tokens
	 * after the object files, which is the correct static-link order.
	 */
	for (i = 0; i < cache.count; i++) {
		char *parent = hel_dirname(cache.paths[i]);
		char *archive = NULL;

		if (asprintf(&archive, "%s/lib%s.a", cache.paths[i],
			     basename(parent)) < 0)
			abort();
		if (hel_path_exists(archive)) {
			if (extra_libs) {
				char *tmp = NULL;

				if (asprintf(&tmp, "%s %s", extra_libs, archive) < 0)
					abort();
				free(extra_libs);
				extra_libs = tmp;
			} else {
				extra_libs = hel_xstrdup(archive);
			}
		}
		free(archive);
		free(parent);
	}

	memset(&opts, 0, sizeof(opts));
	opts.output_path = out;
	opts.extra_libs = extra_libs;
	opts.module_paths = module_paths;
	opts.runtime_source_path = runtime_src;

	rc = helium_compile(src, &opts, &error);
	if (rc != 0) {
		fprintf(stderr, "error: %s\n", error ? error : "compilation failed");
		free(error);
	}
	free(extra_libs);
	free_module_paths(module_paths);
	cache_path_list_free(&cache);
	free(cc);
	free(repo);
	free(cache_root);
	free(runtime_src);
	return rc;
}

static int build_project(const char *root, const struct hel_manifest *m,
			 const struct hel_lock *l)
{
	char *src = hel_path_join(root, "src/main.hel");
	char *build_dir = hel_path_join(root, "build");
	char *out = hel_path_join(build_dir, m->name);
	char *error = NULL;
	int installed;
	int rc;

	(void)l;
	if (hel_mkdir_p(build_dir) != 0) {
		fprintf(stderr, "error: cannot create build directory\n");
		free(src);
		free(build_dir);
		free(out);
		return 1;
	}
	installed = compile_local_modules(root, m->version, &error);
	if (installed < 0) {
		fprintf(stderr, "error: %s\n", error ? error : "failed to compile local modules");
		free(error);
		free(src);
		free(build_dir);
		free(out);
		return 1;
	}
	if (!hel_path_exists(src)) {
		/*
		 * Library mode: without src/main.hel there is no binary to
		 * link, but any installed module or archive is a successful
		 * library build.
		 */
		if (installed > 0) {
			printf("Built libraries\n");
			fflush(stdout);
			rc = 0;
		} else {
			fprintf(stderr, "error: src/main.hel not found\n");
			rc = 1;
		}
		free(src);
		free(build_dir);
		free(out);
		return rc;
	}
	rc = run_compiler(root, src, out);
	if (rc == 0) {
		printf("Built %s\n", out);
		fflush(stdout);
	}
	free(src);
	free(build_dir);
	free(out);
	return rc;
}

static int verify_lock(const struct hel_manifest *m, const struct hel_lock *l)
{
	size_t i;

	for (i = 0; i < m->dep_count; i++) {
		const struct hel_dep *d = &m->deps[i];
		const struct hel_lock_entry *e = hel_lock_find_dep(l, d->name);

		if (!e) {
			fprintf(stderr,
				"error: lock file is missing dependency '%s' (run '%s update')\n",
				d->name, "hel");
			return -1;
		}
		if (strcmp(e->version, d->version) != 0) {
			fprintf(stderr,
				"error: lock file version mismatch for '%s' (run '%s update')\n",
				d->name, "hel");
			return -1;
		}
	}
	return 0;
}

static int verify_cache(const char *root, const struct hel_manifest *m)
{
	size_t i;
	char *cache_root = hel_path_join(root, ".helium");

	for (i = 0; i < m->dep_count; i++) {
		const struct hel_dep *d = &m->deps[i];
		char *dir = hel_path_join(cache_root, d->name);
		char *ver = hel_path_join(dir, d->version);

		if (!hel_is_dir(ver)) {
			fprintf(stderr,
				"error: dependency not found in local cache: %s@%s\n",
				d->name, d->version);
			fprintf(stderr,
				"       expected %s\n", ver);
			free(dir);
			free(ver);
			free(cache_root);
			return -1;
		}
		free(dir);
		free(ver);
	}
	free(cache_root);
	return 0;
}

static int cmp_string(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static char *latest_cached_version(const char *root, const char *name)
{
	char *cache_root = hel_path_join(root, ".helium");
	char *pkg_dir = hel_path_join(cache_root, name);
	DIR *d;
	struct dirent *ent;
	char **versions = NULL;
	size_t count = 0;
	size_t cap = 0;
	char *latest = NULL;

	d = opendir(pkg_dir);
	if (!d)
		goto out;
	while ((ent = readdir(d)) != NULL) {
		char *path;

		if (ent->d_name[0] == '.')
			continue;
		path = hel_path_join(pkg_dir, ent->d_name);
		if (!hel_is_dir(path)) {
			free(path);
			continue;
		}
		free(path);
		if (count == cap) {
			size_t newcap = cap ? cap * 2 : 4;

			versions = hel_xrealloc(versions,
						newcap * sizeof(char *));
			cap = newcap;
		}
		versions[count++] = hel_xstrdup(ent->d_name);
	}
	closedir(d);
	if (count == 0)
		goto out;
	qsort(versions, count, sizeof(char *), cmp_string);
	latest = hel_xstrdup(versions[count - 1]);
out:
	while (count > 0)
		free(versions[--count]);
	free(versions);
	free(pkg_dir);
	free(cache_root);
	return latest;
}

static unsigned long fnv1a_64(const void *data, size_t len)
{
	const unsigned char *p = data;
	unsigned long h = 0xcbf29ce484222325ULL;
	size_t i;

	for (i = 0; i < len; i++) {
		h ^= p[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

static unsigned long checksum_file(const char *path)
{
	char *data = hel_read_file(path);
	unsigned long h;

	if (!data)
		return 0;
	h = fnv1a_64(data, strlen(data));
	free(data);
	return h;
}

static char *checksum_interface_dir(const char *dir)
{
	char **names = NULL;
	size_t count = 0;
	size_t cap = 0;
	DIR *d;
	struct dirent *ent;
	unsigned long h = 0xcbf29ce484222325ULL;
	char *out;
	size_t i;

	d = opendir(dir);
	if (!d)
		return hel_xstrdup("");
	while ((ent = readdir(d)) != NULL) {
		char *path;
		struct stat st;

		if (ent->d_name[0] == '.')
			continue;
		path = hel_path_join(dir, ent->d_name);
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
			free(path);
			continue;
		}
		free(path);
		if (count == cap) {
			size_t newcap = cap ? cap * 2 : 4;

			names = hel_xrealloc(names, newcap * sizeof(char *));
			cap = newcap;
		}
		names[count++] = hel_xstrdup(ent->d_name);
	}
	closedir(d);
	qsort(names, count, sizeof(char *), cmp_string);
	for (i = 0; i < count; i++) {
		char *path = hel_path_join(dir, names[i]);
		unsigned long fh = checksum_file(path);

		h ^= fh;
		h *= 0x100000001b3ULL;
		free(path);
	}
	while (count > 0)
		free(names[--count]);
	free(names);
	out = hel_xalloc(32);
	snprintf(out, 32, "%016lx", h);
	return out;
}

static char *compute_checksum(const char *root, const char *name,
			      const char *version)
{
	char *cache_root = hel_path_join(root, ".helium");
	char *pkg_dir = hel_path_join(cache_root, name);
	char *ver_dir = hel_path_join(pkg_dir, version);
	char *iface_dir = hel_path_join(ver_dir, "interface");
	char *sum = checksum_interface_dir(iface_dir);

	free(iface_dir);
	free(ver_dir);
	free(pkg_dir);
	free(cache_root);
	return sum;
}

static int cmd_build(void)
{
	char *root = project_root();
	char *manifest_path = hel_path_join(root, MANIFEST_NAME);
	char *lock_path = hel_path_join(root, LOCK_NAME);
	char *error = NULL;
	struct hel_manifest *m;
	struct hel_lock *l;
	int rc;

	m = hel_manifest_read(manifest_path, &error);
	if (!m) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot read manifest");
		free(error);
		free(manifest_path);
		free(lock_path);
		free(root);
		return 1;
	}
	l = hel_lock_read(lock_path, &error);
	if (!l) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot read lock file");
		hel_manifest_free(m);
		free(error);
		free(manifest_path);
		free(lock_path);
		free(root);
		return 1;
	}
	if (verify_lock(m, l) != 0) {
		rc = 1;
		goto out;
	}
	if (verify_cache(root, m) != 0) {
		rc = 1;
		goto out;
	}
	rc = build_project(root, m, l);
out:
	hel_manifest_free(m);
	hel_lock_free(l);
	free(manifest_path);
	free(lock_path);
	free(root);
	return rc;
}

static int cmd_run(int argc, char *argv[], int idx)
{
	char *root = project_root();
	char *manifest_path = hel_path_join(root, MANIFEST_NAME);
	char *error = NULL;
	struct hel_manifest *m;
	char *binary;
	char **run_argv;
	int run_argc;
	int i;
	int rc;

	rc = cmd_build();
	if (rc != 0) {
		free(root);
		free(manifest_path);
		return rc;
	}
	m = hel_manifest_read(manifest_path, &error);
	if (!m) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot read manifest");
		free(error);
		free(manifest_path);
		free(root);
		return 1;
	}
	binary = hel_path_join(root, "build");
	{
		char *tmp = binary;

		binary = hel_path_join(tmp, m->name);
		free(tmp);
	}
	if (!hel_path_exists(binary)) {
		fprintf(stderr, "error: binary not found: %s\n", binary);
		rc = 1;
		goto out;
	}

	run_argc = 1 + (argc - idx);
	run_argv = hel_xalloc((run_argc + 1) * sizeof(*run_argv));
	run_argv[0] = binary;
	for (i = idx; i < argc; i++)
		run_argv[1 + (i - idx)] = argv[i];
	run_argv[run_argc] = NULL;

	rc = run_command(root, run_argv);
	free(run_argv);
out:
	hel_manifest_free(m);
	free(binary);
	free(manifest_path);
	free(root);
	return rc;
}

static int cmd_test(void)
{
	char *root = project_root();
	char *repo = repo_root();
	char *script = hel_path_join(repo, "tests/run_tests.py");
	char *cc = compiler_path();
	char *argv[] = {
		"python3", script, "--compiler", cc,
		"--project", root, NULL
	};
	int rc;

	rc = cmd_build();
	if (rc != 0) {
		free(cc);
		free(script);
		free(repo);
		free(root);
		return rc;
	}
	if (!hel_path_exists(script)) {
		fprintf(stderr, "error: test harness not found: %s\n", script);
		free(cc);
		free(script);
		free(repo);
		free(root);
		return 1;
	}
	rc = run_command(root, argv);
	free(cc);
	free(script);
	free(repo);
	free(root);
	return rc;
}

static int write_default_file(const char *root, const char *subpath,
			      const char *contents)
{
	char *path = hel_path_join(root, subpath);
	int rc = hel_write_file(path, contents);

	free(path);
	return rc;
}

static int mkdir_and_free(char *path)
{
	int rc = hel_mkdir_p(path);

	free(path);
	return rc;
}

static int cmd_init(int argc, char *argv[], int *idx)
{
	const char *name_arg = NULL;
	char *root;
	char *name_raw;
	char *name;
	char manifest[1024];
	char lock[1024];
	struct hel_manifest *m;
	struct hel_lock *l;
	int rc = 0;

	if (*idx < argc)
		name_arg = argv[(*idx)++];

	if (name_arg) {
		root = hel_xstrdup(name_arg);
	} else {
		root = project_root();
	}
	name_raw = basename(root);
	name = hel_xstrdup(name_raw);

	if (hel_path_exists(root) && !hel_is_dir(root)) {
		fprintf(stderr, "error: %s exists and is not a directory\n", root);
		free(name);
		free(root);
		return 1;
	}
	if (hel_mkdir_p(root) != 0) {
		fprintf(stderr, "error: cannot create project directory %s\n", root);
		free(name);
		free(root);
		return 1;
	}
	{
		char *manifest_path = hel_path_join(root, MANIFEST_NAME);

		if (hel_path_exists(manifest_path)) {
			fprintf(stderr, "error: project already exists at %s\n", root);
			free(manifest_path);
			free(name);
			free(root);
			return 1;
		}
		free(manifest_path);
	}

	if (mkdir_and_free(hel_path_join(root, "build")) != 0 ||
	    mkdir_and_free(hel_path_join(root, "lib")) != 0 ||
	    mkdir_and_free(hel_path_join(root, "src")) != 0 ||
	    mkdir_and_free(hel_path_join(root, "tests")) != 0 ||
	    mkdir_and_free(hel_path_join(root, ".helium")) != 0) {
		fprintf(stderr, "error: cannot create project directories\n");
		free(name);
		free(root);
		return 1;
	}

	write_default_file(root, "src/main.hel",
			   "foreign io_unit: IO<()>;\n\nmain: IO<()> = io_unit;\n");
	write_default_file(root, "lib/math.hel",
			   "module math;\n\nadd = (a: i32, b: i32): i32 { a + b };\n");
	write_default_file(root, "tests/smoke_test.hel",
			   "foreign io_unit: IO<()>;\n\nmain: IO<()> = io_unit;\n");
	write_default_file(root, ".env", "# Local development environment variables.\n");

	snprintf(manifest, sizeof(manifest),
		 "[package]\nname = \"%s\"\nversion = \"%s\"\nedition = \"%s\"\n\n[dependencies]\n",
		 name, DEFAULT_VERSION, DEFAULT_EDITION);
	snprintf(lock, sizeof(lock),
		 "[package]\nname = \"%s\"\nversion = \"%s\"\nedition = \"%s\"\n",
		 name, DEFAULT_VERSION, DEFAULT_EDITION);

	write_default_file(root, MANIFEST_NAME, manifest);
	write_default_file(root, LOCK_NAME, lock);

	m = hel_manifest_read(hel_path_join(root, MANIFEST_NAME), NULL);
	l = hel_lock_read(hel_path_join(root, LOCK_NAME), NULL);
	if (m)
		hel_manifest_free(m);
	if (l)
		hel_lock_free(l);

	printf("Created project at %s\n", root);
	free(name);
	free(root);
	return rc;
}

static int parse_pkg_spec(const char *spec, char **name, char **version)
{
	const char *at = strchr(spec, '@');

	if (at) {
		*name = hel_xstrndup(spec, at - spec);
		*version = hel_xstrdup(at + 1);
	} else {
		*name = hel_xstrdup(spec);
		*version = NULL;
	}
	if (strlen(*name) == 0) {
		free(*name);
		free(*version);
		return -1;
	}
	return 0;
}

static int cmd_add(const char *spec)
{
	char *root = project_root();
	char *manifest_path = hel_path_join(root, MANIFEST_NAME);
	char *lock_path = hel_path_join(root, LOCK_NAME);
	char *error = NULL;
	struct hel_manifest *m;
	struct hel_lock *l;
	char *name = NULL;
	char *version = NULL;
	char *resolved = NULL;
	char *checksum = NULL;
	int rc = 1;

	if (parse_pkg_spec(spec, &name, &version) < 0) {
		fprintf(stderr, "error: invalid package spec '%s'\n", spec);
		goto out;
	}
	m = hel_manifest_read(manifest_path, &error);
	if (!m) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot read manifest");
		free(error);
		goto out_name;
	}
	l = hel_lock_read(lock_path, &error);
	if (!l) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot read lock file");
		hel_manifest_free(m);
		free(error);
		goto out_name;
	}

	if (version)
		resolved = hel_xstrdup(version);
	else
		resolved = latest_cached_version(root, name);
	if (!resolved)
		resolved = hel_xstrdup(DEFAULT_VERSION);

	hel_manifest_add_dep(m, name, resolved, NULL);
	checksum = compute_checksum(root, name, resolved);
	hel_lock_add_dep(l, name, resolved, checksum);

	if (hel_manifest_write(m, manifest_path, &error) != 0) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot write manifest");
		free(error);
		goto out_all;
	}
	if (hel_lock_write(l, lock_path, &error) != 0) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot write lock file");
		free(error);
		goto out_all;
	}
	printf("Added %s@%s\n", name, resolved);
	rc = 0;
out_all:
	hel_manifest_free(m);
	hel_lock_free(l);
	free(checksum);
	free(resolved);
out_name:
	free(name);
	free(version);
out:
	free(manifest_path);
	free(lock_path);
	free(root);
	return rc;
}

static int cmd_remove(const char *name)
{
	char *root = project_root();
	char *manifest_path = hel_path_join(root, MANIFEST_NAME);
	char *lock_path = hel_path_join(root, LOCK_NAME);
	char *error = NULL;
	struct hel_manifest *m;
	struct hel_lock *l;
	int rc = 1;

	m = hel_manifest_read(manifest_path, &error);
	if (!m) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot read manifest");
		free(error);
		goto out;
	}
	l = hel_lock_read(lock_path, &error);
	if (!l) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot read lock file");
		hel_manifest_free(m);
		free(error);
		goto out;
	}
	if (!hel_manifest_find_dep(m, name)) {
		fprintf(stderr, "error: dependency '%s' not found in manifest\n", name);
		goto out_all;
	}
	hel_manifest_remove_dep(m, name);
	hel_lock_remove_dep(l, name);

	if (hel_manifest_write(m, manifest_path, &error) != 0) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot write manifest");
		free(error);
		goto out_all;
	}
	if (hel_lock_write(l, lock_path, &error) != 0) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot write lock file");
		free(error);
		goto out_all;
	}
	printf("Removed %s\n", name);
	rc = 0;
out_all:
	hel_manifest_free(m);
	hel_lock_free(l);
out:
	free(manifest_path);
	free(lock_path);
	free(root);
	return rc;
}

static int cmd_update(int argc, char *argv[], int *idx)
{
	const char *pkg = NULL;
	char *root = project_root();
	char *manifest_path = hel_path_join(root, MANIFEST_NAME);
	char *lock_path = hel_path_join(root, LOCK_NAME);
	char *error = NULL;
	struct hel_manifest *m;
	struct hel_lock *l;
	size_t i;
	int rc = 1;

	if (*idx < argc)
		pkg = argv[(*idx)++];

	m = hel_manifest_read(manifest_path, &error);
	if (!m) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot read manifest");
		free(error);
		goto out;
	}
	l = hel_lock_read(lock_path, &error);
	if (!l) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot read lock file");
		hel_manifest_free(m);
		free(error);
		goto out;
	}

	for (i = 0; i < m->dep_count; i++) {
		struct hel_dep *d = &m->deps[i];
		char *latest;
		char *checksum;

		if (pkg && strcmp(d->name, pkg) != 0)
			continue;
		latest = latest_cached_version(root, d->name);
		if (latest) {
			free(d->version);
			d->version = latest;
		}
		checksum = compute_checksum(root, d->name, d->version);
		hel_lock_add_dep(l, d->name, d->version, checksum);
		free(checksum);
	}

	if (pkg && !hel_manifest_find_dep(m, pkg)) {
		fprintf(stderr, "error: dependency '%s' not found in manifest\n", pkg);
		goto out_all;
	}

	if (hel_manifest_write(m, manifest_path, &error) != 0) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot write manifest");
		free(error);
		goto out_all;
	}
	if (hel_lock_write(l, lock_path, &error) != 0) {
		fprintf(stderr, "error: %s\n", error ? error : "cannot write lock file");
		free(error);
		goto out_all;
	}
	printf("Updated lock file\n");
	rc = 0;
out_all:
	hel_manifest_free(m);
	hel_lock_free(l);
out:
	free(manifest_path);
	free(lock_path);
	free(root);
	return rc;
}

int main(int argc, char *argv[])
{
	const char *cmd;
	int idx = 1;

	if (idx >= argc) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	cmd = argv[idx++];

	if (strcmp(cmd, "init") == 0)
		return cmd_init(argc, argv, &idx);
	if (strcmp(cmd, "build") == 0)
		return cmd_build();
	if (strcmp(cmd, "run") == 0)
		return cmd_run(argc, argv, idx);
	if (strcmp(cmd, "test") == 0)
		return cmd_test();
	if (strcmp(cmd, "add") == 0) {
		if (idx >= argc) {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
		return cmd_add(argv[idx]);
	}
	if (strcmp(cmd, "remove") == 0) {
		if (idx >= argc) {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
		return cmd_remove(argv[idx]);
	}
	if (strcmp(cmd, "update") == 0)
		return cmd_update(argc, argv, &idx);

	fprintf(stderr, "error: unknown command '%s'\n", cmd);
	usage(argv[0]);
	return EXIT_FAILURE;
}
