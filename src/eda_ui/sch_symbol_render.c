#define _POSIX_C_SOURCE 200809L

#include "sch_symbol_render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* KiCad symbol coords are mm; schematic coords are mils. 1mm = 39.37 mils */
#define MM_TO_MILS 39.3701

/* =========================================================================
 * Sexpr helper: parse float from atom at child index (0-based after tag)
 * ========================================================================= */
static double
sexpr_float(const DC_Sexpr *node, size_t idx)
{
    const char *v = dc_sexpr_value_at(node, idx);
    return v ? atof(v) : 0.0;
}

/* Parse (xy x y) node */
static void
parse_xy(const DC_Sexpr *xy, double *x, double *y)
{
    if (!xy || dc_sexpr_child_count(xy) < 3) { *x = *y = 0; return; }
    *x = sexpr_float(xy, 0);
    *y = sexpr_float(xy, 1);
}

/* =========================================================================
 * Transform: apply symbol position, rotation, mirror
 * KiCad coords are mm, we convert to mils for the schematic canvas.
 * ========================================================================= */
static void
sym_transform(DC_SchSymbol *sym, double lx, double ly,
              double *ox, double *oy)
{
    /* Convert mm to mils */
    double mx = lx * MM_TO_MILS;
    double my = ly * MM_TO_MILS;

    /* Mirror */
    if (sym->mirror) mx = -mx;

    /* Rotate */
    double rad = sym->angle * M_PI / 180.0;
    double rx = mx * cos(rad) - my * sin(rad);
    double ry = mx * sin(rad) + my * cos(rad);

    /* Translate to symbol position */
    *ox = sym->x + rx;
    *oy = sym->y + ry;
}

/* =========================================================================
 * Render primitives from sexpr
 * ========================================================================= */
static void
render_rectangle(cairo_t *cr, DC_SchCanvas *canvas, DC_SchSymbol *sym,
                 const DC_Sexpr *rect)
{
    DC_Sexpr *start = dc_sexpr_find(rect, "start");
    DC_Sexpr *end = dc_sexpr_find(rect, "end");
    if (!start || !end) return;

    double x1, y1, x2, y2;
    parse_xy(start, &x1, &y1);
    parse_xy(end, &x2, &y2);

    double sx1, sy1, sx2, sy2, sx3, sy3, sx4, sy4;
    double wx, wy;

    sym_transform(sym, x1, y1, &wx, &wy);
    dc_sch_canvas_world_to_screen(canvas, wx, wy, &sx1, &sy1);

    sym_transform(sym, x2, y1, &wx, &wy);
    dc_sch_canvas_world_to_screen(canvas, wx, wy, &sx2, &sy2);

    sym_transform(sym, x2, y2, &wx, &wy);
    dc_sch_canvas_world_to_screen(canvas, wx, wy, &sx3, &sy3);

    sym_transform(sym, x1, y2, &wx, &wy);
    dc_sch_canvas_world_to_screen(canvas, wx, wy, &sx4, &sy4);

    cairo_move_to(cr, sx1, sy1);
    cairo_line_to(cr, sx2, sy2);
    cairo_line_to(cr, sx3, sy3);
    cairo_line_to(cr, sx4, sy4);
    cairo_close_path(cr);

    /* Check fill */
    DC_Sexpr *fill = dc_sexpr_find(rect, "fill");
    if (fill) {
        const char *ftype = NULL;
        DC_Sexpr *ft = dc_sexpr_find(fill, "type");
        if (ft) ftype = dc_sexpr_value(ft);
        if (ftype && strcmp(ftype, "background") == 0) {
            cairo_fill_preserve(cr);
        }
    }
    cairo_stroke(cr);
}

