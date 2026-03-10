/*
 * sofer — Sacred Text Scribe
 *
 * Programmatic manipulation of the talmud.c node tree.
 * Adds, removes, and bulk-purges HELP_ constants and TREE[] entries.
 *
 * Hebrew: sofer = a scribe who writes Torah scrolls.
 *
 * Usage:
 *   sofer purge <prefix>           Remove all nodes matching path prefix
 *   sofer add <path> <title>       Add a node (reads body from stdin)
 *   sofer count [prefix]           Count nodes (optionally filtered)
 *   sofer ls [prefix]              List node paths (optionally filtered)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#define TALMUD_UTIL_NEED_DIR
#include "talmud_util.h"

#define MAX_NODES   2048
#define MAX_LINE    8192
#define MAX_LINES   32768
#define MAX_BODY    65536
#define MAX_FMTLINE 16384

/* ----------------------------------------------------------------
 * File I/O: read talmud.c into an array of lines
 * ---------------------------------------------------------------- */

static char *g_lines[MAX_LINES];
static int   g_nlines;
static char  g_src_path[4096];

static int load_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); return -1; }

    char buf[MAX_LINE];
    g_nlines = 0;
    while (fgets(buf, sizeof(buf), fp) && g_nlines < MAX_LINES) {
        g_lines[g_nlines] = strdup(buf);
        if (!g_lines[g_nlines]) { fclose(fp); return -1; }
        g_nlines++;
    }
    fclose(fp);
    snprintf(g_src_path, sizeof(g_src_path), "%s", path);
    return 0;
}

static int save_file(void) {
    FILE *fp = fopen(g_src_path, "w");
    if (!fp) { perror(g_src_path); return -1; }
    for (int i = 0; i < g_nlines; i++)
        fputs(g_lines[i], fp);
    fclose(fp);
    printf("Wrote %s (%d lines)\n", g_src_path, g_nlines);
    return 0;
}

static void delete_line(int idx) {
    free(g_lines[idx]);
    for (int i = idx; i < g_nlines - 1; i++)
        g_lines[i] = g_lines[i + 1];
    g_nlines--;
}

static void delete_range(int start, int end) {
    /* Delete lines [start, end) — end exclusive */
    for (int i = start; i < end && i < g_nlines; i++)
        free(g_lines[i]);
    int removed = (end < g_nlines) ? end - start : g_nlines - start;
    for (int i = start; i + removed < g_nlines; i++)
        g_lines[i] = g_lines[i + removed];
    g_nlines -= removed;
}

static int insert_line(int idx, const char *text) {
    if (g_nlines >= MAX_LINES) return -1;
    for (int i = g_nlines; i > idx; i--)
        g_lines[i] = g_lines[i - 1];
    g_lines[idx] = strdup(text);
    g_nlines++;
    return 0;
}

/* ----------------------------------------------------------------
 * TREE[] parsing: find { "path", HELP_NAME } entries
 * ---------------------------------------------------------------- */

struct tree_entry {
    char path[512];
    char help[512];
    int  line;  /* 0-indexed line number */
};

static struct tree_entry g_entries[MAX_NODES];
static int               g_nentries;

static void parse_tree_entries(void) {
    g_nentries = 0;
    for (int i = 0; i < g_nlines && g_nentries < MAX_NODES; i++) {
        const char *p = g_lines[i];
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '{') continue;
        p++;
        while (*p == ' ') p++;
        if (*p != '"') continue;
        p++;

        /* Extract path */
        const char *path_start = p;
        while (*p && *p != '"') p++;
        if (*p != '"') continue;
        size_t plen = (size_t)(p - path_start);

        p++; /* skip closing quote */
        while (*p == ' ' || *p == ',') p++;

        /* Extract HELP_ name */
        if (strncmp(p, "HELP_", 5) != 0) continue;
        const char *help_start = p;
        while (*p && *p != ' ' && *p != '}' && *p != '\n') p++;
        size_t hlen = (size_t)(p - help_start);

        struct tree_entry *e = &g_entries[g_nentries];
        if (plen >= sizeof(e->path)) plen = sizeof(e->path) - 1;
        if (hlen >= sizeof(e->help)) hlen = sizeof(e->help) - 1;
        memcpy(e->path, path_start, plen);
        e->path[plen] = '\0';
        memcpy(e->help, help_start, hlen);
        e->help[hlen] = '\0';
        e->line = i;
        g_nentries++;
    }
}

