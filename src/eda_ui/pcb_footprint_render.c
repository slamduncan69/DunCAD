#define _POSIX_C_SOURCE 200809L

#include "pcb_footprint_render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================================
 * Helpers
 * ========================================================================= */
static double
fp_float(const DC_Sexpr *node, size_t idx)
{
    const char *v = dc_sexpr_value_at(node, idx);
    return v ? atof(v) : 0.0;
}

static void
fp_xy(const DC_Sexpr *node, double *x, double *y)
{
    if (!node || dc_sexpr_child_count(node) < 3) { *x = *y = 0; return; }
    *x = fp_float(node, 0);
    *y = fp_float(node, 1);
}

/* Layer color mapping */
static void
set_layer_color(cairo_t *cr, const char *layer)
{
    if (!layer) { cairo_set_source_rgb(cr, 0.5, 0.5, 0.5); return; }

    if (strcmp(layer, "F.Cu") == 0)
        cairo_set_source_rgb(cr, 0.8, 0.2, 0.2);
    else if (strcmp(layer, "B.Cu") == 0)
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.8);
    else if (strcmp(layer, "F.SilkS") == 0 || strcmp(layer, "F.Silkscreen") == 0)
        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    else if (strcmp(layer, "B.SilkS") == 0 || strcmp(layer, "B.Silkscreen") == 0)
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.9);
    else if (strcmp(layer, "F.Fab") == 0)
        cairo_set_source_rgb(cr, 0.6, 0.6, 0.3);
    else if (strcmp(layer, "B.Fab") == 0)
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.6);
    else if (strstr(layer, "Courtyard"))
        cairo_set_source_rgb(cr, 0.8, 0.8, 0.2);
    else if (strstr(layer, "Mask"))
        cairo_set_source_rgb(cr, 0.6, 0.2, 0.6);
    else
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
}

/* =========================================================================
 * Bbox scanning
 * ========================================================================= */
static void
bbox_update(double *minx, double *miny, double *maxx, double *maxy,
            double px, double py)
{
    if (px < *minx) *minx = px;
    if (py < *miny) *miny = py;
    if (px > *maxx) *maxx = px;
    if (py > *maxy) *maxy = py;
}

static void
scan_fp_bbox(const DC_Sexpr *fp,
             double *minx, double *miny, double *maxx, double *maxy)
{
    for (size_t i = 0; i < dc_sexpr_child_count(fp); i++) {
        DC_Sexpr *child = fp->children[i];
        if (child->type != DC_SEXPR_LIST) continue;
        const char *tag = dc_sexpr_tag(child);
        if (!tag) continue;

        if (strcmp(tag, "pad") == 0) {
            DC_Sexpr *at = dc_sexpr_find(child, "at");
            DC_Sexpr *size = dc_sexpr_find(child, "size");
            if (at && size) {
                double px, py, sw, sh;
                fp_xy(at, &px, &py);
                fp_xy(size, &sw, &sh);
                bbox_update(minx, miny, maxx, maxy, px - sw/2, py - sh/2);
                bbox_update(minx, miny, maxx, maxy, px + sw/2, py + sh/2);
            }
        } else if (strcmp(tag, "fp_line") == 0) {
            DC_Sexpr *start = dc_sexpr_find(child, "start");
            DC_Sexpr *end = dc_sexpr_find(child, "end");
            if (start && end) {
                double x1, y1, x2, y2;
                fp_xy(start, &x1, &y1);
                fp_xy(end, &x2, &y2);
                bbox_update(minx, miny, maxx, maxy, x1, y1);
                bbox_update(minx, miny, maxx, maxy, x2, y2);
            }
        } else if (strcmp(tag, "fp_rect") == 0) {
            DC_Sexpr *start = dc_sexpr_find(child, "start");
            DC_Sexpr *end = dc_sexpr_find(child, "end");
            if (start && end) {
                double x1, y1, x2, y2;
                fp_xy(start, &x1, &y1);
                fp_xy(end, &x2, &y2);
                bbox_update(minx, miny, maxx, maxy, x1, y1);
                bbox_update(minx, miny, maxx, maxy, x2, y2);
            }
        } else if (strcmp(tag, "fp_circle") == 0) {
            DC_Sexpr *center = dc_sexpr_find(child, "center");
            DC_Sexpr *end = dc_sexpr_find(child, "end");
            if (center && end) {
                double cx, cy, ex, ey;
                fp_xy(center, &cx, &cy);
                fp_xy(end, &ex, &ey);
                double r = sqrt((ex-cx)*(ex-cx) + (ey-cy)*(ey-cy));
                bbox_update(minx, miny, maxx, maxy, cx - r, cy - r);
                bbox_update(minx, miny, maxx, maxy, cx + r, cy + r);
            }
        } else if (strcmp(tag, "fp_arc") == 0) {
            DC_Sexpr *start = dc_sexpr_find(child, "start");
            DC_Sexpr *mid = dc_sexpr_find(child, "mid");
            DC_Sexpr *end = dc_sexpr_find(child, "end");
            if (start && mid && end) {
                double x1, y1, xm, ym, x2, y2;
                fp_xy(start, &x1, &y1);
                fp_xy(mid, &xm, &ym);
                fp_xy(end, &x2, &y2);
                bbox_update(minx, miny, maxx, maxy, x1, y1);
                bbox_update(minx, miny, maxx, maxy, xm, ym);
                bbox_update(minx, miny, maxx, maxy, x2, y2);
            }
        }
    }
}