static void
render_polyline(cairo_t *cr, DC_SchCanvas *canvas, DC_SchSymbol *sym,
                const DC_Sexpr *poly)
{
    DC_Sexpr *pts = dc_sexpr_find(poly, "pts");
    if (!pts) return;

    size_t count = 0;
    DC_Sexpr **xys = dc_sexpr_find_all(pts, "xy", &count);
    if (!xys || count < 2) { free(xys); return; }

    for (size_t i = 0; i < count; i++) {
        double lx, ly;
        parse_xy(xys[i], &lx, &ly);
        double wx, wy, sx, sy;
        sym_transform(sym, lx, ly, &wx, &wy);
        dc_sch_canvas_world_to_screen(canvas, wx, wy, &sx, &sy);
        if (i == 0) cairo_move_to(cr, sx, sy);
        else        cairo_line_to(cr, sx, sy);
    }
    free(xys);

    /* Check fill */
    DC_Sexpr *fill = dc_sexpr_find(poly, "fill");
    if (fill) {
        const char *ftype = NULL;
        DC_Sexpr *ft = dc_sexpr_find(fill, "type");
        if (ft) ftype = dc_sexpr_value(ft);
        if (ftype && strcmp(ftype, "background") == 0) {
            cairo_fill_preserve(cr);
        }
    }
    cairo_stroke(cr);
}

static void
render_circle(cairo_t *cr, DC_SchCanvas *canvas, DC_SchSymbol *sym,
              const DC_Sexpr *circ)
{
    DC_Sexpr *center = dc_sexpr_find(circ, "center");
    DC_Sexpr *radius_node = dc_sexpr_find(circ, "radius");
    if (!center || !radius_node) return;

    double cx_l, cy_l;
    parse_xy(center, &cx_l, &cy_l);
    double r_mm = sexpr_float(radius_node, 0);

    double wx, wy, sx, sy;
    sym_transform(sym, cx_l, cy_l, &wx, &wy);
    dc_sch_canvas_world_to_screen(canvas, wx, wy, &sx, &sy);

    double zoom = dc_sch_canvas_get_zoom(canvas);
    double r_px = r_mm * MM_TO_MILS * zoom;

    cairo_arc(cr, sx, sy, r_px, 0, 2 * M_PI);

    DC_Sexpr *fill = dc_sexpr_find(circ, "fill");
    if (fill) {
        const char *ftype = NULL;
        DC_Sexpr *ft = dc_sexpr_find(fill, "type");
        if (ft) ftype = dc_sexpr_value(ft);
        if (ftype && strcmp(ftype, "background") == 0) {
            cairo_fill_preserve(cr);
        }
    }
    cairo_stroke(cr);
}

static void
render_arc(cairo_t *cr, DC_SchCanvas *canvas, DC_SchSymbol *sym,
           const DC_Sexpr *arc_node)
{
    DC_Sexpr *start = dc_sexpr_find(arc_node, "start");
    DC_Sexpr *mid   = dc_sexpr_find(arc_node, "mid");
    DC_Sexpr *end   = dc_sexpr_find(arc_node, "end");
    if (!start || !mid || !end) return;

    /* Get the three points in local coords */
    double x1, y1, xm, ym, x2, y2;
    parse_xy(start, &x1, &y1);
    parse_xy(mid, &xm, &ym);
    parse_xy(end, &x2, &y2);

    /* Transform all three points to screen coords and draw as a curve_to */
    double wx, wy;
    double sx1, sy1, sxm, sym_y, sx2, sy2;

    sym_transform(sym, x1, y1, &wx, &wy);
    dc_sch_canvas_world_to_screen(canvas, wx, wy, &sx1, &sy1);

    sym_transform(sym, xm, ym, &wx, &wy);
    dc_sch_canvas_world_to_screen(canvas, wx, wy, &sxm, &sym_y);

    sym_transform(sym, x2, y2, &wx, &wy);
    dc_sch_canvas_world_to_screen(canvas, wx, wy, &sx2, &sy2);

    /* Approximate arc through 3 points using quadratic bezier */
    cairo_move_to(cr, sx1, sy1);
    double cp1x = 2.0 * sxm - 0.5 * sx1 - 0.5 * sx2;
    double cp1y = 2.0 * sym_y - 0.5 * sy1 - 0.5 * sy2;
    cairo_curve_to(cr, cp1x, cp1y, cp1x, cp1y, sx2, sy2);
    cairo_stroke(cr);
}

