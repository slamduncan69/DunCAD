#ifndef DC_LOG_H
#define DC_LOG_H

/*
 * log.h — Structured logger for DunCAD.
 *
 * The logger is an explicit singleton: call dc_log_init() once at startup and
 * dc_log_shutdown() at exit.  No other global mutable state exists in the
 * application.
 *
 * Every call to dc_log() writes:
 *   1. A human-readable line to stderr.
 *   2. A JSON object on a single line to the structured log file.
 *
 * The JSON log is a first-class LLM context artifact and must remain
 * machine-parseable (one JSON object per line, UTF-8, no trailing commas).
 *
 * Thread safety: NOT thread-safe in Phase 1. Add a mutex before Phase 3.
 *
 * Ownership: dc_log_init() opens the log file; dc_log_shutdown() closes it.
 *            The singleton owns the file handle.
 */

/* -------------------------------------------------------------------------
 * Log levels
 * ---------------------------------------------------------------------- */
typedef enum {
    DC_LOG_DEBUG = 0,
    DC_LOG_INFO,
    DC_LOG_WARN,
    DC_LOG_ERROR
} DC_LogLevel;

/* -------------------------------------------------------------------------
 * Semantic event categories — used as the "event" field in JSON output.
 * This lets LLM consumers filter log streams by subsystem.
 * ---------------------------------------------------------------------- */
typedef enum {
    DC_LOG_EVENT_APP = 0,
    DC_LOG_EVENT_RENDER,
    DC_LOG_EVENT_FILE,
    DC_LOG_EVENT_BUILD,
    DC_LOG_EVENT_TOOL,
    DC_LOG_EVENT_LLM
} DC_LogEventType;

/* -------------------------------------------------------------------------
 * dc_log_init — open the structured log file and prepare the singleton.
 *
 * Parameters:
 *   log_file_path — path to the JSON log file; created or appended to.
 *                   If NULL, JSON output is suppressed (stderr still works).
 *
 * Must be called before any dc_log() calls.
 * ---------------------------------------------------------------------- */
void dc_log_init(const char *log_file_path);

/* -------------------------------------------------------------------------
 * dc_log_shutdown — flush and close the log file.  Safe to call even if
 * dc_log_init was never called.
 * ---------------------------------------------------------------------- */
void dc_log_shutdown(void);

/* -------------------------------------------------------------------------
 * dc_log — emit a log record.
 *
 * Parameters:
 *   level  — severity level; records below dc_log_set_level() are dropped
 *   event  — semantic subsystem category
 *   fmt    — printf-style format string
 *   ...    — format arguments
 *
 * Output: human-readable line to stderr + JSON line to log file.
 * ---------------------------------------------------------------------- */
void dc_log(DC_LogLevel level, DC_LogEventType event, const char *fmt, ...);

/* -------------------------------------------------------------------------
 * dc_log_set_level — drop records below min_level.
 *
 * Default minimum level is DC_LOG_DEBUG (everything logged).
 * ---------------------------------------------------------------------- */
void dc_log_set_level(DC_LogLevel min_level);

/* -------------------------------------------------------------------------
 * Convenience macros — capture __FILE__ and __LINE__ automatically.
 * ---------------------------------------------------------------------- */
#define DC_LOG_DEBUG_APP(fmt, ...)  dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP,    fmt, ##__VA_ARGS__)
#define DC_LOG_INFO_APP(fmt, ...)   dc_log(DC_LOG_INFO,  DC_LOG_EVENT_APP,    fmt, ##__VA_ARGS__)
#define DC_LOG_WARN_APP(fmt, ...)   dc_log(DC_LOG_WARN,  DC_LOG_EVENT_APP,    fmt, ##__VA_ARGS__)
#define DC_LOG_ERROR_APP(fmt, ...)  dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,    fmt, ##__VA_ARGS__)

#define DC_LOG_INFO_BUILD(fmt, ...) dc_log(DC_LOG_INFO,  DC_LOG_EVENT_BUILD,  fmt, ##__VA_ARGS__)
#define DC_LOG_INFO_FILE(fmt, ...)  dc_log(DC_LOG_INFO,  DC_LOG_EVENT_FILE,   fmt, ##__VA_ARGS__)
#define DC_LOG_INFO_TOOL(fmt, ...)  dc_log(DC_LOG_INFO,  DC_LOG_EVENT_TOOL,   fmt, ##__VA_ARGS__)
#define DC_LOG_INFO_LLM(fmt, ...)   dc_log(DC_LOG_INFO,  DC_LOG_EVENT_LLM,    fmt, ##__VA_ARGS__)

#endif /* DC_LOG_H */
