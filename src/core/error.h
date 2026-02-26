#ifndef EF_ERROR_H
#define EF_ERROR_H

/*
 * error.h — Uniform error type used throughout ElectroForge.
 *
 * Ownership: EF_Error objects are typically stack-allocated by callers and
 * passed by pointer to functions that may populate them. The struct owns its
 * own internal char arrays; no heap allocation occurs inside EF_Error.
 */

#include <stdarg.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
typedef enum {
    EF_OK              = 0,
    EF_ERROR_MEMORY,
    EF_ERROR_IO,
    EF_ERROR_PARSE,
    EF_ERROR_NOT_FOUND,
    EF_ERROR_INVALID_ARG
} EF_ErrorCode;

/* -------------------------------------------------------------------------
 * EF_Error — carries code, human-readable message, and source location.
 *
 * Fields:
 *   code    — machine-readable error category
 *   message — human-readable description (NUL-terminated, 511 chars max)
 *   file    — source file where the error was set (__FILE__)
 *   line    — source line where the error was set (__LINE__)
 * ---------------------------------------------------------------------- */
typedef struct {
    EF_ErrorCode code;
    char         message[512];
    char         file[256];
    int          line;
} EF_Error;

/* -------------------------------------------------------------------------
 * ef_error_set — populate *err with a formatted message and location.
 *
 * Parameters:
 *   err  — output; must not be NULL
 *   code — error category
 *   fmt  — printf-style format string for the human-readable message
 *   ...  — format arguments
 *
 * Ownership: err is caller-owned; no heap allocation occurs.
 * ---------------------------------------------------------------------- */
void ef_error_set(EF_Error *err, EF_ErrorCode code, const char *fmt, ...);

/* Internal variant used by the EF_SET_ERROR macro to capture __FILE__/__LINE__ */
void ef_error_set_at(EF_Error *err, EF_ErrorCode code,
                     const char *file, int line,
                     const char *fmt, ...);

/* -------------------------------------------------------------------------
 * ef_error_clear — reset *err to EF_OK with an empty message.
 *
 * Parameters:
 *   err — must not be NULL
 * ---------------------------------------------------------------------- */
void ef_error_clear(EF_Error *err);

/* -------------------------------------------------------------------------
 * ef_error_string — return a static string label for a given error code.
 *
 * Returns: a NUL-terminated string literal; caller must not free it.
 * ---------------------------------------------------------------------- */
const char *ef_error_string(EF_ErrorCode code);

/* -------------------------------------------------------------------------
 * EF_SET_ERROR — convenience macro that captures __FILE__ / __LINE__
 * automatically.
 *
 * Usage: EF_SET_ERROR(err_ptr, EF_ERROR_IO, "open failed: %s", path);
 * ---------------------------------------------------------------------- */
#define EF_SET_ERROR(err, code, ...) \
    ef_error_set_at((err), (code), __FILE__, __LINE__, __VA_ARGS__)

/* -------------------------------------------------------------------------
 * EF_CHECK — propagate an EF_Error from a sub-call.
 *
 * Usage:
 *   EF_Error err = {0};
 *   EF_CHECK(&err, some_function(&err));
 *   // if some_function set err.code != EF_OK we return early
 *
 * The macro evaluates `call`, then checks err->code. If non-zero it returns
 * err->code from the enclosing function.  The enclosing function must return
 * EF_ErrorCode (or an int compatible type).
 * ---------------------------------------------------------------------- */
#define EF_CHECK(err, call) \
    do { \
        (call); \
        if ((err)->code != EF_OK) return (err)->code; \
    } while (0)

#endif /* EF_ERROR_H */
