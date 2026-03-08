/*
 * talmud_util.h — shared utility functions
 *
 * Single-header, zero-dependency helpers used across talmud.
 * All functions are static — include this directly in any .c file.
 *
 * API:
 *   resolve_exe_dir(probe, fallback, out, outsz)  Resolve exe directory
 *   dir_up(path, levels)                          Strip N path components
 *   run_capture(cmd, buf, bufsz)                  Run shell command, capture stdout
 *   bin_from_src(src, bin, binsz)                 Strip .c extension to get binary name
 *   read_file_alloc(path, out_buf, out_size)      Read entire file into malloc'd buffer
 *   resolve_talmud_dir()                          Resolve the source directory
 *
 * Define TALMUD_UTIL_NEED_BIN before including to enable bin_from_src().
 * Define TALMUD_UTIL_NEED_DIR before including to enable directory-aware
 * functions that depend on a global g_talmud_dir[4096] and TALMUD_SRC_DIR.
 * Define TALMUD_UTIL_NEED_CPARSE before including to enable C parsing helpers:
 *   HAND_WRITTEN[], parse_func_def(), scan_braces(), count_params().
 */
#ifndef TALMUD_UTIL_H
#define TALMUD_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/*
 * Run a shell command and capture its stdout into buf.
 * Returns the exit code on success, -1 on failure.
 * Zeroes buf on popen failure.
 */
static inline __attribute__((unused))
int run_capture(const char *cmd, char *buf, size_t bufsz) {
    FILE *fp = popen(cmd, "r");
    if (!fp) { buf[0] = '\0'; return -1; }
    size_t total = 0;
    while (total < bufsz - 1) {
        size_t n = fread(buf + total, 1, bufsz - 1 - total, fp);
        if (n == 0) break;
        total += n;
    }
    buf[total] = '\0';
    int status = pclose(fp);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

/*
 * Resolve the directory of the running executable.
 *
 * Uses /proc/self/exe to find the executable, strips the filename to get
 * the directory, then probes for `probe` (a relative path) under that
 * directory. If the probe exists, writes the exe directory to `out`.
 * If not, writes `fallback` to `out`.
 *
 * Returns 0 on success, -1 on readlink/parse failure.
 */
static inline __attribute__((unused))
int resolve_exe_dir(const char *probe, const char *fallback,
                    char *out, size_t outsz)
{
    char exe[4096];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len < 0) return -1;
    exe[len] = '\0';
    char *slash = strrchr(exe, '/');
    if (!slash) return -1;
    *slash = '\0';

    char test[8192];
    snprintf(test, sizeof(test), "%s/%s", exe, probe);
    if (access(test, F_OK) == 0)
        snprintf(out, outsz, "%s", exe);
    else
        snprintf(out, outsz, "%s", fallback);
    return 0;
}

/*
 * Strip `levels` path components from `path` in place.
 * E.g., dir_up("/a/b/c", 2) produces "/a".
 * Returns 0 on success, -1 if not enough components.
 */
static inline __attribute__((unused))
int dir_up(char *path, int levels)
{
    for (int i = 0; i < levels; i++) {
        char *slash = strrchr(path, '/');
        if (!slash) return -1;
        *slash = '\0';
    }
    return 0;
}

/*
 * Derive a binary path from a .c source path by stripping the extension.
 * If the source doesn't end in ".c", appends ".bin" instead.
 * Define TALMUD_UTIL_NEED_BIN before including to enable this function. */
#ifdef TALMUD_UTIL_NEED_BIN
static void bin_from_src(const char *src, char *bin, size_t binsz) {
    size_t len = strlen(src);
    if (len > 2 && src[len - 2] == '.' && src[len - 1] == 'c') {
        size_t copy = (len - 2 < binsz - 1) ? len - 2 : binsz - 1;
        memcpy(bin, src, copy);
        bin[copy] = '\0';
    } else {
        snprintf(bin, binsz, "%s.bin", src);
    }
}
#endif /* TALMUD_UTIL_NEED_BIN */

