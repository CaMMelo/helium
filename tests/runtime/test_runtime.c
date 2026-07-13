/* SPDX-License-Identifier: TBD */
/*
 * test_runtime.c - unit tests for the Helium reference-counting runtime.
 */

#include "helium_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static int g_checks_failed;
static int g_fields_destroyed;
static int g_env_destroyed;
static int g_elem_destroyed;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		g_checks_failed++; \
	} \
} while (0)

static void
counting_fields_destroy(helium_object_t *obj)
{
	(void)obj;
	g_fields_destroyed++;
}

static void
counting_env_destroy(void *env)
{
	(void)env;
	g_env_destroyed++;
}

static void
counting_elem_destroy(void *elem)
{
	(void)elem;
	g_elem_destroyed++;
}

static void
release_env(void *env)
{
	helium_release((helium_object_t *)env);
}

/*
 * Good cases: normal retain/release and allocation behavior.
 */

static void
test_string_alloc_and_release(void)
{
	helium_string_t *s;

	s = helium_alloc_string(5);
	CHECK(s != NULL);
	CHECK(s->header.refcount == 1);
	CHECK(s->header.kind == HELIUM_KIND_STRING);
	CHECK(s->length == 5);
	CHECK(s->data[5] == '\0');
	helium_release(&s->header);
}

static void
test_retain_release_counts(void)
{
	helium_string_t *s;

	s = helium_alloc_string(1);
	CHECK(s->header.refcount == 1);
	helium_retain(&s->header);
	CHECK(s->header.refcount == 2);
	helium_retain(&s->header);
	CHECK(s->header.refcount == 3);
	helium_release(&s->header);
	CHECK(s->header.refcount == 2);
	helium_release(&s->header);
	CHECK(s->header.refcount == 1);
	helium_release(&s->header);
}

static void
test_array_alloc_and_element_destroy(void)
{
	helium_array_t *arr;

	g_elem_destroyed = 0;
	arr = helium_alloc_array(3, sizeof(int64_t), counting_elem_destroy);
	CHECK(arr != NULL);
	CHECK(arr->header.refcount == 1);
	CHECK(arr->header.kind == HELIUM_KIND_ARRAY);
	CHECK(arr->length == 3);
	CHECK(arr->elem_size == sizeof(int64_t));
	CHECK(arr->elem_destroy == counting_elem_destroy);
	helium_release(&arr->header);
	CHECK(g_elem_destroyed == 3);
}

static void
test_record_alloc_and_destroy(void)
{
	helium_record_t *rec;

	g_fields_destroyed = 0;
	rec = helium_alloc_record(16, counting_fields_destroy);
	CHECK(rec != NULL);
	CHECK(rec->header.refcount == 1);
	CHECK(rec->header.kind == HELIUM_KIND_RECORD);
	CHECK(rec->fields_destroy == counting_fields_destroy);
	helium_release(&rec->header);
	CHECK(g_fields_destroyed == 1);
}

static void
test_adt_alloc_and_destroy(void)
{
	helium_adt_t *adt;

	g_fields_destroyed = 0;
	adt = helium_alloc_adt(7, 8, counting_fields_destroy);
	CHECK(adt != NULL);
	CHECK(adt->header.refcount == 1);
	CHECK(adt->header.kind == HELIUM_KIND_ADT);
	CHECK(adt->variant == 7);
	CHECK(adt->fields_destroy == counting_fields_destroy);
	helium_release(&adt->header);
	CHECK(g_fields_destroyed == 1);
}

static void
test_closure_alloc_and_destroy(void)
{
	helium_closure_t *clo;

	g_env_destroyed = 0;
	clo = helium_alloc_closure(NULL, NULL, counting_env_destroy);
	CHECK(clo != NULL);
	CHECK(clo->header.refcount == 1);
	CHECK(clo->header.kind == HELIUM_KIND_CLOSURE);
	CHECK(clo->fn == NULL);
	CHECK(clo->env == NULL);
	CHECK(clo->env_destroy == counting_env_destroy);
	helium_release(&clo->header);
	CHECK(g_env_destroyed == 1);
}