static void
render_pin(cairo_t *cr, DC_SchCanvas *canvas, DC_SchSymbol *sym,
           const DC_Sexpr *pin)
{
    /* (pin type shape (at x y angle) (length len) ...) */
    DC_Sexpr *at = dc_sexpr_find(pin, "at");
    DC_Sexpr *length_node = dc_sexpr_find(pin, "length");
    if (!at || !length_node) return;

    double px = sexpr_float(at, 0);
    double py = sexpr_float(at, 1);
    double pin_angle = sexpr_float(at, 2); /* degrees */
    double pin_len = sexpr_float(length_node, 0);

    /* Pin endpoint in local coords */
    double rad = pin_angle * M_PI / 180.0;
    double ex = px + pin_len * cos(rad);
    double ey = py - pin_len * sin(rad); /* Y is inverted in KiCad */

    double wx1, wy1, wx2, wy2;
    sym_transform(sym, px, py, &wx1, &wy1);
    sym_transform(sym, ex, ey, &wx2, &wy2);

    double sx1, sy1, sx2, sy2;
    dc_sch_canvas_world_to_screen(canvas, wx1, wy1, &sx1, &sy1);
    dc_sch_canvas_world_to_screen(canvas, wx2, wy2, &sx2, &sy2);

    /* Draw pin line */
    cairo_set_source_rgb(cr, 0.0, 0.6, 0.0);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, sx1, sy1);
    cairo_line_to(cr, sx2, sy2);
    cairo_stroke(cr);

    /* Pin endpoint circle */
    cairo_arc(cr, sx1, sy1, 2.5, 0, 2 * M_PI);
    cairo_fill(cr);
}

/* =========================================================================
 * Render symbol sub-units from library sexpr
 * ========================================================================= */
static void
render_symbol_unit(cairo_t *cr, DC_SchCanvas *canvas, DC_SchSymbol *sym,
                   const DC_Sexpr *unit, int selected)
{
    if (!unit) return;

    /* Set drawing style */
    if (selected)
        cairo_set_source_rgb(cr, 0.3, 0.8, 1.0);
    else
        cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
    cairo_set_line_width(cr, 2.0);

    /* Iterate children, render each draw primitive */
    for (size_t i = 0; i < dc_sexpr_child_count(unit); i++) {
        DC_Sexpr *child = unit->children[i];
        if (child->type != DC_SEXPR_LIST) continue;
        const char *tag = dc_sexpr_tag(child);
        if (!tag) continue;

        if (strcmp(tag, "rectangle") == 0) {
            if (selected) cairo_set_source_rgb(cr, 0.3, 0.8, 1.0);
            else          cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
            cairo_set_line_width(cr, 2.0);
            render_rectangle(cr, canvas, sym, child);
        } else if (strcmp(tag, "polyline") == 0) {
            if (selected) cairo_set_source_rgb(cr, 0.3, 0.8, 1.0);
            else          cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
            cairo_set_line_width(cr, 2.0);
            render_polyline(cr, canvas, sym, child);
        } else if (strcmp(tag, "circle") == 0) {
            if (selected) cairo_set_source_rgb(cr, 0.3, 0.8, 1.0);
            else          cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
            cairo_set_line_width(cr, 2.0);
            render_circle(cr, canvas, sym, child);
        } else if (strcmp(tag, "arc") == 0) {
            if (selected) cairo_set_source_rgb(cr, 0.3, 0.8, 1.0);
            else          cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
            cairo_set_line_width(cr, 2.0);
            render_arc(cr, canvas, sym, child);
        } else if (strcmp(tag, "pin") == 0) {
            render_pin(cr, canvas, sym, child);
        }
    }
}

/* =========================================================================
 * Fallback: generic box + pin stubs (when library not available)
 * ========================================================================= */
#define SYM_BODY_W  80.0   /* mils */
#define SYM_BODY_H  60.0   /* mils */
#define PIN_LEN     30.0   /* mils */

static void
render_generic(cairo_t *cr, DC_SchCanvas *canvas, DC_SchSymbol *sym,
               int selected)
{
    double cx, cy;
    dc_sch_canvas_world_to_screen(canvas, sym->x, sym->y, &cx, &cy);

    double zoom = dc_sch_canvas_get_zoom(canvas);
    double bw = SYM_BODY_W * zoom;
    double bh = SYM_BODY_H * zoom;
    double pin = PIN_LEN * zoom;

    if (selected)
        cairo_set_source_rgb(cr, 0.3, 0.8, 1.0);
    else
        cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, cx - bw / 2, cy - bh / 2, bw, bh);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.0, 0.6, 0.0);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, cx - bw / 2 - pin, cy);
    cairo_line_to(cr, cx - bw / 2, cy);
    cairo_stroke(cr);
    cairo_move_to(cr, cx + bw / 2, cy);
    cairo_line_to(cr, cx + bw / 2 + pin, cy);
    cairo_stroke(cr);
}

