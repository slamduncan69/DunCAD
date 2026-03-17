#ifndef DC_PCB_FOOTPRINT_RENDER_H
#define DC_PCB_FOOTPRINT_RENDER_H

/*
 * pcb_footprint_render.h — Render KiCad footprint definitions to Cairo.
 *
 * Standalone preview renderer: parses footprint sexpr (pad, fp_line,
 * fp_rect, fp_circle, fp_arc) and draws with layer-colored rendering.
 * No PcbCanvas needed — uses a simple bbox-to-rect affine fit.
 */

#include <cairo.h>
#include "eda/sexpr.h"

/* Render a footprint definition as a standalone preview.
 * Fits the footprint bbox into the rectangle (x,y,w,h) in Cairo user coords.
 * Layer colors: F.Cu red, B.Cu blue, F.SilkS white, courtyard yellow. */
void dc_pcb_footprint_render_preview(cairo_t *cr, const DC_Sexpr *fp_def,
                                       double x, double y, double w, double h);

#endif /* DC_PCB_FOOTPRINT_RENDER_H */
