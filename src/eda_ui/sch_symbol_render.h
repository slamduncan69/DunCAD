#ifndef DC_SCH_SYMBOL_RENDER_H
#define DC_SCH_SYMBOL_RENDER_H

/*
 * sch_symbol_render.h — Render KiCad symbol instances to Cairo.
 *
 * Draws pin stubs, body rectangle, reference/value text.
 * Symbols are rendered at their world position; the caller has already
 * set up the canvas coordinate transform.
 */

#include <cairo.h>
#include "eda/eda_schematic.h"
#include "eda/eda_library.h"
#include "sch_canvas.h"

/* Render a single symbol instance.
 * Uses the canvas for world→screen coordinate transforms.
 * If selected is non-zero, draws with highlight colors. */
void dc_sch_symbol_render(cairo_t *cr,
                           DC_SchCanvas *canvas,
                           DC_SchSymbol *sym,
                           DC_ELibrary *lib,
                           int selected);

/* Render a symbol definition as a standalone preview.
 * Fits the symbol bbox into the rectangle (x,y,w,h) in Cairo user coords.
 * No SchCanvas needed — uses a simple affine fit.
 * lib may be NULL; if non-NULL, used to resolve (extends "...") references. */
void dc_sch_symbol_render_preview(cairo_t *cr, const DC_Sexpr *sym_def,
                                    double x, double y, double w, double h);

/* Same as above but with library for resolving extends. */
void dc_sch_symbol_render_preview_ex(cairo_t *cr, const DC_Sexpr *sym_def,
                                       DC_ELibrary *lib,
                                       double x, double y, double w, double h);

#endif /* DC_SCH_SYMBOL_RENDER_H */
