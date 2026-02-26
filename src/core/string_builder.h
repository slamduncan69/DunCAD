#ifndef DC_STRING_BUILDER_H
#define DC_STRING_BUILDER_H

/*
 * string_builder.h — Dynamic string construction utility.
 *
 * DC_StringBuilder maintains a heap-allocated buffer that grows as needed.
 * It always stores a NUL-terminated C string internally.
 *
 * Ownership:
 *   - DC_StringBuilder owns its internal buffer.
 *   - dc_sb_get() returns a borrowed pointer; do not free it; the pointer is
 *     invalidated by any mutating operation.
 *   - dc_sb_take() transfers ownership of the buffer to the caller; the
 *     caller must free() the returned pointer.  The builder is reset to an
 *     empty state afterwards.
 *   - dc_sb_free() releases both the internal buffer and the struct itself.
 *
 * Error return values: 0 = success, -1 = allocation failure or invalid arg.
 */

#include <stddef.h>
#include <stdarg.h>

/* -------------------------------------------------------------------------
 * DC_StringBuilder — opaque handle
 * ---------------------------------------------------------------------- */
typedef struct DC_StringBuilder DC_StringBuilder;

/* -------------------------------------------------------------------------
 * dc_sb_new — allocate and initialise an empty StringBuilder.
 *
 * Returns: new DC_StringBuilder, or NULL on allocation failure.
 * Ownership: caller owns; free with dc_sb_free() or dc_sb_take().
 * ---------------------------------------------------------------------- */
DC_StringBuilder *dc_sb_new(void);

/* -------------------------------------------------------------------------
 * dc_sb_free — release all memory owned by sb, including sb itself.
 *
 * Parameters:
 *   sb — may be NULL (no-op)
 * ---------------------------------------------------------------------- */
void dc_sb_free(DC_StringBuilder *sb);

/* -------------------------------------------------------------------------
 * dc_sb_append — append a NUL-terminated string to sb.
 *
 * Parameters:
 *   sb  — must not be NULL
 *   str — NUL-terminated string to append; NULL is treated as empty string
 *
 * Returns: 0 on success, -1 on allocation failure.
 * ---------------------------------------------------------------------- */
int dc_sb_append(DC_StringBuilder *sb, const char *str);

/* -------------------------------------------------------------------------
 * dc_sb_appendf — printf-style formatted append.
 *
 * Parameters:
 *   sb  — must not be NULL
 *   fmt — printf format string; must not be NULL
 *   ... — format arguments
 *
 * Returns: 0 on success, -1 on allocation failure or format error.
 * ---------------------------------------------------------------------- */
int dc_sb_appendf(DC_StringBuilder *sb, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* -------------------------------------------------------------------------
 * dc_sb_append_char — append a single character.
 *
 * Parameters:
 *   sb — must not be NULL
 *   c  — character to append
 *
 * Returns: 0 on success, -1 on allocation failure.
 * ---------------------------------------------------------------------- */
int dc_sb_append_char(DC_StringBuilder *sb, char c);

/* -------------------------------------------------------------------------
 * dc_sb_get — return a borrowed pointer to the current string contents.
 *
 * Returns: NUL-terminated string; never NULL (returns "" for empty builder).
 * Ownership: borrowed; do not free; invalidated by any mutating operation.
 * ---------------------------------------------------------------------- */
const char *dc_sb_get(DC_StringBuilder *sb);

/* -------------------------------------------------------------------------
 * dc_sb_length — return the number of bytes in the current string (not
 * counting the NUL terminator).
 *
 * Parameters:
 *   sb — must not be NULL
 * ---------------------------------------------------------------------- */
size_t dc_sb_length(DC_StringBuilder *sb);

/* -------------------------------------------------------------------------
 * dc_sb_clear — reset the builder to empty without freeing the buffer.
 *
 * Parameters:
 *   sb — must not be NULL
 * ---------------------------------------------------------------------- */
void dc_sb_clear(DC_StringBuilder *sb);

/* -------------------------------------------------------------------------
 * dc_sb_take — transfer ownership of the internal buffer to the caller.
 *
 * The builder is reset to an empty state.  The caller must free() the
 * returned string.
 *
 * Parameters:
 *   sb — must not be NULL
 *
 * Returns: heap-allocated NUL-terminated string; caller must free().
 *          Returns NULL on allocation failure during the internal reset.
 * ---------------------------------------------------------------------- */
char *dc_sb_take(DC_StringBuilder *sb);

#endif /* DC_STRING_BUILDER_H */
