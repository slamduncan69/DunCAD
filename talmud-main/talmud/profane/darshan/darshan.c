/*
 * darshan — Codebase Investigation & Refactoring Scribe
 *
 * Exhaustive, algorithmic code analysis for agents. The agent is the
 * brain; darshan is the search. Traces every caller, every callee,
 * every string reference. Misses nothing.
 *
 * Subcommands:
 *   darshan deps <func>         Callers, callees, metrics
 *   darshan deps <file.c>       File-level dependency map
 *   darshan refs <string> ...   Find all references to strings
 *   darshan replace 'old' 'new' Search-and-replace (--yes, --preview, --diff)
 *   darshan seal <file> ...     Touch, verify talmud.c, rebuild
 *
 * Build:  yotzer all
 * Run:    darshan deps resolve_talmud_dir
 *         darshan refs 'talmud_util'
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <glob.h>

#define TALMUD_UTIL_NEED_DIR
#define TALMUD_UTIL_NEED_CPARSE
#include "talmud_util.h"

static int resolve_dirs(void) {
    return resolve_talmud_dir();
}

/* Max line length */
#define MAX_LINE TALMUD_CPARSE_MAX_LINE

/*
 * Measure a function body in a single pass: line count + max nesting.
 * Reads from the current file position. The definition line is the
 * line containing the function signature (may or may not contain '{').
 */
struct func_metrics { int lines; int max_nesting; };

static struct func_metrics measure_function(FILE *fp, const char *def_line) {
    struct func_metrics m = {0, 0};
    int depth = 0, in_comment = 0, max_depth = 0, line_max = 0;

    depth = scan_braces(def_line, &in_comment, 0, &max_depth);
    if (depth > 0) m.lines = 1;

    char buf[MAX_LINE];
    while (fgets(buf, sizeof(buf), fp)) {
        m.lines++;
        depth = scan_braces(buf, &in_comment, depth, &line_max);
        if (line_max > max_depth) max_depth = line_max;
        if (depth <= 0) break;
    }

    m.max_nesting = max_depth > 0 ? max_depth - 1 : 0;
    return m;
}

/* ================================================================
 * FUNCTION INDEX — codebase-wide function registry
 * ================================================================ */

struct func_entry {
    char name[256];
    char file[4096];  /* relative to talmud/ */
    int  line;
    int  is_static;
};

#define MAX_FUNCS 4096

static struct func_entry g_func_index[MAX_FUNCS];
static int g_n_funcs = 0;
static int g_n_indexed_files = 0;

static const char *HEADER_FILES[] = {
    "narthex/include/talmud_util.h",
    NULL
};

static void build_func_index(void) {
    g_n_funcs = 0;
    g_n_indexed_files = 0;
    const char **lists[] = {HAND_WRITTEN, HEADER_FILES, NULL};
    for (int l = 0; lists[l]; l++)
    for (int i = 0; lists[l][i]; i++) {
        char fullpath[8192];
        snprintf(fullpath, sizeof(fullpath), "%s/%s",
                 g_talmud_dir, lists[l][i]);
        FILE *fp = fopen(fullpath, "r");
        if (!fp) continue;
        g_n_indexed_files++;

        char lbuf[2][MAX_LINE];
        int cur = 0, lineno = 0;
        if (!fgets(lbuf[0], MAX_LINE, fp)) { fclose(fp); continue; }
        lineno = 1;

        while (1) {
            int next = 1 - cur;
            int have_next = (fgets(lbuf[next], MAX_LINE, fp) != NULL);
            char *next_line = have_next ? lbuf[next] : NULL;

            char name[256];
            int is_static = 0;
            if (parse_func_def(lbuf[cur], next_line,
                               name, sizeof(name), &is_static)) {
                if (g_n_funcs < MAX_FUNCS) {
                    struct func_entry *e = &g_func_index[g_n_funcs++];
                    snprintf(e->name, sizeof(e->name), "%s", name);
                    snprintf(e->file, sizeof(e->file), "%s", lists[l][i]);
                    e->line = lineno;
                    e->is_static = is_static;
                }
            }

            if (!have_next) break;
            cur = next;
            lineno++;
        }
        fclose(fp);
    }
}

/* Find which function contains a given line in a file */
static const char *containing_func(const char *file, int line) {
    const char *best = NULL;
    int best_line = 0;
    for (int i = 0; i < g_n_funcs; i++) {
        if (strcmp(g_func_index[i].file, file) == 0 &&
            g_func_index[i].line <= line &&
            g_func_index[i].line > best_line) {
            best = g_func_index[i].name;
            best_line = g_func_index[i].line;
        }
    }
    return best ? best : "?";
}

/* ================================================================
 * SHARED HELPERS — callee scanning, dedup, index lookup
 * ================================================================ */

struct ident_hit { const char *start; size_t len; };

/*
 * Scan a line for identifier( patterns, skipping strings and char
 * literals. Returns the number of hits found (up to max_hits).
 * Hits point into the original line buffer.
 */
static int scan_line_idents(const char *line, struct ident_hit *hits,
                            int max_hits) {
    int n = 0;
    for (const char *p = line; *p && n < max_hits; p++) {
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
        if (!isalpha((unsigned char)*p) && *p != '_') continue;
        const char *s = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
        size_t ilen = (size_t)(p - s);
        const char *after = p;
        while (*after == ' ' || *after == '\t') after++;
        if (*after != '(') { p--; continue; }
        hits[n].start = s;
        hits[n].len = ilen;
        n++;
        p--;
    }
    return n;
}

/* Check if name (length-delimited) already appears in names[0..count). */
static int already_seen(const char **names, int count,
                        const char *s, size_t len) {
    for (int k = 0; k < count; k++) {
        if (strlen(names[k]) == len && memcmp(names[k], s, len) == 0)
            return 1;
    }
    return 0;
}

/*
 * Find a function in the index by name (length-delimited).
 * skip_file: if non-NULL, skip entries from this file.
 * prefer_file: if non-NULL, return immediately on match from this file.
 */
