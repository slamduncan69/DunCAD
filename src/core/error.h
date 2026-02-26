#ifndef DC_ERROR_H
#define DC_ERROR_H

/*
 * error.h — Uniform error type used throughout DunCAD.
 *
 * Ownership: DC_Error objects are typically stack-allocated by callers and
 * passed by pointer to functions that may populate them. The struct owns its
 * own internal char arrays; no heap allocation occurs inside DC_Error.
 */

#include <stdarg.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
typedef enum {
    DC_OK              = 0,
    DC_ERROR_MEMORY,
    DC_ERROR_IO,
    DC_ERROR_PARSE,
    DC_ERROR_NOT_FOUND,
    DC_ERROR_INVALID_ARG
} DC_ErrorCode;

/* -------------------------------------------------------------------------
 * DC_Error — carries code, human-readable message, and source location.
 *
 * Fields:
 *   code    — machine-readable error category
 *   message — human-readable description (NUL-terminated, 511 chars max)
 *   file    — source file where the error was set (__FILE__)
 *   line    — source line where the error was set (__LINE__)
 * ---------------------------------------------------------------------- */
typedef struct {
    DC_ErrorCode code;
    char         message[512];
    char         file[256];
    int          line;
} DC_Error;

/* -------------------------------------------------------------------------
 * dc_error_set — populate *err with a formatted message and location.
 *
 * Parameters:
 *   err  — output; must not be NULL
 *   code — error category
 *   fmt  — printf-style format string for the human-readable message
 *   ...  — format arguments
 *
 * Ownership: err is caller-owned; no heap allocation occurs.
 * ---------------------------------------------------------------------- */
void dc_error_set(DC_Error *err, DC_ErrorCode code, const char *fmt, ...);

/* Internal variant used by the DC_SET_ERROR macro to capture __FILE__/__LINE__ */
void dc_error_set_at(DC_Error *err, DC_ErrorCode code,
                     const char *file, int line,
                     const char *fmt, ...);

/* -------------------------------------------------------------------------
 * dc_error_clear — reset *err to DC_OK with an empty message.
 *
 * Parameters:
 *   err — must not be NULL
 * ---------------------------------------------------------------------- */
void dc_error_clear(DC_Error *err);

/* -------------------------------------------------------------------------
 * dc_error_string — return a static string label for a given error code.
 *
 * Returns: a NUL-terminated string literal; caller must not free it.
 * ---------------------------------------------------------------------- */
const char *dc_error_string(DC_ErrorCode code);

/* -------------------------------------------------------------------------
 * DC_SET_ERROR — convenience macro that captures __FILE__ / __LINE__
 * automatically.
 *
 * Usage: DC_SET_ERROR(err_ptr, DC_ERROR_IO, "open failed: %s", path);
 * ---------------------------------------------------------------------- */
#define DC_SET_ERROR(err, code, ...) \
    dc_error_set_at((err), (code), __FILE__, __LINE__, __VA_ARGS__)

/* -------------------------------------------------------------------------
 * DC_CHECK — propagate an DC_Error from a sub-call.
 *
 * Usage:
 *   DC_Error err = {0};
 *   DC_CHECK(&err, some_function(&err));
 *   // if some_function set err.code != DC_OK we return early
 *
 * The macro evaluates `call`, then checks err->code. If non-zero it returns
 * err->code from the enclosing function.  The enclosing function must return
 * DC_ErrorCode (or an int compatible type).
 * ---------------------------------------------------------------------- */
#define DC_CHECK(err, call) \
    do { \
        (call); \
        if ((err)->code != DC_OK) return (err)->code; \
    } while (0)

#endif /* DC_ERROR_H */
