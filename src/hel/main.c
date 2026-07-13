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

static int run_compiler(const char *root, const char *src,
			const char *out)
{
	char *cc = compiler_path();
	char *repo = repo_root();

	(void)root;
	char *argv[] = { cc, (char *)src, "-o", (char *)out, NULL };
	int rc;

	if (!hel_path_exists(cc)) {
		fprintf(stderr, "error: compiler not found: %s\n", cc);
		free(cc);
		free(repo);
		return 1;
	}
	rc = run_command(repo, argv);
	free(cc);
	free(repo);
	return rc;
}

static void ensure_src_symlink(const char *root, const char *src_dir,
			       const char *name)
{
	char *target = hel_path_join(root, name);
	char *link_path = hel_path_join(src_dir, name);

	if (hel_path_exists(link_path))
		goto out;
	if (symlink(target, link_path) != 0 && errno != EEXIST) {
		/* Non-fatal: compiler may not need this file. */
	}
out:
	free(target);
	free(link_path);
}

static void ensure_build_symlinks(const char *root, const char *src_dir)
{
	ensure_src_symlink(root, src_dir, MANIFEST_NAME);
	ensure_src_symlink(root, src_dir, LOCK_NAME);
	ensure_src_symlink(root, src_dir, ".helium");
}

static int build_project(const char *root, const struct hel_manifest *m,
			 const struct hel_lock *l)
{
	char *src = hel_path_join(root, "src/main.hel");

	(void)l;
	char *build_dir = hel_path_join(root, "build");
	char *out = hel_path_join(build_dir, m->name);
	int rc;

	if (!hel_path_exists(src)) {
		fprintf(stderr, "error: src/main.hel not found\n");
		free(src);
		free(build_dir);
		free(out);
		return 1;
	}
	if (hel_mkdir_p(build_dir) != 0) {
		fprintf(stderr, "error: cannot create build directory\n");
		free(src);
		free(build_dir);
		free(out);
		return 1;
	}
	ensure_build_symlinks(root, "src");
	rc = run_compiler(root, src, out);
	if (rc == 0)
		printf("Built %s\n", out);
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

static int cmd_run(void)
{
	char *root = project_root();
	char *manifest_path = hel_path_join(root, MANIFEST_NAME);
	char *error = NULL;
	struct hel_manifest *m;
	char *binary;
	char *argv[2];
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
	argv[0] = binary;
	argv[1] = NULL;
	rc = run_command(root, argv);
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
	char *script = hel_path_join(root, "tests/run_tests.py");
	char *cc = compiler_path();
	char *argv[] = { "python3", script, "--compiler", cc, NULL };
	int rc;

	rc = cmd_build();
	if (rc != 0) {
		free(cc);
		free(script);
		free(root);
		return rc;
	}
	if (!hel_path_exists(script)) {
		fprintf(stderr, "error: test harness not found: tests/run_tests.py\n");
		free(cc);
		free(script);
		free(root);
		return 1;
	}
	rc = run_command(root, argv);
	free(cc);
	free(script);
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
			   "module math;\n\n/* Local module skeleton. */\n");
	write_default_file(root, "tests/smoke_test.hel",
			   "foreign io_unit: IO<()>;\n\nmain: IO<()> = io_unit;\n");
	write_default_file(root, ".env", "# Local development environment variables.\n");
	{
		char *repo = repo_root();
		char *harness_src = hel_path_join(repo, "tests/run_tests.py");
		char *harness_dst = hel_path_join(root, "tests/run_tests.py");

		if (hel_path_exists(harness_src))
			hel_copy_file(harness_src, harness_dst);
		free(harness_src);
		free(harness_dst);
		free(repo);
	}

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
		return cmd_run();
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
