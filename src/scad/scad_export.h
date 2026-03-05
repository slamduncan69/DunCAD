#ifndef DC_SCAD_EXPORT_H
#define DC_SCAD_EXPORT_H

/*
 * scad_export.h — OpenSCAD code generation from bezier spans.
 *
 * Generates two files per export:
 *   1. A shape .scad file with span data and module definitions
 *   2. A companion library (duncad_bezier.scad) with De Casteljau evaluation
 *
 * Spans are arbitrary-degree sequences of DC_Point2 control points,
 * matching the editor's juncture-delimited representation.
 *
 * Ownership:
 *   - dc_scad_generate() and dc_scad_generate_library() return heap-allocated
 *     strings; caller must free().
 *   - dc_scad_spans_free() frees a span array and all owned point data.
 */

#include "bezier/bezier_curve.h"   /* DC_Point2 */
#include "core/error.h"

/* -------------------------------------------------------------------------
 * DC_ScadSpan — one bezier span (arbitrary degree)
 * ---------------------------------------------------------------------- */
typedef struct {
    DC_Point2 *points;   /* owned copy of control points */
    int        count;
} DC_ScadSpan;

/* -------------------------------------------------------------------------
 * dc_scad_generate — produce shape .scad source as a string.
 *
 * Parameters:
 *   name      — shape identifier (used for variable/module names)
 *   spans     — array of spans
 *   num_spans — number of spans
 *   closed    — 1 if shape is closed (uses polygon()), 0 for open polyline
 *   err       — error output (may be NULL)
 *
 * Returns: heap-allocated string; caller must free(). NULL on error.
 * ---------------------------------------------------------------------- */
char *dc_scad_generate(const char *name, const DC_ScadSpan *spans,
                       int num_spans, int closed, DC_Error *err);

/* -------------------------------------------------------------------------
 * dc_scad_generate_library — produce duncad_bezier.scad as a string.
 *
 * Returns: heap-allocated string; caller must free(). NULL on alloc failure.
 * ---------------------------------------------------------------------- */
char *dc_scad_generate_library(void);

/* -------------------------------------------------------------------------
 * dc_scad_export — write shape .scad and companion library to disk.
 *
 * Writes <path> as the shape file and duncad_bezier.scad in the same
 * directory.
 *
 * Parameters:
 *   path      — output file path for the shape .scad
 *   name      — shape identifier
 *   spans     — array of spans
 *   num_spans — number of spans
 *   closed    — 1 if closed shape
 *   err       — error output (may be NULL)
 *
 * Returns: 0 on success, -1 on error.
 * ---------------------------------------------------------------------- */
int dc_scad_export(const char *path, const char *name,
                   const DC_ScadSpan *spans, int num_spans,
                   int closed, DC_Error *err);

/* -------------------------------------------------------------------------
 * dc_scad_spans_free — free a span array and all owned point data.
 *
 * Parameters:
 *   spans     — array to free (may be NULL)
 *   num_spans — number of spans
 * ---------------------------------------------------------------------- */
void dc_scad_spans_free(DC_ScadSpan *spans, int num_spans);

#endif /* DC_SCAD_EXPORT_H */
