#ifndef EF_ARRAY_H
#define EF_ARRAY_H

/*
 * array.h — Type-safe dynamic array using void pointers.
 *
 * EF_Array stores copies of fixed-size elements.  The element size is set at
 * construction time and every operation uses that size consistently.
 *
 * Ownership:
 *   - EF_Array owns its internal buffer; callers own the elements they pass
 *     in (copies are made).
 *   - ef_array_get() returns a pointer into the internal buffer; the pointer
 *     is valid until the next mutating operation (push, remove, clear).
 *   - ef_array_free() releases the internal buffer and the struct itself.
 *
 * Error return values: 0 = success, -1 = error (memory allocation failure or
 * invalid argument).
 */

#include <stddef.h>

/* -------------------------------------------------------------------------
 * EF_Array — opaque handle; members are private to array.c
 * ---------------------------------------------------------------------- */
typedef struct EF_Array EF_Array;

/* -------------------------------------------------------------------------
 * ef_array_new — allocate a new dynamic array for elements of element_size.
 *
 * Parameters:
 *   element_size — size in bytes of each element; must be > 0
 *
 * Returns: pointer to a new EF_Array, or NULL on allocation failure.
 * Ownership: caller owns the returned pointer; free with ef_array_free().
 * ---------------------------------------------------------------------- */
EF_Array *ef_array_new(size_t element_size);

/* -------------------------------------------------------------------------
 * ef_array_free — release all memory owned by arr, including arr itself.
 *
 * Parameters:
 *   arr — may be NULL (no-op)
 * ---------------------------------------------------------------------- */
void ef_array_free(EF_Array *arr);

/* -------------------------------------------------------------------------
 * ef_array_push — append a copy of element to the end of arr.
 *
 * Parameters:
 *   arr     — must not be NULL
 *   element — pointer to element_size bytes to copy; must not be NULL
 *
 * Returns: 0 on success, -1 on allocation failure.
 * ---------------------------------------------------------------------- */
int ef_array_push(EF_Array *arr, const void *element);

/* -------------------------------------------------------------------------
 * ef_array_get — return a pointer to the element at index.
 *
 * Parameters:
 *   arr   — must not be NULL
 *   index — must be < ef_array_length(arr)
 *
 * Returns: pointer into internal buffer, or NULL if index is out of bounds.
 * Ownership: borrowed; do not free. Invalidated by any mutating operation.
 * ---------------------------------------------------------------------- */
void *ef_array_get(EF_Array *arr, size_t index);

/* -------------------------------------------------------------------------
 * ef_array_remove — remove element at index, shifting later elements left.
 *
 * Parameters:
 *   arr   — must not be NULL
 *   index — must be < ef_array_length(arr)
 *
 * Returns: 0 on success, -1 if index is out of bounds.
 * ---------------------------------------------------------------------- */
int ef_array_remove(EF_Array *arr, size_t index);

/* -------------------------------------------------------------------------
 * ef_array_length — return the number of elements currently in arr.
 *
 * Parameters:
 *   arr — must not be NULL
 * ---------------------------------------------------------------------- */
size_t ef_array_length(EF_Array *arr);

/* -------------------------------------------------------------------------
 * ef_array_clear — remove all elements; retains the allocated buffer.
 *
 * Parameters:
 *   arr — must not be NULL
 * ---------------------------------------------------------------------- */
void ef_array_clear(EF_Array *arr);

#endif /* EF_ARRAY_H */
