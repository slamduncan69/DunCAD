#include "string_builder.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Internal structure definition
 * ---------------------------------------------------------------------- */

#define EF_SB_INITIAL_CAPACITY 64

struct EF_StringBuilder {
    char  *buf;      /* heap-allocated buffer, always NUL-terminated */
    size_t length;   /* number of bytes currently stored (not counting NUL) */
    size_t capacity; /* total bytes allocated (includes NUL slot) */
};

/* -------------------------------------------------------------------------
 * Internal: ensure there is room for at least `additional` more bytes.
 * Returns 0 on success, -1 on allocation failure.
 * ---------------------------------------------------------------------- */
static int
sb_ensure_capacity(EF_StringBuilder *sb, size_t additional)
{
    size_t needed = sb->length + additional + 1; /* +1 for NUL */
    if (needed <= sb->capacity) return 0;

    /* Double until sufficient */
    size_t new_cap = sb->capacity;
    while (new_cap < needed) new_cap *= 2;

    char *new_buf = realloc(sb->buf, new_cap);
    if (!new_buf) return -1;

    sb->buf      = new_buf;
    sb->capacity = new_cap;
    return 0;
}

/* -------------------------------------------------------------------------
 * ef_sb_new
 * ---------------------------------------------------------------------- */
EF_StringBuilder *
ef_sb_new(void)
{
    EF_StringBuilder *sb = malloc(sizeof(EF_StringBuilder));
    if (!sb) return NULL;

    sb->buf = malloc(EF_SB_INITIAL_CAPACITY);
    if (!sb->buf) {
        free(sb);
        return NULL;
    }

    sb->buf[0]  = '\0';
    sb->length   = 0;
    sb->capacity = EF_SB_INITIAL_CAPACITY;

    return sb;
}

/* -------------------------------------------------------------------------
 * ef_sb_free
 * ---------------------------------------------------------------------- */
void
ef_sb_free(EF_StringBuilder *sb)
{
    if (!sb) return;
    free(sb->buf);
    free(sb);
}

/* -------------------------------------------------------------------------
 * ef_sb_append
 * ---------------------------------------------------------------------- */
int
ef_sb_append(EF_StringBuilder *sb, const char *str)
{
    if (!sb) return -1;
    if (!str || str[0] == '\0') return 0;

    size_t len = strlen(str);
    if (sb_ensure_capacity(sb, len) != 0) return -1;

    memcpy(sb->buf + sb->length, str, len + 1); /* +1 copies the NUL */
    sb->length += len;

    return 0;
}

/* -------------------------------------------------------------------------
 * ef_sb_appendf
 * ---------------------------------------------------------------------- */
int
ef_sb_appendf(EF_StringBuilder *sb, const char *fmt, ...)
{
    if (!sb || !fmt) return -1;

    /* Determine how many bytes are needed */
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (needed < 0) return -1;
    if (needed == 0) return 0;

    if (sb_ensure_capacity(sb, (size_t)needed) != 0) return -1;

    va_start(ap, fmt);
    vsnprintf(sb->buf + sb->length, (size_t)needed + 1, fmt, ap);
    va_end(ap);

    sb->length += (size_t)needed;

    return 0;
}

/* -------------------------------------------------------------------------
 * ef_sb_append_char
 * ---------------------------------------------------------------------- */
int
ef_sb_append_char(EF_StringBuilder *sb, char c)
{
    if (!sb) return -1;
    if (sb_ensure_capacity(sb, 1) != 0) return -1;

    sb->buf[sb->length]     = c;
    sb->buf[sb->length + 1] = '\0';
    sb->length++;

    return 0;
}

/* -------------------------------------------------------------------------
 * ef_sb_get
 * ---------------------------------------------------------------------- */
const char *
ef_sb_get(EF_StringBuilder *sb)
{
    if (!sb || !sb->buf) return "";
    return sb->buf;
}

/* -------------------------------------------------------------------------
 * ef_sb_length
 * ---------------------------------------------------------------------- */
size_t
ef_sb_length(EF_StringBuilder *sb)
{
    if (!sb) return 0;
    return sb->length;
}

/* -------------------------------------------------------------------------
 * ef_sb_clear
 * ---------------------------------------------------------------------- */
void
ef_sb_clear(EF_StringBuilder *sb)
{
    if (!sb) return;
    sb->length  = 0;
    sb->buf[0]  = '\0';
}

/* -------------------------------------------------------------------------
 * ef_sb_take
 * ---------------------------------------------------------------------- */
char *
ef_sb_take(EF_StringBuilder *sb)
{
    if (!sb) return NULL;

    char *result = sb->buf;

    /* Reset the builder with a fresh small buffer */
    sb->buf = malloc(EF_SB_INITIAL_CAPACITY);
    if (sb->buf) {
        sb->buf[0]  = '\0';
        sb->length   = 0;
        sb->capacity = EF_SB_INITIAL_CAPACITY;
    } else {
        /* Allocation failed: leave builder in a consistent (empty) state */
        sb->buf      = NULL;
        sb->length   = 0;
        sb->capacity = 0;
    }

    return result;
}