static struct func_entry *find_func_by_name(const char *name, size_t len,
                                            const char *skip_file,
                                            const char *prefer_file) {
    struct func_entry *best = NULL;
    for (int i = 0; i < g_n_funcs; i++) {
        if (strlen(g_func_index[i].name) != len) continue;
        if (memcmp(g_func_index[i].name, name, len) != 0) continue;
        if (skip_file && strcmp(g_func_index[i].file, skip_file) == 0)
            continue;
        if (prefer_file && strcmp(g_func_index[i].file, prefer_file) == 0)
            return &g_func_index[i];
        if (!best) best = &g_func_index[i];
    }
    return best;
}

/* ================================================================
 * DEPS — dependency analysis
 * ================================================================ */

static int cmd_deps_file(const char *target);

static int cmd_deps(const char *target) {
    /* Dispatch: file path or function name? */
    if (strstr(target, ".c") || strstr(target, ".h") || strchr(target, '/'))
        return cmd_deps_file(target);

    build_func_index();

    /* Find all definitions matching the target name */
    struct func_entry *defs[16];
    int n_defs = 0;
    for (int i = 0; i < g_n_funcs && n_defs < 16; i++) {
        if (strcmp(g_func_index[i].name, target) == 0)
            defs[n_defs++] = &g_func_index[i];
    }

    if (n_defs == 0) {
        fprintf(stderr, "  '%s' not found (%d functions indexed)\n",
                target, g_n_funcs);
        return 1;
    }

    struct func_entry *def = defs[0];

    /* --- DEFINITION --- */
    printf("\n  \033[1;37mDEFINITION\033[0m\n");
    if (n_defs > 1) {
        for (int i = 0; i < n_defs; i++)
            printf("    %s[%d] %s:%d  %s%s()\033[0m\n",
                   i == 0 ? "\033[1;37m" : "\033[0;90m",
                   i + 1, defs[i]->file, defs[i]->line,
                   defs[i]->is_static ? "static " : "",
                   defs[i]->name);
        printf("    \033[0;90m(showing deps for [1])\033[0m\n");
    }

    /* Read the definition line for metrics */
    char fullpath[8192];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_talmud_dir, def->file);
    FILE *fp = fopen(fullpath, "r");
    char def_line_buf[MAX_LINE] = {0};
    int body_lines = 0, max_nest = 0, params = 0;
    if (fp) {
        char skip[MAX_LINE];
        for (int i = 1; i < def->line; i++)
            if (!fgets(skip, sizeof(skip), fp)) break;
        if (fgets(def_line_buf, sizeof(def_line_buf), fp)) {
            struct func_metrics fm = measure_function(fp, def_line_buf);
            body_lines = fm.lines;
            max_nest = fm.max_nesting;
            params = count_params(def_line_buf);
        }
        fclose(fp);
    }

    printf("    %s:%d  %s%s()",
           def->file, def->line,
           def->is_static ? "static " : "", def->name);
    if (body_lines > 0) printf("  %d lines", body_lines);
    if (params > 0) printf("  %d params", params);
    if (max_nest > 0) printf("  nest=%d", max_nest);
    printf("\n");

    /* --- CALLERS --- */
    printf("\n  \033[1;37mCALLERS\033[0m\n");

    char cmd[32768], out[65536];
    if (def->is_static) {
        /* Static function: only search the same file */
        snprintf(cmd, sizeof(cmd),
            "grep -rnwH '%s' '%s/%s' 2>/dev/null",
            target, g_talmud_dir, def->file);
    } else {
        snprintf(cmd, sizeof(cmd),
            "grep -rnw '%s' '%s' --include='*.c' --include='*.h' 2>/dev/null | "
            "grep -v '/sacred/fae/' | grep -v '/sacred/eaf/' | "
            "grep -v '/sacred/bala/' | grep -v '/sacred/shroud/' | "
            "grep -v '/pleroma/'",
            target, g_talmud_dir);
    }
    run_capture(cmd, out, sizeof(out));

    /* Parse grep output lines */
    int n_callers = 0;
    size_t dirlen = strlen(g_talmud_dir);
    char *saveptr = NULL;
    char *gline = strtok_r(out, "\n", &saveptr);
    while (gline) {
        /* Format: /full/path/file.c:123:content */
        char *c1 = strchr(gline, ':');
        if (!c1) { gline = strtok_r(NULL, "\n", &saveptr); continue; }
        *c1 = '\0';
        char *c2 = strchr(c1 + 1, ':');
        if (!c2) { gline = strtok_r(NULL, "\n", &saveptr); continue; }
        *c2 = '\0';

        const char *file = gline;
        if (strncmp(file, g_talmud_dir, dirlen) == 0 && file[dirlen] == '/')
            file += dirlen + 1;
        int lineno = atoi(c1 + 1);
        char *content = c2 + 1;
        while (*content == ' ' || *content == '\t') content++;
        size_t clen = strlen(content);
        while (clen > 0 && (content[clen-1] == '\n' || content[clen-1] == '\r'))
            content[--clen] = '\0';

        /* Skip the definition itself */
        if (strcmp(file, def->file) == 0 && lineno == def->line) {
            gline = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        const char *in_func = containing_func(file, lineno);
        if (strcmp(in_func, "?") == 0)
            printf("    %s:%d  \033[0;90m(file scope)\033[0m\n", file, lineno);
        else
            printf("    %s:%d  in \033[1;37m%s\033[0m()\n",
                   file, lineno, in_func);
        printf("      %s\n", content);
        n_callers++;

        gline = strtok_r(NULL, "\n", &saveptr);
    }

    if (n_callers == 0)
        printf("    \033[0;90m(none)\033[0m\n");
    printf("    \033[0;90m— %d call sites\033[0m\n", n_callers);

    /* --- CALLEES --- */
    printf("\n  \033[1;37mCALLEES\033[0m\n");

    fp = fopen(fullpath, "r");
    int n_callees = 0;
    if (fp) {
        char skip[MAX_LINE];
        for (int i = 1; i < def->line; i++)
            if (!fgets(skip, sizeof(skip), fp)) break;

        char buf[MAX_LINE];
        if (!fgets(buf, sizeof(buf), fp)) { fclose(fp); goto callees_done; }

        /* Track callees by name (deduped) */
        const char *callee_names[512];
        const char *callee_files[512];
        int callee_lines[512];

        int depth = 0, in_comment = 0;
        depth = scan_braces(buf, &in_comment, 0, NULL);

        /* Scan function body for identifier( patterns */
        int scanning = 1;
        while (scanning) {
            struct ident_hit hits[64];
            int nh = scan_line_idents(buf, hits, 64);
            for (int h = 0; h < nh; h++) {
                /* Skip self-references */
                if (strlen(target) == hits[h].len &&
                    memcmp(target, hits[h].start, hits[h].len) == 0)
                    continue;
                if (already_seen(callee_names, n_callees,
                                 hits[h].start, hits[h].len))
                    continue;
                struct func_entry *best = find_func_by_name(
                    hits[h].start, hits[h].len, NULL, def->file);
                if (best && n_callees < 512) {
                    callee_names[n_callees] = best->name;
                    callee_files[n_callees] = best->file;
                    callee_lines[n_callees] = best->line;
                    n_callees++;
                }
            }

            if (!fgets(buf, sizeof(buf), fp)) break;
            depth = scan_braces(buf, &in_comment, depth, NULL);
            if (depth <= 0) break;
        }
        fclose(fp);

        for (int k = 0; k < n_callees; k++)
            printf("    \033[1;37m%s\033[0m()  %s:%d\n",
                   callee_names[k], callee_files[k], callee_lines[k]);
    }

callees_done:
    if (n_callees == 0)
        printf("    \033[0;90m(no codebase-internal callees)\033[0m\n");
    printf("    \033[0;90m— %d internal calls\033[0m\n", n_callees);

    printf("\n  \033[0;90mSEARCHED  %d functions indexed across %d files\n",
           g_n_funcs, g_n_indexed_files);
    printf("  CALLERS   %s -- whole-word grep, excluded generated code\n",
           def->is_static ? "in-file only (static)" : "codebase-wide");
    printf("  CALLEES   body scan, matched against function index\033[0m\n");

    printf("\n");
    return 0;
}

