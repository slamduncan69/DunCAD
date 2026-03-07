#ifndef DC_SCAD_SPLITTER_H
#define DC_SCAD_SPLITTER_H

/*
 * scad_splitter.h — Split OpenSCAD source into top-level statements.
 *
 * Parses SCAD text by counting braces/parens to find top-level
 * statement boundaries. Each statement gets a line range so we can
 * map viewport objects back to source code.
 *
 * No GTK dependency — pure C with libc only.
 */

#include <stddef.h>

typedef struct {
    char *text;       /* owned: the statement text */
    int   line_start; /* 1-based first line */
    int   line_end;   /* 1-based last line (inclusive) */
} DC_ScadStatement;

typedef struct {
    DC_ScadStatement *stmts; /* owned array */
    int               count;
    int               capacity;
} DC_ScadStatements;

/* Split SCAD source into top-level statements.
 * Returns NULL on failure. Caller must free with dc_scad_stmts_free(). */
DC_ScadStatements *dc_scad_split(const char *source);

/* Free a statement list. Safe with NULL. */
void dc_scad_stmts_free(DC_ScadStatements *ss);

#endif /* DC_SCAD_SPLITTER_H */