/* =========================================================================
 * Render primitives
 * ========================================================================= */
static void
render_fp_primitive(cairo_t *cr, const DC_Sexpr *prim,
                    double ox, double oy, double scale)
{
    const char *tag = dc_sexpr_tag(prim);
    if (!tag) return;

#define FPX(lx) (ox + (lx) * scale)
#define FPY(ly) (oy + (ly) * scale)

    /* Get layer for coloring */
    DC_Sexpr *layer_node = dc_sexpr_find(prim, "layer");
    const char *layer = layer_node ? dc_sexpr_value(layer_node) : NULL;

    if (strcmp(tag, "pad") == 0) {
        DC_Sexpr *at = dc_sexpr_find(prim, "at");
        DC_Sexpr *size = dc_sexpr_find(prim, "size");
        if (!at || !size) return;

        double px, py, sw, sh;
        fp_xy(at, &px, &py);
        fp_xy(size, &sw, &sh);

        /* Determine pad type for coloring */
        const char *pad_type = dc_sexpr_value_at(prim, 1); /* smd/thru_hole */
        if (pad_type && strcmp(pad_type, "thru_hole") == 0)
            cairo_set_source_rgba(cr, 0.8, 0.6, 0.2, 0.7);
        else
            set_layer_color(cr, layer);

        /* Draw rounded rectangle pad */
        double rx = FPX(px - sw/2);
        double ry = FPY(py - sh/2);
        double rw = sw * scale;
        double rh = sh * scale;
        double corner = fmin(rw, rh) * 0.2;

        cairo_new_sub_path(cr);
        cairo_arc(cr, rx + rw - corner, ry + corner,        corner, -M_PI/2, 0);
        cairo_arc(cr, rx + rw - corner, ry + rh - corner,   corner, 0, M_PI/2);
        cairo_arc(cr, rx + corner,      ry + rh - corner,   corner, M_PI/2, M_PI);
        cairo_arc(cr, rx + corner,      ry + corner,        corner, M_PI, 3*M_PI/2);
        cairo_close_path(cr);
        cairo_fill(cr);

        /* Pad number */
        const char *pad_num = dc_sexpr_value(prim);
        if (pad_num) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_font_size(cr, fmax(8.0, fmin(rh * 0.6, rw * 0.6)));
            cairo_text_extents_t ext;
            cairo_text_extents(cr, pad_num, &ext);
            cairo_move_to(cr, rx + (rw - ext.width) / 2.0,
                              ry + (rh + ext.height) / 2.0);
            cairo_show_text(cr, pad_num);
        }

    } else if (strcmp(tag, "fp_line") == 0) {
        DC_Sexpr *start = dc_sexpr_find(prim, "start");
        DC_Sexpr *end = dc_sexpr_find(prim, "end");
        if (!start || !end) return;

        double x1, y1, x2, y2;
        fp_xy(start, &x1, &y1);
        fp_xy(end, &x2, &y2);

        set_layer_color(cr, layer);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, FPX(x1), FPY(y1));
        cairo_line_to(cr, FPX(x2), FPY(y2));
        cairo_stroke(cr);

    } else if (strcmp(tag, "fp_rect") == 0) {
        DC_Sexpr *start = dc_sexpr_find(prim, "start");
        DC_Sexpr *end = dc_sexpr_find(prim, "end");
        if (!start || !end) return;

        double x1, y1, x2, y2;
        fp_xy(start, &x1, &y1);
        fp_xy(end, &x2, &y2);

        set_layer_color(cr, layer);
        cairo_set_line_width(cr, 1.5);
        cairo_rectangle(cr, FPX(x1), FPY(y1),
                        (x2 - x1) * scale, (y2 - y1) * scale);
        cairo_stroke(cr);

    } else if (strcmp(tag, "fp_circle") == 0) {
        DC_Sexpr *center = dc_sexpr_find(prim, "center");
        DC_Sexpr *end = dc_sexpr_find(prim, "end");
        if (!center || !end) return;

        double cx, cy, ex, ey;
        fp_xy(center, &cx, &cy);
        fp_xy(end, &ex, &ey);
        double r = sqrt((ex-cx)*(ex-cx) + (ey-cy)*(ey-cy));

        set_layer_color(cr, layer);
        cairo_set_line_width(cr, 1.5);
        cairo_arc(cr, FPX(cx), FPY(cy), r * scale, 0, 2 * M_PI);
        cairo_stroke(cr);

    } else if (strcmp(tag, "fp_arc") == 0) {
        DC_Sexpr *start = dc_sexpr_find(prim, "start");
        DC_Sexpr *mid = dc_sexpr_find(prim, "mid");
        DC_Sexpr *end = dc_sexpr_find(prim, "end");
        if (!start || !mid || !end) return;

        double x1, y1, xm, ym, x2, y2;
        fp_xy(start, &x1, &y1);
        fp_xy(mid, &xm, &ym);
        fp_xy(end, &x2, &y2);

        set_layer_color(cr, layer);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, FPX(x1), FPY(y1));
        double cp1x = 2.0 * FPX(xm) - 0.5 * FPX(x1) - 0.5 * FPX(x2);
        double cp1y = 2.0 * FPY(ym) - 0.5 * FPY(y1) - 0.5 * FPY(y2);
        cairo_curve_to(cr, cp1x, cp1y, cp1x, cp1y, FPX(x2), FPY(y2));
        cairo_stroke(cr);
    }