/* ================================================================
 * DEPS (file-level) — coupling analysis for a file
 * ================================================================ */

static int cmd_deps_file(const char *target) {
    build_func_index();

    /* Normalize: strip talmud/ prefix if present */
    const char *rel = target;
    if (strncmp(rel, "talmud/", 7) == 0) rel += 7;

    /* Verify file exists */
    char fullpath[8192];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_talmud_dir, rel);
    if (access(fullpath, F_OK) != 0) {
        fprintf(stderr, "  File not found: %s\n", rel);
        return 1;
    }

    /* Collect all functions defined in this file */
    struct func_entry *local[512];
    int n_local = 0;
    for (int i = 0; i < g_n_funcs && n_local < 512; i++) {
        if (strcmp(g_func_index[i].file, rel) == 0)
            local[n_local++] = &g_func_index[i];
    }

    printf("\n  \033[1;37mFILE\033[0m  %s  (%d functions)\n", rel, n_local);

    /* For each function: count external callers and classify */
    int n_exported = 0;    /* non-static, called from outside */
    int n_internal = 0;    /* static or only called within file */
    int n_external_deps = 0; /* unique external functions called */

    printf("\n  \033[1;37mFUNCTIONS\033[0m\n");
    for (int i = 0; i < n_local; i++) {
        struct func_entry *f = local[i];

        /* Count external callers (callers from OTHER files) */
        int ext_callers = 0;
        if (!f->is_static) {
            char cmd[32768], out[32768];
            snprintf(cmd, sizeof(cmd),
                "grep -rwl '%s' '%s' --include='*.c' --include='*.h' "
                "2>/dev/null | grep -v '/sacred/fae/' | grep -v '/sacred/eaf/' | "
                "grep -v '/sacred/bala/' | grep -v '/sacred/shroud/' | "
                "grep -v '/pleroma/' | grep -v '%s'",
                f->name, g_talmud_dir, rel);
            run_capture(cmd, out, sizeof(out));
            /* Count lines */
            if (out[0]) {
                ext_callers = 1;
                for (char *p = out; *p; p++)
                    if (*p == '\n') ext_callers++;
            }
        }

        if (ext_callers > 0) {
            printf("    \033[1;33m→\033[0m %-30s  %d external callers\n",
                   f->name, ext_callers);
            n_exported++;
        } else {
            printf("    \033[0;90m·\033[0m %-30s  %s\n",
                   f->name, f->is_static ? "static" : "internal");
            n_internal++;
        }
    }

    /* Find external dependencies: functions from OTHER files called by THIS file */
    printf("\n  \033[1;37mEXTERNAL DEPS\033[0m  (codebase functions this file calls)\n");

    /* Read the whole file, scan for callees not in this file */
    FILE *fp = fopen(fullpath, "r");
    const char *ext_names[512];
    const char *ext_files[512];

    if (fp) {
        char buf[MAX_LINE];
        while (fgets(buf, sizeof(buf), fp)) {
            struct ident_hit hits[64];
            int nh = scan_line_idents(buf, hits, 64);
            for (int h = 0; h < nh; h++) {
                if (already_seen(ext_names, n_external_deps,
                                 hits[h].start, hits[h].len))
                    continue;
                /* Is it a local function? Skip. */
                int is_local = 0;
                for (int k = 0; k < n_local; k++) {
                    if (strlen(local[k]->name) == hits[h].len &&
                        memcmp(local[k]->name, hits[h].start,
                               hits[h].len) == 0) {
                        is_local = 1;
                        break;
                    }
                }
                if (is_local) continue;
                struct func_entry *best = find_func_by_name(
                    hits[h].start, hits[h].len, rel, NULL);
                if (best && n_external_deps < 512) {
                    ext_names[n_external_deps] = best->name;
                    ext_files[n_external_deps] = best->file;
                    n_external_deps++;
                }
            }
        }
        fclose(fp);

        for (int k = 0; k < n_external_deps; k++)
            printf("    \033[1;37m%s\033[0m()  %s\n",
                   ext_names[k], ext_files[k]);
    }

    if (n_external_deps == 0)
        printf("    \033[0;90m(none)\033[0m\n");

    /* Summary */
    printf("\n  \033[1;37mCOUPLING\033[0m\n");
    printf("    functions:    %d total  (%d exported, %d internal)\n",
           n_local, n_exported, n_internal);
    printf("    fan-out:      %d external deps\n", n_external_deps);
    printf("    fan-in:       %d functions called from outside\n", n_exported);
    int coupling = n_exported + n_external_deps;
    printf("    coupling:     %d  %s\n", coupling,
           coupling <= 5 ? "\033[1;32mLOW\033[0m" :
           coupling <= 15 ? "\033[1;33mMODERATE\033[0m" :
           "\033[1;31mHIGH\033[0m");

    printf("\n  \033[0;90mSEARCHED  %d functions indexed across %d files -- whole-word grep per function\n",
           g_n_funcs, g_n_indexed_files);
    printf("  EXCLUDED  sacred/fae/ sacred/eaf/ sacred/bala/ sacred/shroud/ narthex/pleroma/\033[0m\n");

    printf("\n");
    return 0;
}

