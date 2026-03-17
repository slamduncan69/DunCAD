#define _POSIX_C_SOURCE 200809L

#include "sch_symbol_render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================================
 * Symbol body rendering — generic box + pin stubs
 *
 * When the library has the symbol definition, we could parse the draw
 * primitives. For now, we draw a generic rectangle with pin stubs.
 * ========================================================================= */

#define SYM_BODY_W  80.0   /* mils */
#define SYM_BODY_H  60.0   /* mils */
#define PIN_LEN     30.0   /* mils */

void
dc_sch_symbol_render(cairo_t *cr,
                      DC_SchCanvas *canvas,
                      DC_SchSymbol *sym,
                      DC_ELibrary *lib,
                      int selected)
{
    if (!cr || !canvas || !sym) return;
    (void)lib; /* Future: parse actual symbol graphics from library */

    double cx, cy;
    dc_sch_canvas_world_to_screen(canvas, sym->x, sym->y, &cx, &cy);

    double zoom = dc_sch_canvas_get_zoom(canvas);
    double bw = SYM_BODY_W * zoom;
    double bh = SYM_BODY_H * zoom;
    double pin = PIN_LEN * zoom;

    /* Body rectangle */
    if (selected) {
        cairo_set_source_rgb(cr, 0.3, 0.8, 1.0);
    } else {
        cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
    }
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, cx - bw / 2, cy - bh / 2, bw, bh);
    cairo_stroke(cr);

    /* Pin stubs — left and right */
    cairo_set_source_rgb(cr, 0.0, 0.6, 0.0);
    cairo_set_line_width(cr, 1.5);

    /* Left pin */
    cairo_move_to(cr, cx - bw / 2 - pin, cy);
    cairo_line_to(cr, cx - bw / 2, cy);
    cairo_stroke(cr);

    /* Right pin */
    cairo_move_to(cr, cx + bw / 2, cy);
    cairo_line_to(cr, cx + bw / 2 + pin, cy);
    cairo_stroke(cr);

    /* If we have pin data, draw more stubs */
    if (sym->pins) {
        size_t n_pins = dc_array_length(sym->pins);
        for (size_t i = 0; i < n_pins && i < 8; i++) {
            DC_SchPin *p = dc_array_get(sym->pins, i);
            double px, py;
            dc_sch_canvas_world_to_screen(canvas, p->x, p->y, &px, &py);

            cairo_set_source_rgb(cr, 0.0, 0.6, 0.0);
            cairo_arc(cr, px, py, 3.0, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    /* Reference text (above body) */
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.3);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);
    double font_size = 11.0;
    if (zoom > 1.5) font_size = 11.0 * (zoom / 1.5);
    if (font_size > 20.0) font_size = 20.0;
    cairo_set_font_size(cr, font_size);

    if (sym->reference) {
        cairo_move_to(cr, cx - bw / 2, cy - bh / 2 - 4);
        cairo_show_text(cr, sym->reference);
    }

    /* Value text (below body) */
    const char *val = dc_eschematic_symbol_property(sym, "Value");
    if (val) {
        cairo_set_source_rgb(cr, 0.6, 0.6, 0.9);
        cairo_set_font_size(cr, font_size * 0.85);
        cairo_move_to(cr, cx - bw / 2, cy + bh / 2 + font_size + 2);
        cairo_show_text(cr, val);
    }
}