/* =========================================================================
 * Main render entry point
 * ========================================================================= */
void
dc_sch_symbol_render(cairo_t *cr,
                      DC_SchCanvas *canvas,
                      DC_SchSymbol *sym,
                      DC_ELibrary *lib,
                      int selected)
{
    if (!cr || !canvas || !sym) return;

    /* Try to find the symbol definition in the library */
    const DC_Sexpr *sym_def = NULL;
    if (lib && sym->lib_id) {
        sym_def = dc_elibrary_find_symbol(lib, sym->lib_id);
        if (!sym_def) {
            /* Try name-only lookup (strip library prefix) */
            const char *colon = strchr(sym->lib_id, ':');
            const char *name = colon ? colon + 1 : sym->lib_id;
            sym_def = dc_elibrary_find_symbol_by_name(lib, name);
        }
    }

    int rendered_from_lib = 0;

    if (sym_def) {
        /* Find sub-symbol units: {name}_0_1 (graphics) and {name}_1_1 (pins) */
        const char *colon = strchr(sym->lib_id, ':');
        const char *base_name = colon ? colon + 1 : sym->lib_id;

        for (size_t i = 0; i < dc_sexpr_child_count(sym_def); i++) {
            DC_Sexpr *child = sym_def->children[i];
            if (child->type != DC_SEXPR_LIST) continue;
            const char *tag = dc_sexpr_tag(child);
            if (!tag || strcmp(tag, "symbol") != 0) continue;

            /* Get the sub-symbol name */
            const char *sub_name = dc_sexpr_value(child);
            if (!sub_name) continue;

            /* Match {base_name}_0_1 or {base_name}_1_1 */
            size_t blen = strlen(base_name);
            if (strncmp(sub_name, base_name, blen) == 0 &&
                sub_name[blen] == '_') {
                render_symbol_unit(cr, canvas, sym, child, selected);
                rendered_from_lib = 1;
            }
        }
    }

    if (!rendered_from_lib) {
        render_generic(cr, canvas, sym, selected);
    }

    /* Reference text (above symbol) */
    double cx, cy;
    dc_sch_canvas_world_to_screen(canvas, sym->x, sym->y, &cx, &cy);
    double zoom = dc_sch_canvas_get_zoom(canvas);

    cairo_set_source_rgb(cr, 0.9, 0.9, 0.3);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);
    double font_size = 11.0;
    if (zoom > 1.5) font_size = 11.0 * (zoom / 1.5);
    if (font_size > 20.0) font_size = 20.0;
    cairo_set_font_size(cr, font_size);

    if (sym->reference) {
        /* Position reference above the symbol body */
        double ref_y = cy - 5.0 * MM_TO_MILS * zoom - 4;
        cairo_move_to(cr, cx - 20, ref_y);
        cairo_show_text(cr, sym->reference);
    }

    /* Value text (below symbol) */
    const char *val = dc_eschematic_symbol_property(sym, "Value");
    if (!val && sym->lib_id) {
        /* Use lib_id as fallback value display */
        val = sym->lib_id;
    }
    if (val) {
        cairo_set_source_rgb(cr, 0.6, 0.6, 0.9);
        cairo_set_font_size(cr, font_size * 0.85);
        double val_y = cy + 5.0 * MM_TO_MILS * zoom + font_size + 2;
        cairo_move_to(cr, cx - 20, val_y);
        cairo_show_text(cr, val);
    }
}

/* =========================================================================
 * Standalone preview renderer (no SchCanvas needed)
 *
 * Scans the symbol sexpr for bbox, fits into target rect, renders all
 * primitives with a simple translate+scale transform.
 * ========================================================================= */

/* Collect bounding box from all draw primitives */
static void
preview_bbox_update(double *minx, double *miny, double *maxx, double *maxy,
                    double px, double py)
{
    if (px < *minx) *minx = px;
    if (py < *miny) *miny = py;
    if (px > *maxx) *maxx = px;
    if (py > *maxy) *maxy = py;
}