/* ================================================================
 * REFS — exhaustive string reference search
 * ================================================================ */

/*
 * Run the refs grep and return malloc'd output buffer (caller frees).
 * Returns NULL on no results or error.
 */
static char *run_refs_grep(int n_terms, const char **terms) {
    char cmd[32768];
    int pos = 0;

    char repo_root[4096];
    snprintf(repo_root, sizeof(repo_root), "%s", g_talmud_dir);
    dir_up(repo_root, 1);

    char pattern[4096];
    if (n_terms == 1) {
        snprintf(pattern, sizeof(pattern), "%s", terms[0]);
    } else {
        int ppos = 0;
        for (int i = 0; i < n_terms && ppos < 4000; i++) {
            if (i > 0) ppos += snprintf(pattern + ppos, sizeof(pattern) - (size_t)ppos, "\\|");
            ppos += snprintf(pattern + ppos, sizeof(pattern) - (size_t)ppos, "%s", terms[i]);
        }
    }

    pos = snprintf(cmd, sizeof(cmd),
        "grep -rnH --include='*.c' --include='*.h' --include='*.yotzer' "
        "--include='*.md' --include='*.txt' --include='.gitignore' "
        "--include='Makefile' "
        "'%s' '%s' 2>/dev/null; "
        "grep -rnH '%s' '%s/.gitignore' '%s/CLAUDE.md' 2>/dev/null",
        pattern, g_talmud_dir, pattern, repo_root, repo_root);
    (void)pos;

    char full_cmd[65536];
    snprintf(full_cmd, sizeof(full_cmd),
        "(%s) | grep -v '/sacred/fae/' | grep -v '/sacred/eaf/' | "
        "grep -v '/sacred/bala/' | grep -v '/sacred/shroud/' | "
        "grep -v '/pleroma/' | sort",
        cmd);

    char *out = malloc(262144);
    if (!out) return NULL;
    run_capture(full_cmd, out, 262144);

    if (!out[0]) {
        free(out);
        return NULL;
    }
    return out;
}

static int cmd_refs(int n_terms, const char **terms) {
    build_func_index();

    char *out = run_refs_grep(n_terms, terms);
    if (!out) {
        printf("\n  \033[0;90mNo references found.\033[0m\n\n");
        return 0;
    }

    /* Parse and group by file */
    printf("\n");
    size_t dirlen = strlen(g_talmud_dir);
    int total_hits = 0;
    int n_files = 0;
    char last_file[4096] = {0};
    char *saveptr = NULL;
    char *gline = strtok_r(out, "\n", &saveptr);

    while (gline) {
        char *c1 = strchr(gline, ':');
        if (!c1) { gline = strtok_r(NULL, "\n", &saveptr); continue; }
        *c1 = '\0';
        char *c2 = strchr(c1 + 1, ':');
        if (!c2) { gline = strtok_r(NULL, "\n", &saveptr); continue; }
        *c2 = '\0';

        const char *file = gline;
        if (strncmp(file, g_talmud_dir, dirlen) == 0 && file[dirlen] == '/')
            file += dirlen + 1;
        int lineno = atoi(c1 + 1);
        char *content = c2 + 1;
        while (*content == ' ' || *content == '\t') content++;
        /* Trim trailing whitespace */
        size_t clen = strlen(content);
        while (clen > 0 && (content[clen-1] == '\n' || content[clen-1] == '\r'
               || content[clen-1] == ' '))
            content[--clen] = '\0';
        /* Truncate very long lines */
        if (clen > 100) { content[97] = '.'; content[98] = '.'; content[99] = '.'; content[100] = '\0'; }

        /* New file? Print header */
        if (strcmp(file, last_file) != 0) {
            if (last_file[0]) printf("\n");
            printf("  \033[1;37m%s\033[0m\n", file);
            snprintf(last_file, sizeof(last_file), "%s", file);
            n_files++;
        }

        /* Find containing function (for C files) */
        const char *in_func = NULL;
        if (strstr(file, ".c") || strstr(file, ".h"))
            in_func = containing_func(file, lineno);

        if (in_func && strcmp(in_func, "?") != 0)
            printf("    %4d  in %-20s  %s\n", lineno, in_func, content);
        else
            printf("    %4d  %s\n", lineno, content);

        total_hits++;
        gline = strtok_r(NULL, "\n", &saveptr);
    }

    printf("\n  \033[0;90mSEARCHED  talmud/ -- *.c *.h *.yotzer *.md *.txt .gitignore Makefile\n");
    printf("            + .gitignore, CLAUDE.md (repo root)\n");
    printf("  EXCLUDED  sacred/fae/ sacred/eaf/ sacred/bala/ sacred/shroud/ narthex/pleroma/\n");
    printf("  TERMS     ");
    for (int t = 0; t < n_terms; t++) {
        if (t > 0) printf(" OR ");
        printf("'%s'", terms[t]);
    }
    printf("\n");
    printf("  RESULT    %d hits across %d files\033[0m\n\n", total_hits, n_files);

    free(out);
    return 0;
}