/*
 * Directory-aware functions. These require:
 *   1. TALMUD_SRC_DIR defined at compile time (fallback for resolve_talmud_dir)
 *
 * Define TALMUD_UTIL_NEED_DIR before including to enable.
 * Declares g_talmud_dir[2048] automatically.
 */
#ifdef TALMUD_UTIL_NEED_DIR

static char g_talmud_dir[2048];

static inline __attribute__((unused))
int resolve_talmud_dir(void) {
    char exe[2048];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len < 0) goto fallback;
    exe[len] = '\0';

    /* Strip binary name to get directory */
    char *slash = strrchr(exe, '/');
    if (!slash) goto fallback;
    *slash = '\0';

    /* Walk upward: check each level for the landmark */
    char dir[2048];
    snprintf(dir, sizeof(dir), "%s", exe);
    for (int i = 0; i < 10; i++) {
        char test[8192];
        snprintf(test, sizeof(test), "%s/narthex/include/talmud_util.h", dir);
        if (access(test, F_OK) == 0) {
            snprintf(g_talmud_dir, sizeof(g_talmud_dir), "%s", dir);
            return 0;
        }
        slash = strrchr(dir, '/');
        if (!slash) goto fallback;
        *slash = '\0';
    }

fallback:
    snprintf(g_talmud_dir, sizeof(g_talmud_dir), "%s", TALMUD_SRC_DIR);
    return 0;
}

#endif /* TALMUD_UTIL_NEED_DIR */

/*
 * Read an entire file into a malloc'd buffer.
 * Caller must free(*out_buf) when done.
 * Returns 0 on success, -1 on error.
 */
static inline __attribute__((unused))
int read_file_alloc(const char *path, void **out_buf, size_t *out_size) {
    *out_buf = NULL;
    *out_size = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    long len = ftell(fp);
    if (len < 0) { fclose(fp); return -1; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return -1; }

    void *buf = malloc((size_t)len);
    if (!buf) { fclose(fp); return -1; }

    size_t nread = fread(buf, 1, (size_t)len, fp);
    fclose(fp);

    if ((long)nread != len) { free(buf); return -1; }

    *out_buf = buf;
    *out_size = (size_t)len;
    return 0;
}

/*
 * C parsing helpers used by darshan.
 * Heuristic function detection, brace scanning, parameter counting,
 * and the canonical list of hand-written source files.
 *
 * Define TALMUD_UTIL_NEED_CPARSE before including to enable.
 * Requires <ctype.h> (caller must include it).
 */
#ifdef TALMUD_UTIL_NEED_CPARSE

#ifndef TALMUD_CPARSE_MAX_LINE
#define TALMUD_CPARSE_MAX_LINE 4096
#endif

/* Hand-written C files */
static const char *HAND_WRITTEN[] __attribute__((unused)) = {
    "../talmud.c",
    "profane/darshan/darshan.c",
    "narthex/sofer/sofer.c",
    "narthex/yotzer/yotzer.c",
    NULL
};

/*
 * Heuristic: detect C function definitions.
 *
 * A function definition line:
 *   - Starts at column 0 (no leading whitespace)
 *   - Is not a preprocessor directive (#)
 *   - Is not a comment
 *   - Contains a '(' character
 *   - The identifier before '(' is the function name
 *   - May be preceded by return type, static, inline, etc.
 *   - The '{' appears on this line or the next
 *
 * Returns 1 if a function def was found, populating name/is_static.
 */