/* ----------------------------------------------------------------
 * HELP_ constant finder: locate the full block for a HELP_NAME
 * ---------------------------------------------------------------- */

/* Find the line range [start, end) for:
 *   static const char HELP_NAME[] = "..."; (possibly multi-line)
 * Also gobbles preceding comment blocks and blank lines. */
static int find_help_range(const char *help_name, int *out_start, int *out_end) {
    /* Build the pattern: "static const char HELP_NAME[]" */
    char pattern[600];
    snprintf(pattern, sizeof(pattern), "static const char %s[]", help_name);
    size_t plen = strlen(pattern);

    int def_line = -1;
    for (int i = 0; i < g_nlines; i++) {
        if (strncmp(g_lines[i], pattern, plen) == 0) {
            def_line = i;
            break;
        }
    }
    if (def_line < 0) return -1;

    /* Find end: line ending with '";' */
    int end = def_line;
    for (int i = def_line; i < g_nlines; i++) {
        size_t len = strlen(g_lines[i]);
        /* Strip trailing newline for checking */
        while (len > 0 && (g_lines[i][len - 1] == '\n' || g_lines[i][len - 1] == '\r'))
            len--;
        if (len >= 2 && g_lines[i][len - 2] == '"' && g_lines[i][len - 1] == ';') {
            end = i + 1;
            break;
        }
    }

    /* Gobble preceding blank lines */
    int start = def_line;
    while (start > 0 && g_lines[start - 1][0] == '\n')
        start--;

    /* Gobble preceding comment block */
    if (start > 0) {
        int j = start - 1;
        /* Check for closing star-slash on this line */
        const char *line = g_lines[j];
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r')) ll--;
        if (ll >= 2 && line[ll-2] == '*' && line[ll-1] == '/') {
            /* Walk back to find opening comment */
            while (j > 0) {
                if (strstr(g_lines[j], "/*") != NULL) {
                    start = j;
                    break;
                }
                j--;
            }
        }
    }

    /* Gobble blank lines before the comment too */
    while (start > 0 && g_lines[start - 1][0] == '\n')
        start--;

    *out_start = start;
    *out_end = end;
    return 0;
}

/* ----------------------------------------------------------------
 * Path matching
 * ---------------------------------------------------------------- */

static int path_matches(const char *entry_path, const char *prefix) {
    if (strcmp(entry_path, prefix) == 0) return 1;
    size_t plen = strlen(prefix);
    if (strncmp(entry_path, prefix, plen) == 0 && entry_path[plen] == '.')
        return 1;
    return 0;
}

/* ----------------------------------------------------------------
 * Path to HELP_ name conversion
 * ---------------------------------------------------------------- */

static void path_to_help_name(const char *path, char *out, size_t outsz) {
    snprintf(out, outsz, "HELP_");
    size_t pos = 5;
    for (const char *p = path; *p && pos < outsz - 1; p++) {
        if (*p == '.' || *p == '-' || *p == ' ')
            out[pos++] = '_';
        else
            out[pos++] = (char)toupper((unsigned char)*p);
    }
    out[pos] = '\0';
}

/* Normalize path: convert spaces to dots (so "reference doctrine x" -> "reference.doctrine.x") */
static void normalize_path(const char *in, char *out, size_t outsz) {
    size_t i = 0;
    for (; *in && i < outsz - 1; in++, i++)
        out[i] = (*in == ' ') ? '.' : *in;
    out[i] = '\0';
}

/* ----------------------------------------------------------------
 * Commands
 * ---------------------------------------------------------------- */