/* ================================================================
 * REPLACE — search-and-replace with interactive confirmation
 * ================================================================ */

struct replacement {
    char file[4096];      /* full path */
    char relfile[4096];   /* relative to talmud/ (for display) */
    int  line;            /* 1-indexed */
    char old_content[4096]; /* original line */
    char new_content[4096]; /* after substitution */
    int  status;          /* 0=pending, 1=accepted, -1=skipped */
};
#define MAX_REPLACEMENTS 4096

/*
 * Replace old_text with new_text in src, writing to dst (max dst_sz bytes).
 * When old_text is a substring of new_text, occurrences already embedded
 * within new_text are skipped to prevent double-replacement.
 * Returns 1 if any replacement was made, 0 otherwise.
 */
static int smart_replace(char *dst, size_t dst_sz, const char *src,
                         const char *old_text, const char *new_text) {
    size_t old_len = strlen(old_text);
    if (old_len == 0) {
        snprintf(dst, dst_sz, "%s", src);
        return 0;
    }
    size_t new_len = strlen(new_text);
    size_t src_len = strlen(src);
    const char *src_base = src;
    char *end = dst + dst_sz - 1;
    int changed = 0;

    /* When old is a substring of new, find where old appears in new.
     * For each hit of old in src, check if it's already part of new. */
    int check_embedded = (strstr(new_text, old_text) != NULL && new_len > old_len);

    while (*src && dst < end) {
        const char *hit = strstr(src, old_text);
        if (!hit) {
            while (*src && dst < end) *dst++ = *src++;
            break;
        }

        if (check_embedded) {
            /* Check if this hit is already part of a new_text occurrence.
             * old_text may appear at multiple offsets within new_text,
             * so check all of them. */
            int skip = 0;
            const char *scan = new_text;
            while ((scan = strstr(scan, old_text)) != NULL) {
                size_t offset = (size_t)(scan - new_text);
                const char *candidate = hit - offset;
                /* Bounds: candidate must be within [src_base, src_base+src_len) */
                if (candidate >= src_base &&
                    candidate + new_len <= src_base + src_len &&
                    memcmp(candidate, new_text, new_len) == 0) {
                    skip = 1;
                    break;
                }
                scan += old_len;
            }
            if (skip) {
                /* Copy this char and advance — don't replace */
                *dst++ = *src++;
                continue;
            }
        }

        /* Copy up to the hit */
        while (src < hit && dst < end) *dst++ = *src++;
        /* Copy replacement */
        for (size_t i = 0; i < new_len && dst < end; i++)
            *dst++ = new_text[i];
        src += old_len;
        changed = 1;
    }
    *dst = '\0';
    return changed;
}

/*
 * Build a replacement entry: substitute all occurrences of old_text
 * with new_text in content, storing result in r->new_content.
 * Returns 1 if line changed, 0 if not.
 */
static int build_replacement(struct replacement *r, const char *fullpath,
                             const char *relfile, int lineno,
                             const char *content, const char *old_text,
                             const char *new_text) {
    snprintf(r->file, sizeof(r->file), "%s", fullpath);
    snprintf(r->relfile, sizeof(r->relfile), "%s", relfile);
    r->line = lineno;
    snprintf(r->old_content, sizeof(r->old_content), "%s", content);
    r->status = 0;

    return smart_replace(r->new_content, sizeof(r->new_content),
                         content, old_text, new_text);
}

/*
 * Show a replacement with color highlighting.
 * Old text in red, new text in green, rest default.
 */
static void show_replacement(const struct replacement *r,
                             const char *old_text, const char *new_text) {
    size_t old_len = strlen(old_text);
    size_t new_len = strlen(new_text);

    /* Print old line with old_text highlighted red */
    printf("    %4d  \033[1;31m-\033[0m ", r->line);
    const char *src = r->old_content;
    if (old_len == 0) {
        printf("%s", src);
    } else {
        while (*src) {
            const char *hit = strstr(src, old_text);
            if (!hit) { printf("%s", src); break; }
            printf("%.*s\033[1;31m%s\033[0m", (int)(hit - src), src, old_text);
            src = hit + old_len;
        }
    }
    printf("\n");

    /* Print new line with new_text highlighted green */
    printf("    %4d  \033[1;32m+\033[0m ", r->line);
    src = r->new_content;
    if (new_len == 0) {
        printf("%s", src);
    } else {
        while (*src) {
            const char *hit = strstr(src, new_text);
            if (!hit) { printf("%s", src); break; }
            printf("%.*s\033[1;32m%s\033[0m", (int)(hit - src), src, new_text);
            src = hit + new_len;
        }
    }
    printf("\n");
}

/*
 * Interactive confirmation of replacements.
 * Groups by file. y=accept, n=skip, a=accept all, q=skip all.
 */