static void
preview_scan_bbox(const DC_Sexpr *unit,
                  double *minx, double *miny, double *maxx, double *maxy)
{
    for (size_t i = 0; i < dc_sexpr_child_count(unit); i++) {
        DC_Sexpr *child = unit->children[i];
        if (child->type != DC_SEXPR_LIST) continue;
        const char *tag = dc_sexpr_tag(child);
        if (!tag) continue;

        if (strcmp(tag, "rectangle") == 0) {
            DC_Sexpr *s = dc_sexpr_find(child, "start");
            DC_Sexpr *e = dc_sexpr_find(child, "end");
            if (s && e) {
                double x1, y1, x2, y2;
                parse_xy(s, &x1, &y1);
                parse_xy(e, &x2, &y2);
                preview_bbox_update(minx, miny, maxx, maxy, x1, y1);
                preview_bbox_update(minx, miny, maxx, maxy, x2, y2);
            }
        } else if (strcmp(tag, "polyline") == 0) {
            DC_Sexpr *pts = dc_sexpr_find(child, "pts");
            if (pts) {
                size_t nc = 0;
                DC_Sexpr **xys = dc_sexpr_find_all(pts, "xy", &nc);
                if (xys) {
                    for (size_t j = 0; j < nc; j++) {
                        double px, py;
                        parse_xy(xys[j], &px, &py);
                        preview_bbox_update(minx, miny, maxx, maxy, px, py);
                    }
                    free(xys);
                }
            }
        } else if (strcmp(tag, "circle") == 0) {
            DC_Sexpr *center = dc_sexpr_find(child, "center");
            DC_Sexpr *radius = dc_sexpr_find(child, "radius");
            if (center && radius) {
                double cx, cy;
                parse_xy(center, &cx, &cy);
                double r = sexpr_float(radius, 0);
                preview_bbox_update(minx, miny, maxx, maxy, cx - r, cy - r);
                preview_bbox_update(minx, miny, maxx, maxy, cx + r, cy + r);
            }
        } else if (strcmp(tag, "arc") == 0) {
            DC_Sexpr *s = dc_sexpr_find(child, "start");
            DC_Sexpr *m = dc_sexpr_find(child, "mid");
            DC_Sexpr *e = dc_sexpr_find(child, "end");
            if (s && m && e) {
                double x1, y1, xm, ym, x2, y2;
                parse_xy(s, &x1, &y1);
                parse_xy(m, &xm, &ym);
                parse_xy(e, &x2, &y2);
                preview_bbox_update(minx, miny, maxx, maxy, x1, y1);
                preview_bbox_update(minx, miny, maxx, maxy, xm, ym);
                preview_bbox_update(minx, miny, maxx, maxy, x2, y2);
            }
        } else if (strcmp(tag, "pin") == 0) {
            DC_Sexpr *at = dc_sexpr_find(child, "at");
            DC_Sexpr *length_node = dc_sexpr_find(child, "length");
            if (at && length_node) {
                double px = sexpr_float(at, 0);
                double py = sexpr_float(at, 1);
                double pin_angle = sexpr_float(at, 2);
                double pin_len = sexpr_float(length_node, 0);
                double rad = pin_angle * M_PI / 180.0;
                double ex = px + pin_len * cos(rad);
                double ey = py + pin_len * sin(rad);
                preview_bbox_update(minx, miny, maxx, maxy, px, py);
                preview_bbox_update(minx, miny, maxx, maxy, ex, ey);
            }
        }
    }
}

