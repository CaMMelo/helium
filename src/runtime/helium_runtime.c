/* SPDX-License-Identifier: TBD */
/*
 * helium_runtime.c - reference-counting runtime for Helium.
 */

#include "helium_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
helium_retain(helium_object_t *obj)
{
	if (obj)
		obj->refcount++;
}

void
helium_release(helium_object_t *obj)
{
	if (!obj)
		return;

	if (--obj->refcount == 0 && obj->destroy)
		obj->destroy(obj);
}

void
helium_destroy_string(helium_object_t *obj)
{
	free(obj);
}

void
helium_destroy_array(helium_object_t *obj)
{
	helium_array_t *arr = (helium_array_t *)obj;
	size_t i;

	if (arr->elem_destroy) {
		for (i = 0; i < arr->length; i++) {
			uint8_t *elem = &arr->data[i * arr->elem_size];

			arr->elem_destroy(elem);
		}
	}

	free(arr);
}

void
helium_destroy_record(helium_object_t *obj)
{
	helium_record_t *rec = (helium_record_t *)obj;

	if (rec->fields_destroy)
		rec->fields_destroy(obj);

	free(rec);
}

void
helium_destroy_adt(helium_object_t *obj)
{
	helium_adt_t *adt = (helium_adt_t *)obj;

	if (adt->fields_destroy)
		adt->fields_destroy(obj);

	free(adt);
}

void
helium_destroy_closure(helium_object_t *obj)
{
	helium_closure_t *clo = (helium_closure_t *)obj;

	if (clo->env_destroy)
		clo->env_destroy(clo->env);

	free(clo);
}

helium_string_t *
helium_alloc_string(size_t length)
{
	helium_string_t *str;

	str = malloc(sizeof(*str) + length + 1);
	if (!str)
		abort();

	str->header.refcount = 1;
	str->header.kind = HELIUM_KIND_STRING;
	str->header.destroy = helium_destroy_string;
	str->length = length;
	str->data[length] = '\0';

	return str;
}

helium_array_t *
helium_alloc_array(size_t length, size_t elem_size,
		   helium_array_elem_destroy_fn elem_destroy)
{
	helium_array_t *arr;

	arr = malloc(sizeof(*arr) + length * elem_size);
	if (!arr)
		abort();

	arr->header.refcount = 1;
	arr->header.kind = HELIUM_KIND_ARRAY;
	arr->header.destroy = helium_destroy_array;
	arr->length = length;
	arr->elem_size = elem_size;
	arr->elem_destroy = elem_destroy;

	if (length > 0 && elem_size > 0)
		memset(arr->data, 0, length * elem_size);

	return arr;
}

helium_record_t *
helium_alloc_record(size_t size, helium_destroy_fn fields_destroy)
{
	helium_record_t *rec;

	rec = malloc(sizeof(*rec) + size);
	if (!rec)
		abort();

	rec->header.refcount = 1;
	rec->header.kind = HELIUM_KIND_RECORD;
	rec->header.destroy = helium_destroy_record;
	rec->fields_destroy = fields_destroy;

	if (size > 0)
		memset(rec->data, 0, size);

	return rec;
}

helium_adt_t *
helium_alloc_adt(size_t variant, size_t size, helium_destroy_fn fields_destroy)
{
	helium_adt_t *adt;

	adt = malloc(sizeof(*adt) + size);
	if (!adt)
		abort();

	adt->header.refcount = 1;
	adt->header.kind = HELIUM_KIND_ADT;
	adt->header.destroy = helium_destroy_adt;
	adt->variant = variant;
	adt->fields_destroy = fields_destroy;

	if (size > 0)
		memset(adt->data, 0, size);

	return adt;
}

helium_closure_t *
helium_alloc_closure(helium_generic_fn_t fn, void *env,
		     void (*env_destroy)(void *env))
{
	helium_closure_t *clo;

	clo = malloc(sizeof(*clo));
	if (!clo)
		abort();

	clo->header.refcount = 1;
	clo->header.kind = HELIUM_KIND_CLOSURE;
	clo->header.destroy = helium_destroy_closure;
	clo->fn = fn;
	clo->env = env;
	clo->env_destroy = env_destroy;

	return clo;
}

/*
 * Bootstrap std.io helpers.  These are temporary foreign functions used by
 * tests while the standard library is not yet implemented.
 */
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
io_printi(int32_t n)
{
	printf("%d", n);
	return 0;
}

char *
helium_format_i32(int32_t n)
{
	char *buf = malloc(16);

	if (!buf)
		abort();
	snprintf(buf, 16, "%d", n);
	return buf;
}

char *
helium_format_i64(int64_t n)
{
	char *buf = malloc(32);

	if (!buf)
		abort();
	snprintf(buf, 32, "%ld", (long)n);
	return buf;
}

char *
helium_format_bool(int8_t b)
{
	char *buf = malloc(6);

	if (!buf)
		abort();
	snprintf(buf, 6, "%s", b ? "true" : "false");
	return buf;
}

char *
helium_format_f64(double n)
{
	char *buf = malloc(64);

	if (!buf)
		abort();
	snprintf(buf, 64, "%g", n);
	return buf;
}

void
helium_main_wrapper(int64_t (*main_fn)(void))
{
	int64_t status;

	status = main_fn();
	exit((int)status);
}
