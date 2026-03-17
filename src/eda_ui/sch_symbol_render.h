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

#endif /* DC_SCH_SYMBOL_RENDER_H */