static int cmd_count(const char *prefix) {
    parse_tree_entries();
    int count = 0;
    for (int i = 0; i < g_nentries; i++) {
        if (!prefix || path_matches(g_entries[i].path, prefix))
            count++;
    }
    if (prefix)
        printf("%d node(s) matching '%s'\n", count, prefix);
    else
        printf("%d node(s)\n", count);
    return 0;
}

static int cmd_ls(const char *prefix) {
    parse_tree_entries();
    int count = 0;
    for (int i = 0; i < g_nentries; i++) {
        if (!prefix || path_matches(g_entries[i].path, prefix)) {
            printf("  %-50s (%s)\n", g_entries[i].path, g_entries[i].help);
            count++;
        }
    }
    printf("\n%d node(s)\n", count);
    return 0;
}

static int cmd_purge(const char *prefix) {
    parse_tree_entries();

    /* Collect victims */
    struct tree_entry *victims[MAX_NODES];
    int nvictims = 0;
    for (int i = 0; i < g_nentries; i++) {
        if (path_matches(g_entries[i].path, prefix))
            victims[nvictims++] = &g_entries[i];
    }

    if (nvictims == 0) {
        printf("No nodes match prefix '%s'\n", prefix);
        return 0;
    }

    printf("Purging %d node(s) matching '%s':\n", nvictims, prefix);
    for (int i = 0; i < nvictims; i++)
        printf("  - %s  (%s)\n", victims[i]->path, victims[i]->help);

    /*
     * Collect all line ranges to delete.
     * We collect (start, end) pairs, sort them, then delete
     * in reverse order to preserve indices.
     */
    struct range { int start; int end; };
    struct range ranges[MAX_NODES * 2];
    int nranges = 0;

    /* 1. TREE[] entry lines */
    for (int i = 0; i < nvictims; i++) {
        ranges[nranges].start = victims[i]->line;
        ranges[nranges].end   = victims[i]->line + 1;
        nranges++;
    }

    /* 2. HELP_ constant blocks (deduplicated) */
    char seen[MAX_NODES][512];
    int nseen = 0;
    for (int i = 0; i < nvictims; i++) {
        /* Check if we already found this HELP_ name */
        int dup = 0;
        for (int j = 0; j < nseen; j++) {
            if (strcmp(seen[j], victims[i]->help) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        snprintf(seen[nseen], sizeof(seen[nseen]), "%s", victims[i]->help);
        nseen++;

        int s, e;
        if (find_help_range(victims[i]->help, &s, &e) == 0) {
            ranges[nranges].start = s;
            ranges[nranges].end   = e;
            nranges++;
        } else {
            fprintf(stderr, "  WARNING: could not find constant %s\n",
                    victims[i]->help);
        }
    }

    /* Sort ranges by start line */
    for (int i = 0; i < nranges - 1; i++) {
        for (int j = i + 1; j < nranges; j++) {
            if (ranges[j].start < ranges[i].start) {
                struct range tmp = ranges[i];
                ranges[i] = ranges[j];
                ranges[j] = tmp;
            }
        }
    }

    /* Merge overlapping ranges */
    struct range merged[MAX_NODES * 2];
    int nmerged = 0;
    for (int i = 0; i < nranges; i++) {
        if (nmerged > 0 && ranges[i].start <= merged[nmerged - 1].end) {
            if (ranges[i].end > merged[nmerged - 1].end)
                merged[nmerged - 1].end = ranges[i].end;
        } else {
            merged[nmerged++] = ranges[i];
        }
    }

    /* Delete in reverse order */
    int total_deleted = 0;
    for (int i = nmerged - 1; i >= 0; i--) {
        int count = merged[i].end - merged[i].start;
        delete_range(merged[i].start, merged[i].end);
        total_deleted += count;
    }

    /* Clean up excessive blank lines (3+ consecutive -> 2) */
    int blanks = 0;
    for (int i = 0; i < g_nlines; ) {
        if (g_lines[i][0] == '\n') {
            blanks++;
            if (blanks > 2) {
                delete_line(i);
                continue;
            }
        } else {
            blanks = 0;
        }
        i++;
    }

    printf("\nDeleted %d lines (%d nodes)\n", total_deleted, nvictims);
    return save_file();
}

static int cmd_add(const char *raw_path, const char *title) {
    char path_buf[512];
    normalize_path(raw_path, path_buf, sizeof(path_buf));
    const char *path = path_buf;

    parse_tree_entries();

    /* Check for duplicates */
    for (int i = 0; i < g_nentries; i++) {
        if (strcmp(g_entries[i].path, path) == 0) {
            fprintf(stderr, "ERROR: node '%s' already exists\n", path);
            return 1;
        }
    }

    /* Read body from stdin */
    char body[MAX_BODY];
    size_t total = 0;
    while (total < sizeof(body) - 1) {
        size_t n = fread(body + total, 1, sizeof(body) - 1 - total, stdin);
        if (n == 0) break;
        total += n;
    }
    body[total] = '\0';

    /* Strip trailing newlines */
    while (total > 0 && body[total - 1] == '\n')
        body[--total] = '\0';

    /* Build HELP_ constant name */
    char help_name[512];
    path_to_help_name(path, help_name, sizeof(help_name));

    /* Find insertion point for the constant:
     * After the last sibling's HELP_ constant block */
    char parent[512];
    snprintf(parent, sizeof(parent), "%s", path);
    char *dot = strrchr(parent, '.');
    if (dot) *dot = '\0';
    else parent[0] = '\0';

    int insert_const = -1;
    if (parent[0]) {
        /* Find all entries under the parent, get the last one's HELP_ block */
        for (int i = g_nentries - 1; i >= 0; i--) {
            if (path_matches(g_entries[i].path, parent)) {
                int s, e;
                if (find_help_range(g_entries[i].help, &s, &e) == 0)
                    insert_const = e;
                break;
            }
        }
    }

    /* Fallback: find TREE[] array start and insert before it */
    if (insert_const < 0) {
        for (int i = 0; i < g_nlines; i++) {
            if (strstr(g_lines[i], "help_node") && strstr(g_lines[i], "TREE[]")) {
                insert_const = i;
                break;
            }
        }
    }

    if (insert_const < 0) {
        fprintf(stderr, "ERROR: cannot find insertion point\n");
        return 1;
    }

    /* Build the constant lines and insert them */
    /* First: blank line separator */
    insert_line(insert_const++, "\n");

    /* static const char HELP_NAME[] = */
    char decl[1024];
    snprintf(decl, sizeof(decl), "static const char %s[] =\n", help_name);
    insert_line(insert_const++, decl);

    /* Title line */
    char line_buf[MAX_FMTLINE];
    snprintf(line_buf, sizeof(line_buf), "\"%s\\n\"\n", title);
    insert_line(insert_const++, line_buf);

    /* Blank separator */
    insert_line(insert_const++, "\"\\n\"\n");

    /* Body lines */
    const char *p = body;
    const char *last_line_start = p;
    while (*p) {
        if (*p == '\n') {
            size_t len = (size_t)(p - last_line_start);
            /* Escape quotes and backslashes */
            char escaped[MAX_LINE];
            size_t epos = 0;
            for (size_t j = 0; j < len && epos < sizeof(escaped) - 4; j++) {
                char c = last_line_start[j];
                if (c == '"' || c == '\\') {
                    escaped[epos++] = '\\';
                }
                escaped[epos++] = c;
            }
            escaped[epos] = '\0';

            snprintf(line_buf, sizeof(line_buf), "\"%s\\n\"\n", escaped);
            insert_line(insert_const++, line_buf);
            last_line_start = p + 1;
        }
        p++;
    }

    /* Handle last line (no trailing newline) */
    if (last_line_start < p) {
        size_t len = (size_t)(p - last_line_start);
        char escaped[MAX_LINE];
        size_t epos = 0;
        for (size_t j = 0; j < len && epos < sizeof(escaped) - 4; j++) {
            char c = last_line_start[j];
            if (c == '"' || c == '\\') {
                escaped[epos++] = '\\';
            }
            escaped[epos++] = c;
        }
        escaped[epos] = '\0';

        snprintf(line_buf, sizeof(line_buf), "\"%s\\n\";\n", escaped);
        insert_line(insert_const++, line_buf);
    } else {
        /* The last string line needs to end with "; instead of just " */
        /* Find the last inserted line and fix its ending */
        int last = insert_const - 1;
        char *ll = g_lines[last];
        size_t ll_len = strlen(ll);
        /* Replace trailing "\n" with ";\n" */
        /* Find the last quote before \n */
        if (ll_len >= 2 && ll[ll_len - 1] == '\n' && ll[ll_len - 2] == '"') {
            /* Already ends with "\n, just need to add ; */
            char fixed[MAX_LINE];
            memcpy(fixed, ll, ll_len - 1);
            fixed[ll_len - 1] = ';';
            fixed[ll_len] = '\n';
            fixed[ll_len + 1] = '\0';
            free(g_lines[last]);
            g_lines[last] = strdup(fixed);
        }
    }

    /* Now find the TREE[] insertion point.
     * Re-parse because we modified the file. */
    parse_tree_entries();

    int tree_insert = -1;
    if (parent[0]) {
        for (int i = g_nentries - 1; i >= 0; i--) {
            if (path_matches(g_entries[i].path, parent)) {
                tree_insert = g_entries[i].line + 1;
                break;
            }
        }
    }

    /* Fallback: before the NULL sentinel in TREE[] */
    if (tree_insert < 0) {
        for (int i = 0; i < g_nlines; i++) {
            if (strstr(g_lines[i], "{ NULL") &&
                strstr(g_lines[i], "NULL }")) {
                tree_insert = i;
                break;
            }
        }
    }

    if (tree_insert < 0) {
        fprintf(stderr, "ERROR: cannot find TREE[] insertion point\n");
        return 1;
    }

    char tree_line[2048];
    snprintf(tree_line, sizeof(tree_line),
             "    { \"%s\", %s },\n", path, help_name);
    insert_line(tree_insert, tree_line);

    printf("Added node: %s (%s)\n", path, help_name);
    return save_file();
}

/* ----------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------- */

static void print_usage(void) {
    printf("sofer — Sacred Text Scribe\n\n");
    printf("Usage:\n");
    printf("  sofer purge <prefix>        Remove all nodes matching prefix\n");
    printf("  sofer add <path> <title>    Add a node (reads body from stdin)\n");
    printf("  sofer count [prefix]        Count nodes\n");
    printf("  sofer ls [prefix]           List node paths\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { print_usage(); return 1; }

    if (resolve_talmud_dir() != 0) {
        fprintf(stderr, "sofer: could not resolve directory\n");
        return 1;
    }

    /* Find talmud.c: one level up from talmud/ */
    char talmud_path[4096];
    snprintf(talmud_path, sizeof(talmud_path), "%s/../talmud.c", g_talmud_dir);

    if (load_file(talmud_path) != 0) {
        fprintf(stderr, "sofer: could not load %s\n", talmud_path);
        return 1;
    }

    if (strcmp(argv[1], "count") == 0) {
        return cmd_count(argc > 2 ? argv[2] : NULL);
    }
    if (strcmp(argv[1], "ls") == 0) {
        return cmd_ls(argc > 2 ? argv[2] : NULL);
    }
    if (strcmp(argv[1], "purge") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: sofer purge <prefix>\n");
            return 1;
        }
        return cmd_purge(argv[2]);
    }
    if (strcmp(argv[1], "add") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: sofer add <path> <title>\n");
            fprintf(stderr, "  Reads body from stdin\n");
            return 1;
        }
        return cmd_add(argv[2], argv[3]);
    }

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    print_usage();
    return 1;
}
