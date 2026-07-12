/* SPDX-License-Identifier: TBD */
/*
 * helium_runtime.h - public API for the Helium reference-counting runtime.
 */

#ifndef HELIUM_RUNTIME_H
#define HELIUM_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * helium_kind_t - discriminates heap-allocated Helium values.
 */
typedef enum helium_kind {
	HELIUM_KIND_STRING,
	HELIUM_KIND_ARRAY,
	HELIUM_KIND_RECORD,
	HELIUM_KIND_ADT,
	HELIUM_KIND_CLOSURE,
} helium_kind_t;

struct helium_object;
typedef struct helium_object helium_object_t;

/*
 * helium_destroy_fn - kind-specific destructor called when the reference count
 * of an object reaches zero.  The function must release any heap references
 * held by the object and then free the object itself.
 */
typedef void (*helium_destroy_fn)(helium_object_t *obj);

struct helium_object {
	size_t refcount;
	helium_kind_t kind;
	helium_destroy_fn destroy;
};

/*
 * helium_string_t - immutable UTF-8 byte sequence.
 */
typedef struct helium_string {
	helium_object_t header;
	size_t length;
	char data[];
} helium_string_t;

/*
 * helium_array_elem_destroy_fn - per-element destructor for arrays.
 *
 * The function receives a pointer to the element stored inside the array.
 * It may be NULL for arrays of primitive values.
 */
typedef void (*helium_array_elem_destroy_fn)(void *elem);

/*
 * helium_array_t - fixed-size array of uniformly-sized elements.
 */
typedef struct helium_array {
	helium_object_t header;
	size_t length;
	size_t elem_size;
	helium_array_elem_destroy_fn elem_destroy;
	uint8_t data[];
} helium_array_t;

/*
 * helium_record_t - heap-allocated record value.
 *
 * @fields_destroy: compiler-generated destructor that releases all
 * heap-allocated fields.  It receives the record object but must not free it.
 */
typedef struct helium_record {
	helium_object_t header;
	helium_destroy_fn fields_destroy;
	uint8_t data[];
} helium_record_t;

/*
 * helium_adt_t - heap-allocated algebraic data type variant.
 */
typedef struct helium_adt {
	helium_object_t header;
	size_t variant;
	helium_destroy_fn fields_destroy;
	uint8_t data[];
} helium_adt_t;

/*
 * helium_generic_fn_t - generic function pointer stored inside closures.
 *
 * All function pointers can be cast to and from this type without loss.
 */
typedef void (*helium_generic_fn_t)(void);

/*
 * helium_closure_t - closure value containing a code pointer and captured
 * environment.
 */
typedef struct helium_closure {
	helium_object_t header;
	helium_generic_fn_t fn;
	void (*env_destroy)(void *env);
	void *env;
} helium_closure_t;

/* Reference counting. */
void helium_retain(helium_object_t *obj);
void helium_release(helium_object_t *obj);

/* Kind-specific destructors. */
void helium_destroy_string(helium_object_t *obj);
void helium_destroy_array(helium_object_t *obj);
void helium_destroy_record(helium_object_t *obj);
void helium_destroy_adt(helium_object_t *obj);
void helium_destroy_closure(helium_object_t *obj);

/* Allocation helpers. */
helium_string_t *helium_alloc_string(size_t length);
helium_array_t *helium_alloc_array(size_t length, size_t elem_size,
				    helium_array_elem_destroy_fn elem_destroy);
helium_record_t *helium_alloc_record(size_t size,
				     helium_destroy_fn fields_destroy);
helium_adt_t *helium_alloc_adt(size_t variant, size_t size,
			       helium_destroy_fn fields_destroy);
helium_closure_t *helium_alloc_closure(helium_generic_fn_t fn, void *env,
				       void (*env_destroy)(void *env));

/* Program entry wrapper. */
void helium_main_wrapper(int64_t (*main_fn)(void));

#ifdef __cplusplus
}
#endif

#endif /* HELIUM_RUNTIME_H */