static void confirm_replacements(struct replacement *repls, int n,
                                 const char *old_text, const char *new_text) {
    char last_file[4096] = {0};

    for (int i = 0; i < n; i++) {
        if (repls[i].status != 0) continue; /* already decided */

        /* File header */
        if (strcmp(repls[i].relfile, last_file) != 0) {
            if (last_file[0]) printf("\n");
            printf("  \033[1;37m%s\033[0m\n", repls[i].relfile);
            snprintf(last_file, sizeof(last_file), "%s", repls[i].relfile);
        }

        show_replacement(&repls[i], old_text, new_text);
        printf("    \033[1;37m[y/n/a/q]\033[0m ");
        fflush(stdout);

        char input[64];
        if (!fgets(input, sizeof(input), stdin)) {
            printf("\n  Interrupted. Skipping all remaining.\n");
            for (int j = i; j < n; j++)
                if (repls[j].status == 0) repls[j].status = -1;
            break;
        }

        char ch = input[0];
        if (ch == 'y' || ch == 'Y') {
            repls[i].status = 1;
        } else if (ch == 'a' || ch == 'A') {
            repls[i].status = 1;
            for (int j = i + 1; j < n; j++)
                if (repls[j].status == 0) repls[j].status = 1;
            printf("    \033[1;32mAccepting all remaining.\033[0m\n");
            break;
        } else if (ch == 'q' || ch == 'Q') {
            for (int j = i; j < n; j++)
                if (repls[j].status == 0) repls[j].status = -1;
            printf("    Skipping all remaining.\n");
            break;
        } else {
            repls[i].status = -1;
        }
    }
}

/*
 * Apply accepted replacements to disk.
 * Groups by file, reads all lines, substitutes accepted ones, writes back.
 * Returns count of files modified.
 */
static int apply_replacements(struct replacement *repls, int n,
                              const char *old_text, const char *new_text) {
    int files_modified = 0;

    int i = 0;
    while (i < n) {
        /* Find all accepted replacements for this file */
        const char *cur_file = repls[i].file;
        int file_start = i;
        int any_accepted = 0;
        while (i < n && strcmp(repls[i].file, cur_file) == 0) {
            if (repls[i].status == 1) any_accepted = 1;
            i++;
        }
        if (!any_accepted) continue;

        /* Read entire file */
        FILE *fp = fopen(cur_file, "r");
        if (!fp) {
            printf("  \033[1;31m!\033[0m %s: vanished, skipping\n",
                   repls[file_start].relfile);
            continue;
        }

        char **lines = NULL;
        int n_lines = 0;
        int capacity = 0;
        char buf[MAX_LINE];

        while (fgets(buf, sizeof(buf), fp)) {
            if (n_lines >= capacity) {
                capacity = capacity ? capacity * 2 : 1024;
                lines = realloc(lines, (size_t)capacity * sizeof(char *));
            }
            lines[n_lines] = strdup(buf);
            n_lines++;
        }
        fclose(fp);

        /* Apply substitutions */
        int file_count = 0;
        for (int j = file_start; j < i; j++) {
            if (repls[j].status != 1) continue;
            int idx = repls[j].line - 1;
            if (idx < 0 || idx >= n_lines) continue;
            /* Validate line still contains old_text */
            if (!strstr(lines[idx], old_text)) {
                printf("  \033[1;33m!\033[0m %s:%d: line changed, skipping\n",
                       repls[j].relfile, repls[j].line);
                continue;
            }
            /* Build substituted line preserving trailing newline */
            char newline[8192];
            if (!smart_replace(newline, sizeof(newline), lines[idx],
                               old_text, new_text))
                continue; /* nothing to replace (already embedded) */

            free(lines[idx]);
            lines[idx] = strdup(newline);
            file_count++;
        }

        /* Write back */
        fp = fopen(cur_file, "w");
        if (!fp) {
            printf("  \033[1;31m!\033[0m %s: write failed\n",
                   repls[file_start].relfile);
            for (int k = 0; k < n_lines; k++) free(lines[k]);
            free(lines);
            continue;
        }
        for (int k = 0; k < n_lines; k++) {
            fputs(lines[k], fp);
            free(lines[k]);
        }
        fclose(fp);
        free(lines);

        printf("  \033[1;32m\xe2\x9c\x93\033[0m %s  %d replacement%s\n",
               repls[file_start].relfile, file_count,
               file_count == 1 ? "" : "s");
        files_modified++;
    }

    return files_modified;
}

