#include "array.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal structure definition
 * ---------------------------------------------------------------------- */

#define DC_ARRAY_INITIAL_CAPACITY 8

struct DC_Array {
    void   *data;         /* heap-allocated buffer of (capacity * element_size) bytes */
    size_t  length;       /* number of elements currently stored */
    size_t  capacity;     /* number of elements the buffer can hold */
    size_t  element_size; /* size in bytes of each element */
};

/* -------------------------------------------------------------------------
 * dc_array_new
 * ---------------------------------------------------------------------- */
DC_Array *
dc_array_new(size_t element_size)
{
    if (element_size == 0) return NULL;

    DC_Array *arr = malloc(sizeof(DC_Array));
    if (!arr) return NULL;

    arr->element_size = element_size;
    arr->length       = 0;
    arr->capacity     = DC_ARRAY_INITIAL_CAPACITY;

    arr->data = malloc(arr->capacity * element_size);
    if (!arr->data) {
        free(arr);
        return NULL;
    }

    return arr;
}

/* -------------------------------------------------------------------------
 * dc_array_free
 * ---------------------------------------------------------------------- */
void
dc_array_free(DC_Array *arr)
{
    if (!arr) return;
    free(arr->data);
    free(arr);
}

/* -------------------------------------------------------------------------
 * dc_array_push
 * ---------------------------------------------------------------------- */
int
dc_array_push(DC_Array *arr, const void *element)
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
 * dc_array_get
 * ---------------------------------------------------------------------- */
void *
dc_array_get(DC_Array *arr, size_t index)
{
    if (!arr || index >= arr->length) return NULL;
    return (char *)arr->data + index * arr->element_size;
}

/* -------------------------------------------------------------------------
 * dc_array_remove
 * ---------------------------------------------------------------------- */
int
dc_array_remove(DC_Array *arr, size_t index)
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
 * dc_array_length
 * ---------------------------------------------------------------------- */
size_t
dc_array_length(DC_Array *arr)
{
    if (!arr) return 0;
    return arr->length;
}

/* -------------------------------------------------------------------------
 * dc_array_clear
 * ---------------------------------------------------------------------- */
void
dc_array_clear(DC_Array *arr)
{
    if (!arr) return;
    arr->length = 0;
}