static void
test_closure_releases_environment(void)
{
	helium_string_t *env;
	helium_closure_t *clo;

	env = helium_alloc_string(4);
	clo = helium_alloc_closure(NULL, env, release_env);
	CHECK(clo != NULL);
	CHECK(env->header.refcount == 1);
	helium_release(&clo->header);
}

static int64_t
main_returns_42(void)
{
	return 42;
}

static void
test_main_wrapper(void)
{
	pid_t pid;
	int status;

	pid = fork();
	CHECK(pid >= 0);

	if (pid == 0) {
		helium_main_wrapper(main_returns_42, 0, NULL, 0);
		_exit(99); /* unreachable if wrapper exits */
	}

	waitpid(pid, &status, 0);
	CHECK(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 42);
}

/*
 * Bad / edge cases: NULL pointers, empty allocations, missing destructors.
 */

static void
test_null_retain_release(void)
{
	/* These must be no-ops and must not crash. */
	helium_retain(NULL);
	helium_release(NULL);
	CHECK(1);
}

static void
test_array_no_element_destroy(void)
{
	helium_array_t *arr;

	arr = helium_alloc_array(2, sizeof(int64_t), NULL);
	CHECK(arr != NULL);
	CHECK(arr->elem_destroy == NULL);
	helium_release(&arr->header);
}

static void
test_empty_string(void)
{
	helium_string_t *s;

	s = helium_alloc_string(0);
	CHECK(s != NULL);
	CHECK(s->length == 0);
	CHECK(s->data[0] == '\0');
	helium_release(&s->header);
}

static void
test_zero_length_array(void)
{
	helium_array_t *arr;

	arr = helium_alloc_array(0, sizeof(int64_t), counting_elem_destroy);
	CHECK(arr != NULL);
	CHECK(arr->length == 0);
	helium_release(&arr->header);
}

static void
test_zero_size_record(void)
{
	helium_record_t *rec;

	g_fields_destroyed = 0;
	rec = helium_alloc_record(0, counting_fields_destroy);
	CHECK(rec != NULL);
	helium_release(&rec->header);
	CHECK(g_fields_destroyed == 1);
}

static void
release_string_elem(void *elem)
{
	helium_string_t **sp = elem;

	helium_release(&(*sp)->header);
}

static void
test_array_of_strings(void)
{
	helium_array_t *arr;
	helium_string_t *s0;
	helium_string_t *s1;
	helium_string_t *s2;

	s0 = helium_alloc_string(3);
	s1 = helium_alloc_string(3);
	s2 = helium_alloc_string(3);

	arr = helium_alloc_array(3, sizeof(helium_string_t *),
				 release_string_elem);
	CHECK(arr != NULL);
	memcpy(arr->data, &s0, sizeof(s0));
	memcpy(arr->data + sizeof(s0), &s1, sizeof(s1));
	memcpy(arr->data + 2 * sizeof(s0), &s2, sizeof(s2));
	helium_release(&arr->header);
}

static void
destroy_record_with_string_field(helium_object_t *obj)
{
	helium_record_t *rec = (helium_record_t *)obj;
	helium_string_t **sp = (helium_string_t **)rec->data;

	helium_release(&(*sp)->header);
}

static void
test_record_releases_heap_field(void)
{
	helium_record_t *rec;
	helium_string_t *s;

	s = helium_alloc_string(4);
	rec = helium_alloc_record(sizeof(helium_string_t *),
				  destroy_record_with_string_field);
	CHECK(rec != NULL);
	memcpy(rec->data, &s, sizeof(s));
	helium_release(&rec->header);
}

int
main(void)
{
	printf("Running runtime tests...\n");
	fflush(stdout);

	test_string_alloc_and_release();
	test_retain_release_counts();
	test_array_alloc_and_element_destroy();
	test_record_alloc_and_destroy();
	test_adt_alloc_and_destroy();
	test_closure_alloc_and_destroy();
	test_closure_releases_environment();
	test_main_wrapper();

	test_null_retain_release();
	test_array_no_element_destroy();
	test_empty_string();
	test_zero_length_array();
	test_zero_size_record();
	test_array_of_strings();
	test_record_releases_heap_field();

	if (g_checks_failed) {
		printf("FAILED: %d check(s)\n", g_checks_failed);
		return EXIT_FAILURE;
	}

	printf("All runtime tests passed.\n");
	return EXIT_SUCCESS;
}