static int cmd_replace(const char *old_text, const char *new_text,
                       int flag_confirm, int flag_preview, int flag_diff) {
    if (!old_text[0]) {
        fprintf(stderr, "darshan: empty old_text — refusing to replace.\n");
        return 1;
    }
    build_func_index();

    /* Header */
    printf("\n  \033[1;35mREPLACE\033[0m  '%s' -> '%s'\n", old_text, new_text);

    /* Search for old_text */
    const char *terms[] = { old_text };
    char *out = run_refs_grep(1, terms);
    if (!out) {
        printf("  \033[0;90mNo references found.\033[0m\n\n");
        return 0;
    }

    /* Parse grep output, build replacement entries */
    struct replacement *repls = calloc(MAX_REPLACEMENTS, sizeof(struct replacement));
    if (!repls) { free(out); return 1; }
    int n_repls = 0;

    size_t dirlen = strlen(g_talmud_dir);
    char *saveptr = NULL;
    char *gline = strtok_r(out, "\n", &saveptr);

    while (gline && n_repls < MAX_REPLACEMENTS) {
        char *c1 = strchr(gline, ':');
        if (!c1) { gline = strtok_r(NULL, "\n", &saveptr); continue; }
        *c1 = '\0';
        char *c2 = strchr(c1 + 1, ':');
        if (!c2) { gline = strtok_r(NULL, "\n", &saveptr); continue; }
        *c2 = '\0';

        const char *file = gline;
        const char *relfile = file;
        if (strncmp(file, g_talmud_dir, dirlen) == 0 && file[dirlen] == '/')
            relfile = file + dirlen + 1;
        int lineno = atoi(c1 + 1);
        char *content = c2 + 1;

        if (build_replacement(&repls[n_repls], file, relfile, lineno,
                              content, old_text, new_text)) {
            n_repls++;
        }

        gline = strtok_r(NULL, "\n", &saveptr);
    }
    free(out);

    if (n_repls == 0) {
        printf("  \033[0;90mAll references already in correct form.\033[0m\n\n");
        free(repls);
        return 0;
    }

    /* Count files */
    int n_files = 0;
    {
        char lf[4096] = {0};
        for (int i = 0; i < n_repls; i++) {
            if (strcmp(repls[i].relfile, lf) != 0) {
                n_files++;
                snprintf(lf, sizeof(lf), "%s", repls[i].relfile);
            }
        }
    }

    printf("  %d hit%s in %d file%s:\n\n",
           n_repls, n_repls == 1 ? "" : "s",
           n_files, n_files == 1 ? "" : "s");

    if (flag_diff || flag_preview) {
        /* Full diffs grouped by file */
        char last_file[4096] = {0};
        for (int i = 0; i < n_repls; i++) {
            if (strcmp(repls[i].relfile, last_file) != 0) {
                if (last_file[0]) printf("\n");
                printf("  \033[1;37m%s\033[0m\n", repls[i].relfile);
                snprintf(last_file, sizeof(last_file), "%s", repls[i].relfile);
            }
            show_replacement(&repls[i], old_text, new_text);
        }
    } else {
        /* Concise: file name + hit count */
        char last_file[4096] = {0};
        int file_hits = 0;
        for (int i = 0; i <= n_repls; i++) {
            if (i == n_repls || strcmp(repls[i].relfile, last_file) != 0) {
                if (last_file[0])
                    printf("    %-50s %d\n", last_file, file_hits);
                if (i < n_repls) {
                    snprintf(last_file, sizeof(last_file), "%s", repls[i].relfile);
                    file_hits = 1;
                }
            } else {
                file_hits++;
            }
        }
    }
    printf("\n");

    /* Preview mode: done */
    if (flag_preview) {
        printf("  \033[0;90m(dry run)\033[0m\n\n");
        free(repls);
        return 0;
    }

    /* Confirm mode, non-TTY auto-accept, or interactive */
    if (flag_confirm || !isatty(STDIN_FILENO)) {
        if (!flag_confirm)
            printf("  \033[0;90m(non-TTY: auto-accepting all)\033[0m\n");
        for (int i = 0; i < n_repls; i++)
            repls[i].status = 1;
    } else {
        confirm_replacements(repls, n_repls, old_text, new_text);
        printf("\n");
    }

    /* Count accepted/skipped */
    int accepted = 0, skipped = 0;
    for (int i = 0; i < n_repls; i++) {
        if (repls[i].status == 1) accepted++;
        else skipped++;
    }

    if (accepted == 0) {
        printf("  \033[0;90mNo replacements accepted.\033[0m\n\n");
        free(repls);
        return 0;
    }

    /* Apply */
    int files_modified = apply_replacements(repls, n_repls, old_text, new_text);

    /* Result line */
    printf("\n  \033[0;90m%d replaced, %d skipped, %d file%s\033[0m\n",
           accepted, skipped, files_modified,
           files_modified == 1 ? "" : "s");

    /* Verification: grep for old_text again.
     * When old is a substring of new, lines containing only the new form
     * are false positives — use smart_replace to detect genuine remainders. */
    const char *vterms[] = { old_text };
    char *vout = run_refs_grep(1, vterms);
    if (vout) {
        int remaining = 0;
        int check_embedded = (strstr(new_text, old_text) != NULL &&
                              strlen(new_text) > strlen(old_text));
        char *vs = NULL;
        char *vl = strtok_r(vout, "\n", &vs);
        while (vl) {
            if (check_embedded) {
                char *c1 = strchr(vl, ':');
                char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
                const char *content = c2 ? c2 + 1 : vl;
                char tmp[4096];
                if (smart_replace(tmp, sizeof(tmp), content,
                                  old_text, new_text))
                    remaining++;
            } else {
                remaining++;
            }
            vl = strtok_r(NULL, "\n", &vs);
        }
        free(vout);
        if (remaining > 0)
            printf("  \033[1;33m%d remaining\033[0m\n", remaining);
    }
    printf("\n");

    free(repls);
    return 0;
}

/* ================================================================
 * SEAL — Touch, verify talmud.c present, rebuild
 * ================================================================ */

