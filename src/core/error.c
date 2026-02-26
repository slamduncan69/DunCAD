#include "error.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * ef_error_set_at — internal implementation that records source location.
 * ---------------------------------------------------------------------- */
void
ef_error_set_at(EF_Error *err, EF_ErrorCode code,
                const char *file, int line,
                const char *fmt, ...)
{
    if (!err) return;

    err->code = code;
    err->line = line;

    /* Truncate gracefully rather than overflow */
    strncpy(err->file, file ? file : "", sizeof(err->file) - 1);
    err->file[sizeof(err->file) - 1] = '\0';

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt ? fmt : "", ap);
    va_end(ap);
}

/* -------------------------------------------------------------------------
 * ef_error_set — public variant; source location is the call site of this
 * function, which is less useful than the macro but satisfies the interface.
 * ---------------------------------------------------------------------- */
void
ef_error_set(EF_Error *err, EF_ErrorCode code, const char *fmt, ...)
{
    if (!err) return;

    err->code = code;
    err->line = 0;
    err->file[0] = '\0';

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt ? fmt : "", ap);
    va_end(ap);
}

/* -------------------------------------------------------------------------
 * ef_error_clear
 * ---------------------------------------------------------------------- */
void
ef_error_clear(EF_Error *err)
{
    if (!err) return;
    err->code    = EF_OK;
    err->line    = 0;
    err->message[0] = '\0';
    err->file[0]    = '\0';
}

/* -------------------------------------------------------------------------
 * ef_error_string
 * ---------------------------------------------------------------------- */
const char *
ef_error_string(EF_ErrorCode code)
{
    switch (code) {
        case EF_OK:               return "OK";
        case EF_ERROR_MEMORY:     return "MEMORY";
        case EF_ERROR_IO:         return "IO";
        case EF_ERROR_PARSE:      return "PARSE";
        case EF_ERROR_NOT_FOUND:  return "NOT_FOUND";
        case EF_ERROR_INVALID_ARG: return "INVALID_ARG";
        default:                  return "UNKNOWN";
    }
}