/* Render a single primitive in preview coords */
static void
preview_render_primitive(cairo_t *cr, const DC_Sexpr *prim,
                         double ox, double oy, double scale)
{
    const char *tag = dc_sexpr_tag(prim);
    if (!tag) return;

#define PX(lx) (ox + (lx) * scale)
#define PY(ly) (oy + (ly) * scale)

    if (strcmp(tag, "rectangle") == 0) {
        DC_Sexpr *s = dc_sexpr_find(prim, "start");
        DC_Sexpr *e = dc_sexpr_find(prim, "end");
        if (!s || !e) return;
        double x1, y1, x2, y2;
        parse_xy(s, &x1, &y1);
        parse_xy(e, &x2, &y2);
        cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, PX(x1), PY(y1), (x2 - x1) * scale, (y2 - y1) * scale);

        DC_Sexpr *fill = dc_sexpr_find(prim, "fill");
        if (fill) {
            DC_Sexpr *ft = dc_sexpr_find(fill, "type");
            const char *ftype = ft ? dc_sexpr_value(ft) : NULL;
            if (ftype && strcmp(ftype, "background") == 0) {
                cairo_set_source_rgba(cr, 0.7, 0.2, 0.2, 0.15);
                cairo_fill_preserve(cr);
                cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
            }
        }
        cairo_stroke(cr);

    } else if (strcmp(tag, "polyline") == 0) {
        DC_Sexpr *pts = dc_sexpr_find(prim, "pts");
        if (!pts) return;
        size_t nc = 0;
        DC_Sexpr **xys = dc_sexpr_find_all(pts, "xy", &nc);
        if (!xys || nc < 2) { free(xys); return; }

        cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
        cairo_set_line_width(cr, 2.0);
        for (size_t i = 0; i < nc; i++) {
            double lx, ly;
            parse_xy(xys[i], &lx, &ly);
            if (i == 0) cairo_move_to(cr, PX(lx), PY(ly));
            else        cairo_line_to(cr, PX(lx), PY(ly));
        }
        free(xys);

        DC_Sexpr *fill = dc_sexpr_find(prim, "fill");
        if (fill) {
            DC_Sexpr *ft = dc_sexpr_find(fill, "type");
            const char *ftype = ft ? dc_sexpr_value(ft) : NULL;
            if (ftype && strcmp(ftype, "background") == 0) {
                cairo_set_source_rgba(cr, 0.7, 0.2, 0.2, 0.15);
                cairo_fill_preserve(cr);
                cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
            }
        }
        cairo_stroke(cr);

    } else if (strcmp(tag, "circle") == 0) {
        DC_Sexpr *center = dc_sexpr_find(prim, "center");
        DC_Sexpr *radius = dc_sexpr_find(prim, "radius");
        if (!center || !radius) return;
        double cx, cy;
        parse_xy(center, &cx, &cy);
        double r = sexpr_float(radius, 0);

        cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
        cairo_set_line_width(cr, 2.0);
        cairo_arc(cr, PX(cx), PY(cy), r * scale, 0, 2 * M_PI);

        DC_Sexpr *fill = dc_sexpr_find(prim, "fill");
        if (fill) {
            DC_Sexpr *ft = dc_sexpr_find(fill, "type");
            const char *ftype = ft ? dc_sexpr_value(ft) : NULL;
            if (ftype && strcmp(ftype, "background") == 0) {
                cairo_set_source_rgba(cr, 0.7, 0.2, 0.2, 0.15);
                cairo_fill_preserve(cr);
                cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
            }
        }
        cairo_stroke(cr);

    } else if (strcmp(tag, "arc") == 0) {
        DC_Sexpr *s = dc_sexpr_find(prim, "start");
        DC_Sexpr *m = dc_sexpr_find(prim, "mid");
        DC_Sexpr *e = dc_sexpr_find(prim, "end");
        if (!s || !m || !e) return;
        double x1, y1, xm, ym, x2, y2;
        parse_xy(s, &x1, &y1);
        parse_xy(m, &xm, &ym);
        parse_xy(e, &x2, &y2);

        cairo_set_source_rgb(cr, 0.7, 0.2, 0.2);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, PX(x1), PY(y1));
        double cp1x = 2.0 * PX(xm) - 0.5 * PX(x1) - 0.5 * PX(x2);
        double cp1y = 2.0 * PY(ym) - 0.5 * PY(y1) - 0.5 * PY(y2);
        cairo_curve_to(cr, cp1x, cp1y, cp1x, cp1y, PX(x2), PY(y2));
        cairo_stroke(cr);

    } else if (strcmp(tag, "pin") == 0) {
        DC_Sexpr *at = dc_sexpr_find(prim, "at");
        DC_Sexpr *length_node = dc_sexpr_find(prim, "length");
        if (!at || !length_node) return;

        double px = sexpr_float(at, 0);
        double py = sexpr_float(at, 1);
        double pin_angle = sexpr_float(at, 2);
        double pin_len = sexpr_float(length_node, 0);
        double rad = pin_angle * M_PI / 180.0;
        double ex = px + pin_len * cos(rad);
        double ey = py + pin_len * sin(rad);

        cairo_set_source_rgb(cr, 0.0, 0.6, 0.0);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, PX(px), PY(py));
        cairo_line_to(cr, PX(ex), PY(ey));
        cairo_stroke(cr);
        cairo_arc(cr, PX(px), PY(py), 2.5, 0, 2 * M_PI);
        cairo_fill(cr);
    }