static inline __attribute__((unused))
int parse_func_def(const char *line, const char *next_line,
                   char *name, size_t namesz, int *is_static) {
    /* Skip empty or preprocessor lines */
    if (line[0] == '\0' || line[0] == '#' || line[0] == '/')
        return 0;

    /* Must start at column 0 (not indented) */
    if (line[0] == ' ' || line[0] == '\t')
        return 0;

    /* Skip string literal lines (common in talmud const arrays) */
    if (line[0] == '"')
        return 0;

    /* Must contain '(' */
    const char *paren = strchr(line, '(');
    if (!paren) return 0;

    /* Must NOT be a typedef, struct, enum, or macro call */
    if (strncmp(line, "typedef ", 8) == 0) return 0;
    if (strncmp(line, "struct ",  7) == 0) return 0;
    if (strncmp(line, "enum ",    5) == 0) return 0;
    if (strncmp(line, "union ",   6) == 0) return 0;

    /* Must have '{' on this line or next */
    int has_brace = (strchr(line, '{') != NULL);
    if (!has_brace && next_line)
        has_brace = (next_line[0] == '{' || strstr(next_line, "{") != NULL);
    if (!has_brace) return 0;

    /* Check for static */
    *is_static = (strstr(line, "static ") != NULL && strstr(line, "static ") < paren);

    /* Extract function name: the identifier immediately before '(' */
    const char *end = paren;
    while (end > line && *(end - 1) == ' ') end--;
    const char *start = end;
    while (start > line && (isalnum((unsigned char)*(start - 1)) || *(start - 1) == '_'))
        start--;

    if (start == end) return 0;

    size_t len = (size_t)(end - start);
    if (len >= namesz) len = namesz - 1;
    memcpy(name, start, len);
    name[len] = '\0';

    /* Function names must start with a letter or underscore */
    if (!isalpha((unsigned char)name[0]) && name[0] != '_')
        return 0;

    /* Skip common non-function patterns */
    if (strcmp(name, "if") == 0 || strcmp(name, "for") == 0 ||
        strcmp(name, "while") == 0 || strcmp(name, "switch") == 0 ||
        strcmp(name, "return") == 0 || strcmp(name, "sizeof") == 0)
        return 0;

    return 1;
}

/*
 * Scan one line for brace depth changes, respecting strings, char
 * literals, block comments, and line comments. Tracks block-comment
 * state across lines via *in_comment.
 *
 * Returns the new brace depth. Writes the highest depth reached on
 * this line to *max_out if non-NULL.
 */
static inline __attribute__((unused))
int scan_braces(const char *line, int *in_comment,
                int depth, int *max_out) {
    int max_d = depth;
    for (const char *p = line; *p; p++) {
        if (*in_comment) {
            if (p[0] == '*' && p[1] == '/') {
                *in_comment = 0;
                p++;
            }
            continue;
        }
        if (p[0] == '/' && p[1] == '*') { *in_comment = 1; p++; continue; }
        if (p[0] == '/' && p[1] == '/') break;
        if (*p == '"') {
            for (p++; *p && *p != '"'; p++)
                if (*p == '\\' && p[1]) p++;
            if (!*p) break;
            continue;
        }
        if (*p == '\'') {
            for (p++; *p && *p != '\''; p++)
                if (*p == '\\' && p[1]) p++;
            if (!*p) break;
            continue;
        }
        if (*p == '{') { depth++; if (depth > max_d) max_d = depth; }
        else if (*p == '}') { depth--; }
    }
    if (max_out) *max_out = max_d;
    return depth;
}

/*
 * Count parameters in a function definition line.
 * Counts commas between the outermost parens, +1 if non-void.
 */
static inline __attribute__((unused))
int count_params(const char *line) {
    const char *open = strchr(line, '(');
    if (!open) return 0;
    open++;

    while (*open == ' ' || *open == '\t') open++;

    if (strncmp(open, "void)", 5) == 0 || *open == ')')
        return 0;

    int params = 1;
    int depth = 0;
    for (const char *p = open; *p && !(*p == ')' && depth == 0); p++) {
        if (*p == '(') depth++;
        else if (*p == ')') depth--;
        else if (*p == ',' && depth == 0) params++;
    }
    return params;
}

#endif /* TALMUD_UTIL_NEED_CPARSE */

#endif /* TALMUD_UTIL_H */
