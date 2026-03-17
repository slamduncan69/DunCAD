#ifndef DC_SEXPR_H
#define DC_SEXPR_H

/*
 * sexpr.h — Generic s-expression parser for KiCad file formats.
 *
 * KiCad 6+ uses s-expressions for all file formats (.kicad_sch, .kicad_pcb,
 * .kicad_sym, .kicad_mod). This module provides:
 *   - Parsing s-expressions into an AST (DC_Sexpr tree)
 *   - Writing an AST back to text (roundtrip)
 *   - Query helpers for navigating the tree
 *
 * Ownership: dc_sexpr_parse() returns a heap-allocated tree. The caller must
 * free it with dc_sexpr_free(). All strings within the tree are owned by
 * the tree and freed recursively.
 *
 * No external dependencies — only libc.
 */

#include "core/error.h"
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Node types
 * ---------------------------------------------------------------------- */
typedef enum {
    DC_SEXPR_ATOM,    /* unquoted token: e.g. kicad_sch, 1.27, F.Cu */
    DC_SEXPR_STRING,  /* quoted string: e.g. "Device:R_Small" */
    DC_SEXPR_LIST     /* parenthesized list: (tag child1 child2 ...) */
} DC_SexprType;

/* -------------------------------------------------------------------------
 * DC_Sexpr — a single node in the s-expression tree.
 *
 * For ATOM/STRING: value is the text content, children/count are 0/NULL.
 * For LIST: value is NULL, children is an array of child nodes.
 * ---------------------------------------------------------------------- */
typedef struct DC_Sexpr {
    DC_SexprType      type;
    char             *value;       /* atom or string text; NULL for list */
    struct DC_Sexpr **children;    /* child array; NULL for atom/string */
    size_t            child_count; /* number of children */
    size_t            child_cap;   /* allocated capacity */
    int               line;        /* source line for error reporting */
} DC_Sexpr;

/* -------------------------------------------------------------------------
 * dc_sexpr_parse — parse s-expression text into an AST.
 *
 * Parameters:
 *   text — NUL-terminated s-expression source
 *   err  — output error; may be NULL
 *
 * Returns: root node (always a LIST), or NULL on error.
 * Ownership: caller must free with dc_sexpr_free().
 * ---------------------------------------------------------------------- */
DC_Sexpr *dc_sexpr_parse(const char *text, DC_Error *err);

/* -------------------------------------------------------------------------
 * dc_sexpr_free — recursively free an s-expression tree.
 *
 * Parameters:
 *   node — may be NULL (no-op)
 * ---------------------------------------------------------------------- */
void dc_sexpr_free(DC_Sexpr *node);

/* -------------------------------------------------------------------------
 * dc_sexpr_write — serialize an AST back to text.
 *
 * Parameters:
 *   node — the node to serialize; must not be NULL
 *   err  — output error; may be NULL
 *
 * Returns: malloc'd string, or NULL on error.
 * Ownership: caller must free() the returned string.
 * ---------------------------------------------------------------------- */
char *dc_sexpr_write(const DC_Sexpr *node, DC_Error *err);

/* -------------------------------------------------------------------------
 * Query helpers — navigate the tree without manual iteration.
 * ---------------------------------------------------------------------- */

/* Find the first child list whose tag (first atom) matches `tag`.
 * Returns NULL if not found. Returned pointer is borrowed. */
DC_Sexpr *dc_sexpr_find(const DC_Sexpr *parent, const char *tag);

/* Find all child lists matching `tag`. Returns count via out_count.
 * Returns a malloc'd array of borrowed pointers, or NULL if none.
 * Caller must free() the array (not the nodes). */
DC_Sexpr **dc_sexpr_find_all(const DC_Sexpr *parent, const char *tag,
                              size_t *out_count);

/* Get the tag (first atom) of a list node. Returns NULL if not a list
 * or if the first child is not an atom. Returned pointer is borrowed. */
const char *dc_sexpr_tag(const DC_Sexpr *node);

/* Get the value of the Nth child (0-based, after the tag). Returns NULL
 * if out of bounds or if the child is a list. Borrowed pointer. */
const char *dc_sexpr_value_at(const DC_Sexpr *node, size_t index);

/* Get the value of the first atom/string child after the tag.
 * Convenience for the common (tag value) pattern. Borrowed pointer. */
const char *dc_sexpr_value(const DC_Sexpr *node);

/* Count direct children of a list node. Returns 0 for atom/string. */
size_t dc_sexpr_child_count(const DC_Sexpr *node);

#endif /* DC_SEXPR_H */