#undef FPX
#undef FPY
}

/* =========================================================================
 * Public API
 * ========================================================================= */
void
dc_pcb_footprint_render_preview(cairo_t *cr, const DC_Sexpr *fp_def,
                                  double x, double y, double w, double h)
{
    if (!cr || !fp_def || w <= 0 || h <= 0) return;

    /* Compute bbox */
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    scan_fp_bbox(fp_def, &minx, &miny, &maxx, &maxy);

    if (minx >= maxx || miny >= maxy) return;

    /* Fit to rect */
    double margin = 12.0;
    double tw = w - 2 * margin;
    double th = h - 2 * margin;
    if (tw <= 0 || th <= 0) return;

    double bw = maxx - minx;
    double bh = maxy - miny;
    double scale = (tw / bw < th / bh) ? tw / bw : th / bh;

    double ox = x + margin + (tw - bw * scale) / 2.0 - minx * scale;
    double oy = y + margin + (th - bh * scale) / 2.0 - miny * scale;

    /* Render all primitives */
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_NORMAL);
    for (size_t i = 0; i < dc_sexpr_child_count(fp_def); i++) {
        DC_Sexpr *child = fp_def->children[i];
        if (child->type == DC_SEXPR_LIST)
            render_fp_primitive(cr, child, ox, oy, scale);
    }
}
