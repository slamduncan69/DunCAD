#include "error.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * dc_error_set_at — internal implementation that records source location.
 * ---------------------------------------------------------------------- */
void
dc_error_set_at(DC_Error *err, DC_ErrorCode code,
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
 * dc_error_set — public variant; source location is the call site of this
 * function, which is less useful than the macro but satisfies the interface.
 * ---------------------------------------------------------------------- */
void
dc_error_set(DC_Error *err, DC_ErrorCode code, const char *fmt, ...)
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
 * dc_error_clear
 * ---------------------------------------------------------------------- */
void
dc_error_clear(DC_Error *err)
{
    if (!err) return;
    err->code    = DC_OK;
    err->line    = 0;
    err->message[0] = '\0';
    err->file[0]    = '\0';
}

/* -------------------------------------------------------------------------
 * dc_error_string
 * ---------------------------------------------------------------------- */
const char *
dc_error_string(DC_ErrorCode code)
{
    switch (code) {
        case DC_OK:               return "OK";
        case DC_ERROR_MEMORY:     return "MEMORY";
        case DC_ERROR_IO:         return "IO";
        case DC_ERROR_PARSE:      return "PARSE";
        case DC_ERROR_NOT_FOUND:  return "NOT_FOUND";
        case DC_ERROR_INVALID_ARG: return "INVALID_ARG";
        default:                  return "UNKNOWN";
    }
}
