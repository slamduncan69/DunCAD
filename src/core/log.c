#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Singleton state
 * ---------------------------------------------------------------------- */
static struct {
    FILE       *log_file;   /* JSON structured log; NULL if not configured */
    DC_LogLevel min_level;
    int         initialised;
} g_log = { NULL, DC_LOG_DEBUG, 0 };

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static const char *
level_to_string(DC_LogLevel level)
{
    switch (level) {
        case DC_LOG_DEBUG: return "DEBUG";
        case DC_LOG_INFO:  return "INFO";
        case DC_LOG_WARN:  return "WARN";
        case DC_LOG_ERROR: return "ERROR";
        default:           return "UNKNOWN";
    }
}

static const char *
event_to_string(DC_LogEventType event)
{
    switch (event) {
        case DC_LOG_EVENT_APP:    return "APP";
        case DC_LOG_EVENT_RENDER: return "RENDER";
        case DC_LOG_EVENT_FILE:   return "FILE";
        case DC_LOG_EVENT_BUILD:  return "BUILD";
        case DC_LOG_EVENT_TOOL:   return "TOOL";
        case DC_LOG_EVENT_LLM:    return "LLM";
        default:                  return "UNKNOWN";
    }
}

/*
 * iso8601_now — write an ISO-8601 UTC timestamp to buf (at least 32 bytes).
 */
static void
iso8601_now(char *buf, size_t buf_size)
{
    time_t     now = time(NULL);
    struct tm *utc = gmtime(&now); /* Not thread-safe; see log.h thread-safety note */

    strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", utc);
}

/*
 * json_escape_string — write a JSON-safe version of src into dst.
 * dst must be at least 2*src_len + 3 bytes (worst case all backslashes).
 * Returns number of bytes written (excluding NUL terminator).
 */
static size_t
json_escape_string(char *dst, size_t dst_size, const char *src)
{
    size_t written = 0;
    const char *p  = src;

    while (*p && written + 4 < dst_size) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            dst[written++] = '\\';
            dst[written++] = (char)c;
        } else if (c == '\n') {
            dst[written++] = '\\';
            dst[written++] = 'n';
        } else if (c == '\r') {
            dst[written++] = '\\';
            dst[written++] = 'r';
        } else if (c == '\t') {
            dst[written++] = '\\';
            dst[written++] = 't';
        } else if (c < 0x20) {
            /* Control characters: \uXXXX */
            written += (size_t)snprintf(dst + written, dst_size - written,
                                        "\\u%04x", c);
        } else {
            dst[written++] = (char)c;
        }
        p++;
    }
    dst[written] = '\0';
    return written;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void
dc_log_init(const char *log_file_path)
{
    g_log.min_level   = DC_LOG_DEBUG;
    g_log.initialised = 1;

    if (log_file_path) {
        g_log.log_file = fopen(log_file_path, "a");
        if (!g_log.log_file) {
            fprintf(stderr, "[DC_LOG] WARNING: could not open log file: %s\n",
                    log_file_path);
        }
    }
}

void
dc_log_shutdown(void)
{
    if (g_log.log_file) {
        fflush(g_log.log_file);
        fclose(g_log.log_file);
        g_log.log_file = NULL;
    }
    g_log.initialised = 0;
}

void
dc_log_set_level(DC_LogLevel min_level)
{
    g_log.min_level = min_level;
}

void
dc_log(DC_LogLevel level, DC_LogEventType event, const char *fmt, ...)
{
    if (!g_log.initialised) return;
    if (level < g_log.min_level) return;

    /* Format the message */
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);

    /* Timestamp */
    char ts[32];
    iso8601_now(ts, sizeof(ts));

    const char *lvl_str   = level_to_string(level);
    const char *event_str = event_to_string(event);

    /* Human-readable stderr output */
    fprintf(stderr, "[%s] [%s] [%s] %s\n", ts, lvl_str, event_str, msg);

    /* JSON structured log — one object per line */
    if (g_log.log_file) {
        /* Escape the message for JSON */
        char escaped[2048];
        json_escape_string(escaped, sizeof(escaped), msg);

        fprintf(g_log.log_file,
                "{\"ts\":\"%s\",\"level\":\"%s\",\"event\":\"%s\",\"msg\":\"%s\"}\n",
                ts, lvl_str, event_str, escaped);
        fflush(g_log.log_file);
    }
}