#undef PX
#undef PY
}

/* Resolve (extends "ParentName") to get the actual draw definition.
 * Returns the parent symbol if extends is found, otherwise sym_def itself. */
static const DC_Sexpr *
resolve_extends(const DC_Sexpr *sym_def, DC_ELibrary *lib)
{
    if (!sym_def || !lib) return sym_def;

    DC_Sexpr *ext = dc_sexpr_find(sym_def, "extends");
    if (!ext) return sym_def;

    const char *parent_name = dc_sexpr_value(ext);
    if (!parent_name) return sym_def;

    /* Look up parent in the same library */
    const DC_Sexpr *parent = dc_elibrary_find_symbol_by_name(lib, parent_name);
    if (!parent) return sym_def;

    /* Recursively resolve (parent might also extend) */
    return resolve_extends(parent, lib);
}

/* Core preview implementation */
static void
render_preview_impl(cairo_t *cr, const DC_Sexpr *sym_def,
                     double x, double y, double w, double h)
{
    if (!cr || !sym_def || w <= 0 || h <= 0) return;

    /* Compute bbox across all sub-units */
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;

    for (size_t i = 0; i < dc_sexpr_child_count(sym_def); i++) {
        DC_Sexpr *child = sym_def->children[i];
        if (child->type != DC_SEXPR_LIST) continue;
        const char *ctag = dc_sexpr_tag(child);
        if (ctag && strcmp(ctag, "symbol") == 0) {
            preview_scan_bbox(child, &minx, &miny, &maxx, &maxy);
        }
    }
    /* Also scan top-level primitives */
    preview_scan_bbox(sym_def, &minx, &miny, &maxx, &maxy);

    if (minx >= maxx || miny >= maxy) return; /* nothing to draw */

    /* Compute scale to fit bbox into target rect with margin */
    double margin = 8.0;
    double tw = w - 2 * margin;
    double th = h - 2 * margin;
    if (tw <= 0 || th <= 0) return;

    double bw = maxx - minx;
    double bh = maxy - miny;
    double scale = (tw / bw < th / bh) ? tw / bw : th / bh;

    /* Center in target rect */
    double ox = x + margin + (tw - bw * scale) / 2.0 - minx * scale;
    double oy = y + margin + (th - bh * scale) / 2.0 - miny * scale;

    /* Render all sub-units */
    for (size_t i = 0; i < dc_sexpr_child_count(sym_def); i++) {
        DC_Sexpr *child = sym_def->children[i];
        if (child->type != DC_SEXPR_LIST) continue;
        const char *ctag = dc_sexpr_tag(child);
        if (!ctag) continue;

        if (strcmp(ctag, "symbol") == 0) {
            /* Render primitives within this sub-unit */
            for (size_t j = 0; j < dc_sexpr_child_count(child); j++) {
                DC_Sexpr *prim = child->children[j];
                if (prim->type == DC_SEXPR_LIST)
                    preview_render_primitive(cr, prim, ox, oy, scale);
            }
        } else {
            /* Top-level primitive */
            preview_render_primitive(cr, child, ox, oy, scale);
        }
    }
}

void
dc_sch_symbol_render_preview(cairo_t *cr, const DC_Sexpr *sym_def,
                               double x, double y, double w, double h)
{
    render_preview_impl(cr, sym_def, x, y, w, h);
}

void
dc_sch_symbol_render_preview_ex(cairo_t *cr, const DC_Sexpr *sym_def,
                                  DC_ELibrary *lib,
                                  double x, double y, double w, double h)
{
    const DC_Sexpr *resolved = resolve_extends(sym_def, lib);
    render_preview_impl(cr, resolved, x, y, w, h);
}