static int cmd_seal(int argc, const char **files) {
    printf("\n\033[1;37m=== DARSHAN SEAL ===\033[0m\n");
    printf("  \033[0;90m(note: Claude Code may show this output twice — that is a display quirk, not a real error)\033[0m\n\n");

    /* --- Phase 1: Verify talmud.c is in the file list --- */
    int has_talmud = 0;
    for (int i = 0; i < argc; i++) {
        if (strstr(files[i], "talmud.c") || strstr(files[i], "narthex/talmud/talmud"))
            has_talmud = 1;
    }
    if (!has_talmud) {
        printf("  \033[31mREFUSED\033[0m  talmud.c not in file list\n\n"
               "  The documentation must be updated. Add talmud.c to seal:\n"
               "    darshan seal <your files> ../talmud.c\n\n");
        return 1;
    }

    /* --- Phase 2: Touch all files --- */
    printf("  \033[1;35m[1/3]\033[0m Touch\n");

    /* Repo root = g_talmud_dir minus trailing /talmud */
    char repo_root[4096];
    snprintf(repo_root, sizeof(repo_root), "%s", g_talmud_dir);
    dir_up(repo_root, 1);

    for (int i = 0; i < argc; i++) {
        char path[8192];
        if (files[i][0] == '/')
            snprintf(path, sizeof(path), "%s", files[i]);
        else if (strncmp(files[i], "../", 3) == 0)
            /* Paths above talmud/ like ../talmud.c */
            snprintf(path, sizeof(path), "%s/%s", g_talmud_dir, files[i]);
        else if (files[i][0] == '.' || strchr(files[i], '/') == NULL)
            /* Repo-root files like .gitignore or bare filenames */
            snprintf(path, sizeof(path), "%s/%s", repo_root, files[i]);
        else
            /* Paths under talmud/ like narthex/winnow/winnow.c */
            snprintf(path, sizeof(path), "%s/%s", g_talmud_dir, files[i]);

        FILE *f = fopen(path, "a");
        if (f) {
            fclose(f);
            printf("    touch %s\n", files[i]);
        } else {
            printf("    \033[31mFAIL\033[0m  %s — %s\n", files[i], strerror(errno));
            printf("          (resolved to: %s)\n", path);
            return 1;
        }
    }
    printf("\n");

    /* --- Phase 3: Rebuild (compact preview) --- */
    printf("  \033[1;35m[2/2]\033[0m Build\n");
    char build_cmd[8192];
    snprintf(build_cmd, sizeof(build_cmd), "%s/narthex/yotzer/yotzer all 2>&1", g_talmud_dir);
    static char build_out[262144];
    int ret = run_capture(build_cmd, build_out, sizeof(build_out));

    /* Collect interesting lines: OK (new builds), -> (installs), FAIL/error */
    const char *interesting[64];
    int n_interesting = 0;
    int has_fail = 0;
    {
        char *line = build_out;
        while (line && *line && n_interesting < 64) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (strstr(line, "FAIL") || strstr(line, "error") ||
                strstr(line, "warning")) {
                interesting[n_interesting++] = line;
                has_fail = 1;
            } else if ((strstr(line, "OK") || strstr(line, "->")) &&
                       !strstr(line, "up to date")) {
                interesting[n_interesting++] = line;
            }
            if (nl) { *nl = '\0'; line = nl + 1; } else break;
        }
    }

    if (n_interesting == 0) {
        printf("    all up to date\n");
    } else if (n_interesting <= 6 || has_fail) {
        /* On failure: show everything so the error isn't hidden */
        for (int i = 0; i < n_interesting; i++)
            printf("    %s\n", interesting[i]);
    } else {
        for (int i = 0; i < 3; i++)
            printf("    %s\n", interesting[i]);
        printf("    \033[0;90m... %d more ...\033[0m\n", n_interesting - 6);
        for (int i = n_interesting - 3; i < n_interesting; i++)
            printf("    %s\n", interesting[i]);
    }

    if (ret != 0 || has_fail) {
        printf("\n  \033[31mCONDEMNED\033[0m  Build failed\n\n");
        return 1;
    }
    printf("\n");

    printf("\n  \033[32mSEALED\033[0m\n\n");
    return 0;
}

/* ================================================================
 * MAIN
 * ================================================================ */

static void print_help(void) {
    fprintf(stderr,
        "\033[1;36m"
        "  ╔═══════════════════════════════════════╗\n"
        "  ║         DARSHAN — THE SCRIBE          ║\n"
        "  ║   Investigate. Expose. Refactor.       ║\n"
        "  ╚═══════════════════════════════════════╝\n"
        "\033[0m\n"
        "  \033[1;37mCOMMANDS:\033[0m\n"
        "    darshan deps <func>         Callers, callees, metrics\n"
        "    darshan deps <file.c>       File-level dependency map\n"
        "    darshan refs <str> ...      Find all references to strings\n"
        "    darshan replace 'old' 'new' Search-and-replace (--yes, --preview, --diff)\n"
        "    darshan seal <files> ...    Touch, rebuild (talmud.c required)\n"
        "\n"
        "  \033[1;37mSEAL:\033[0m\n"
        "    Certify your changes. Touches files, rebuilds with yotzer.\n"
        "    Refuses unless talmud.c is in the file list —\n"
        "    documentation is not optional.\n"
        "\n"
        "  \033[1;37mREFS:\033[0m\n"
        "    Better grep. Searches all code, configs, docs for string\n"
        "    references. Groups by file, shows containing function.\n"
        "    Multiple terms = OR. Excludes generated code.\n"
        "\n"
        "  \033[1;37mDEPS:\033[0m\n"
        "    Exhaustive dependency analysis. Shows definition, metrics,\n"
        "    every caller with context, every codebase-internal callee.\n"
        "\n"
        "  \033[1;37mREPLACE:\033[0m\n"
        "    Search-and-replace. Default: concise (file + count).\n"
        "    --diff/-d shows full before/after diffs per line.\n"
        "    --preview is dry run (implies --diff). --yes/-y auto-accepts.\n"
        "\n"
        "  \033[0;90mThe agent is the brain. This tool is the search.\033[0m\n"
        "\n");
}

int main(int argc, char *argv[]) {
    if (resolve_dirs() != 0) {
        fprintf(stderr, "Could not resolve talmud directory\n");
        return 1;
    }

    if (argc < 2) {
        print_help();
        return 1;
    }

    /* Subcommand: deps */
    if (strcmp(argv[1], "deps") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: darshan deps <function_name|file.c>\n");
            return 1;
        }
        return cmd_deps(argv[2]);
    }

    /* Subcommand: refs */
    if (strcmp(argv[1], "refs") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: darshan refs <string> [string2] ...\n");
            return 1;
        }
        return cmd_refs(argc - 2, (const char **)(argv + 2));
    }

    /* Subcommand: replace */
    if (strcmp(argv[1], "replace") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: darshan replace 'old' 'new' [--yes] [--preview] [--diff]\n");
            return 1;
        }
        int fc = 0, fp = 0, fd = 0;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--yes") == 0 || strcmp(argv[i], "-y") == 0) fc = 1;
            if (strcmp(argv[i], "--preview") == 0) fp = 1;
            if (strcmp(argv[i], "--diff") == 0 || strcmp(argv[i], "-d") == 0) fd = 1;
        }
        return cmd_replace(argv[2], argv[3], fc, fp, fd);
    }

    /* Subcommand: seal */
    if (strcmp(argv[1], "seal") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: darshan seal <file1> <file2> ...\n"
                            "  Files changed. talmud.c MUST be included.\n"
                            "  Touches files, rebuilds (yotzer all).\n");
            return 1;
        }
        return cmd_seal(argc - 2, (const char **)(argv + 2));
    }

    /* Unknown command */
    print_help();
    return 1;
}
