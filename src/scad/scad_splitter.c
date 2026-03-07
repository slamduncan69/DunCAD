#include "scad/scad_splitter.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * Top-level SCAD statement splitter.
 *
 * Strategy: scan character-by-character, tracking brace/paren depth,
 * string literals, and comments. A top-level statement ends when:
 *   - We hit a semicolon at depth 0
 *   - We close a brace that returns depth to 0
 *   - We hit end-of-file
 *
 * Each statement records its line range (1-based, inclusive).
 * ---------------------------------------------------------------------- */

static void
push_stmt(DC_ScadStatements *ss, const char *start, size_t len,
          int line_start, int line_end)
{
    /* Skip pure whitespace / empty statements */
    const char *p = start;
    const char *end = start + len;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end) return;

    if (ss->count >= ss->capacity) {
        int newcap = ss->capacity ? ss->capacity * 2 : 8;
        DC_ScadStatement *tmp = realloc(ss->stmts,
            (size_t)newcap * sizeof(DC_ScadStatement));
        if (!tmp) return;
        ss->stmts = tmp;
        ss->capacity = newcap;
    }

    DC_ScadStatement *s = &ss->stmts[ss->count];
    s->text = malloc(len + 1);
    if (!s->text) return;
    memcpy(s->text, start, len);
    s->text[len] = '\0';
    s->line_start = line_start;
    s->line_end = line_end;
    ss->count++;
}

DC_ScadStatements *
dc_scad_split(const char *source)
{
    if (!source) return NULL;

    DC_ScadStatements *ss = calloc(1, sizeof(*ss));
    if (!ss) return NULL;

    int depth = 0;       /* brace depth */
    int line = 1;        /* current line number */
    int stmt_start_line = 1;
    const char *stmt_start = source;
    const char *p = source;
    int in_string = 0;   /* inside "..." */
    int in_line_comment = 0;
    int in_block_comment = 0;

    while (*p) {
        char c = *p;

        /* Track line numbers */
        if (c == '\n') {
            line++;
            in_line_comment = 0;
            p++;
            continue;
        }

        /* Inside comments — skip */
        if (in_line_comment) {
            p++;
            continue;
        }
        if (in_block_comment) {
            if (c == '*' && p[1] == '/') {
                in_block_comment = 0;
                p += 2;
            } else {
                p++;
            }
            continue;
        }

        /* Inside string literal */
        if (in_string) {
            if (c == '\\' && p[1]) {
                p += 2; /* skip escape */
            } else if (c == '"') {
                in_string = 0;
                p++;
            } else {
                p++;
            }
            continue;
        }

        /* Detect comment starts */
        if (c == '/' && p[1] == '/') {
            in_line_comment = 1;
            p += 2;
            continue;
        }
        if (c == '/' && p[1] == '*') {
            in_block_comment = 1;
            p += 2;
            continue;
        }

        /* String start */
        if (c == '"') {
            in_string = 1;
            p++;
            continue;
        }

        /* Brace/paren tracking */
        if (c == '{' || c == '(') {
            depth++;
            p++;
            continue;
        }
        if (c == ')') {
            /* Closing paren does NOT end a statement — transforms like
             * translate([x,y,z]) cube(...); need the whole line. */
            if (depth > 0) depth--;
            p++;
            continue;
        }
        if (c == '}') {
            depth--;
            if (depth <= 0) {
                depth = 0;
                /* End of top-level block — include this closing brace */
                p++;
                /* Skip trailing whitespace on same line */
                while (*p && *p != '\n' && isspace((unsigned char)*p)) p++;
                /* If there's a semicolon right after closing brace, include it */
                if (*p == ';') p++;

                push_stmt(ss, stmt_start, (size_t)(p - stmt_start),
                          stmt_start_line, line);

                /* Skip whitespace to next statement */
                while (*p && isspace((unsigned char)*p)) {
                    if (*p == '\n') line++;
                    p++;
                }
                stmt_start = p;
                stmt_start_line = line;
            } else {
                p++;
            }
            continue;
        }

        /* Semicolon at depth 0 = end of simple statement */
        if (c == ';' && depth == 0) {
            p++; /* include the semicolon */
            push_stmt(ss, stmt_start, (size_t)(p - stmt_start),
                      stmt_start_line, line);

            while (*p && isspace((unsigned char)*p)) {
                if (*p == '\n') line++;
                p++;
            }
            stmt_start = p;
            stmt_start_line = line;
            continue;
        }

        p++;
    }

    /* Remaining text (if any) */
    if (p > stmt_start) {
        push_stmt(ss, stmt_start, (size_t)(p - stmt_start),
                  stmt_start_line, line);
    }

    return ss;
}

void
dc_scad_stmts_free(DC_ScadStatements *ss)
{
    if (!ss) return;
    for (int i = 0; i < ss->count; i++)
        free(ss->stmts[i].text);
    free(ss->stmts);
    free(ss);
}
