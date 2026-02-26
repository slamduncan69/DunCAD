#include "array.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal structure definition
 * ---------------------------------------------------------------------- */

#define EF_ARRAY_INITIAL_CAPACITY 8

struct EF_Array {
    void   *data;         /* heap-allocated buffer of (capacity * element_size) bytes */
    size_t  length;       /* number of elements currently stored */
    size_t  capacity;     /* number of elements the buffer can hold */
    size_t  element_size; /* size in bytes of each element */
};

/* -------------------------------------------------------------------------
 * ef_array_new
 * ---------------------------------------------------------------------- */
EF_Array *
ef_array_new(size_t element_size)
{
    if (element_size == 0) return NULL;

    EF_Array *arr = malloc(sizeof(EF_Array));
    if (!arr) return NULL;

    arr->element_size = element_size;
    arr->length       = 0;
    arr->capacity     = EF_ARRAY_INITIAL_CAPACITY;

    arr->data = malloc(arr->capacity * element_size);
    if (!arr->data) {
        free(arr);
        return NULL;
    }

    return arr;
}

/* -------------------------------------------------------------------------
 * ef_array_free
 * ---------------------------------------------------------------------- */
void
ef_array_free(EF_Array *arr)
{
    if (!arr) return;
    free(arr->data);
    free(arr);
}

/* -------------------------------------------------------------------------
 * ef_array_push
 * ---------------------------------------------------------------------- */
int
ef_array_push(EF_Array *arr, const void *element)
{
    if (!arr || !element) return -1;

    /* Grow by doubling if full */
    if (arr->length == arr->capacity) {
        size_t new_capacity = arr->capacity * 2;
        void  *new_data     = realloc(arr->data, new_capacity * arr->element_size);
        if (!new_data) return -1;
        arr->data     = new_data;
        arr->capacity = new_capacity;
    }

    /* Copy element into the next slot */
    char *dst = (char *)arr->data + arr->length * arr->element_size;
    memcpy(dst, element, arr->element_size);
    arr->length++;

    return 0;
}

/* -------------------------------------------------------------------------
 * ef_array_get
 * ---------------------------------------------------------------------- */
void *
ef_array_get(EF_Array *arr, size_t index)
{
    if (!arr || index >= arr->length) return NULL;
    return (char *)arr->data + index * arr->element_size;
}

/* -------------------------------------------------------------------------
 * ef_array_remove
 * ---------------------------------------------------------------------- */
int
ef_array_remove(EF_Array *arr, size_t index)
{
    if (!arr || index >= arr->length) return -1;

    /* Shift elements left to fill the gap */
    size_t  elements_after = arr->length - index - 1;
    if (elements_after > 0) {
        char *dst = (char *)arr->data + index * arr->element_size;
        char *src = dst + arr->element_size;
        memmove(dst, src, elements_after * arr->element_size);
    }

    arr->length--;
    return 0;
}

/* -------------------------------------------------------------------------
 * ef_array_length
 * ---------------------------------------------------------------------- */
size_t
ef_array_length(EF_Array *arr)
{
    if (!arr) return 0;
    return arr->length;
}

/* -------------------------------------------------------------------------
 * ef_array_clear
 * ---------------------------------------------------------------------- */
void
ef_array_clear(EF_Array *arr)
{
    if (!arr) return;
    arr->length = 0;
}
