#ifndef EF_LOG_H
#define EF_LOG_H

/*
 * log.h — Structured logger for ElectroForge.
 *
 * The logger is an explicit singleton: call ef_log_init() once at startup and
 * ef_log_shutdown() at exit.  No other global mutable state exists in the
 * application.
 *
 * Every call to ef_log() writes:
 *   1. A human-readable line to stderr.
 *   2. A JSON object on a single line to the structured log file.
 *
 * The JSON log is a first-class LLM context artifact and must remain
 * machine-parseable (one JSON object per line, UTF-8, no trailing commas).
 *
 * Thread safety: NOT thread-safe in Phase 1. Add a mutex before Phase 3.
 *
 * Ownership: ef_log_init() opens the log file; ef_log_shutdown() closes it.
 *            The singleton owns the file handle.
 */

/* -------------------------------------------------------------------------
 * Log levels
 * ---------------------------------------------------------------------- */
typedef enum {
    EF_LOG_DEBUG = 0,
    EF_LOG_INFO,
    EF_LOG_WARN,
    EF_LOG_ERROR
} EF_LogLevel;

/* -------------------------------------------------------------------------
 * Semantic event categories — used as the "event" field in JSON output.
 * This lets LLM consumers filter log streams by subsystem.
 * ---------------------------------------------------------------------- */
typedef enum {
    EF_LOG_EVENT_APP = 0,
    EF_LOG_EVENT_RENDER,
    EF_LOG_EVENT_FILE,
    EF_LOG_EVENT_BUILD,
    EF_LOG_EVENT_TOOL,
    EF_LOG_EVENT_LLM
} EF_LogEventType;

/* -------------------------------------------------------------------------
 * ef_log_init — open the structured log file and prepare the singleton.
 *
 * Parameters:
 *   log_file_path — path to the JSON log file; created or appended to.
 *                   If NULL, JSON output is suppressed (stderr still works).
 *
 * Must be called before any ef_log() calls.
 * ---------------------------------------------------------------------- */
void ef_log_init(const char *log_file_path);

/* -------------------------------------------------------------------------
 * ef_log_shutdown — flush and close the log file.  Safe to call even if
 * ef_log_init was never called.
 * ---------------------------------------------------------------------- */
void ef_log_shutdown(void);

/* -------------------------------------------------------------------------
 * ef_log — emit a log record.
 *
 * Parameters:
 *   level  — severity level; records below ef_log_set_level() are dropped
 *   event  — semantic subsystem category
 *   fmt    — printf-style format string
 *   ...    — format arguments
 *
 * Output: human-readable line to stderr + JSON line to log file.
 * ---------------------------------------------------------------------- */
void ef_log(EF_LogLevel level, EF_LogEventType event, const char *fmt, ...);

/* -------------------------------------------------------------------------
 * ef_log_set_level — drop records below min_level.
 *
 * Default minimum level is EF_LOG_DEBUG (everything logged).
 * ---------------------------------------------------------------------- */
void ef_log_set_level(EF_LogLevel min_level);

/* -------------------------------------------------------------------------
 * Convenience macros — capture __FILE__ and __LINE__ automatically.
 * ---------------------------------------------------------------------- */
#define EF_LOG_DEBUG_APP(fmt, ...)  ef_log(EF_LOG_DEBUG, EF_LOG_EVENT_APP,    fmt, ##__VA_ARGS__)
#define EF_LOG_INFO_APP(fmt, ...)   ef_log(EF_LOG_INFO,  EF_LOG_EVENT_APP,    fmt, ##__VA_ARGS__)
#define EF_LOG_WARN_APP(fmt, ...)   ef_log(EF_LOG_WARN,  EF_LOG_EVENT_APP,    fmt, ##__VA_ARGS__)
#define EF_LOG_ERROR_APP(fmt, ...)  ef_log(EF_LOG_ERROR, EF_LOG_EVENT_APP,    fmt, ##__VA_ARGS__)

#define EF_LOG_INFO_BUILD(fmt, ...) ef_log(EF_LOG_INFO,  EF_LOG_EVENT_BUILD,  fmt, ##__VA_ARGS__)
#define EF_LOG_INFO_FILE(fmt, ...)  ef_log(EF_LOG_INFO,  EF_LOG_EVENT_FILE,   fmt, ##__VA_ARGS__)
#define EF_LOG_INFO_TOOL(fmt, ...)  ef_log(EF_LOG_INFO,  EF_LOG_EVENT_TOOL,   fmt, ##__VA_ARGS__)
#define EF_LOG_INFO_LLM(fmt, ...)   ef_log(EF_LOG_INFO,  EF_LOG_EVENT_LLM,    fmt, ##__VA_ARGS__)

#endif /* EF_LOG_H */
