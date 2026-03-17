#define _POSIX_C_SOURCE 200809L

#include "pcb_canvas.h"
#include "pcb_editor.h"
#include "eda/eda_pcb.h"
#include "eda/eda_ratsnest.h"
#include "eda/eda_library.h"
#include "core/log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Constants
 * ========================================================================= */
#define PCB_ZOOM_MIN     0.5
#define PCB_ZOOM_MAX     200.0
#define PCB_ZOOM_DEFAULT 10.0   /* pixels per mm */

#define PCB_HIT_RADIUS_PX  8.0 /* hit test radius in screen pixels */
#define PCB_FP_HALF_W      1.5 /* default footprint bbox half-width (mm) */
#define PCB_FP_HALF_H      1.0
#define PCB_TRACK_HIT_PX   6.0
#define PCB_VIA_HIT_EXTRA  0.2 /* mm extra radius for via hit */
#define PCB_PAD_HIT_EXTRA  0.1

#define PCB_GRID_FINE      0.1  /* mm */
#define PCB_GRID_COARSE    1.0  /* mm */

/* Standard layer colors (r, g, b, a) */
typedef struct { double r, g, b, a; } LayerColor;

static const LayerColor LAYER_COLORS[] = {
    [DC_PCB_LAYER_F_CU]      = { 0.8, 0.0, 0.0, 0.8 },
    [DC_PCB_LAYER_B_CU]      = { 0.0, 0.0, 0.8, 0.8 },
    [DC_PCB_LAYER_F_SILKS]   = { 1.0, 1.0, 1.0, 0.7 },
    [DC_PCB_LAYER_B_SILKS]   = { 0.0, 0.8, 0.8, 0.7 },
    [DC_PCB_LAYER_F_MASK]    = { 0.5, 0.0, 0.5, 0.3 },
    [DC_PCB_LAYER_B_MASK]    = { 0.0, 0.5, 0.5, 0.3 },
    [DC_PCB_LAYER_EDGE_CUTS] = { 0.9, 0.9, 0.0, 1.0 },
};
#define N_LAYER_COLORS ((int)(sizeof(LAYER_COLORS) / sizeof(LAYER_COLORS[0])))

static LayerColor get_layer_color(int layer_id)
{
    if (layer_id >= 0 && layer_id < N_LAYER_COLORS &&
        (LAYER_COLORS[layer_id].r > 0 || LAYER_COLORS[layer_id].g > 0 ||
         LAYER_COLORS[layer_id].b > 0)) {
        return LAYER_COLORS[layer_id];
    }
    return (LayerColor){ 0.6, 0.6, 0.0, 0.6 };
}

/* =========================================================================
 * Internal state
 * ========================================================================= */
struct DC_PcbCanvas {
    GtkWidget      *drawing_area;
    double          zoom;          /* pixels per mm */
    double          pan_x, pan_y;  /* world center (mm) */

    DC_EPcb        *pcb;           /* borrowed */
    DC_ELibrary    *lib;           /* borrowed */
    DC_Ratsnest    *ratsnest;      /* borrowed */
    DC_PcbEditor   *editor;        /* back-pointer */

    int             active_layer;
    unsigned char   layer_visible[DC_PCB_LAYER_COUNT];

    /* Selection */
    DC_PcbSelType   sel_type;
    int             sel_index;     /* -1 = none */

    /* Cursor */
    double          cursor_wx, cursor_wy;
    double          cursor_sx, cursor_sy;

    /* Pan state (button 2 drag) */
    int             panning;
    double          pan_drag_sx, pan_drag_sy;
    double          pan_drag_wx, pan_drag_wy;

    /* Move-drag state */
    int             moving;
    double          move_start_wx, move_start_wy;
    double          move_orig_x, move_orig_y;
    double          move_orig_x2, move_orig_y2;

    /* Route drawing state */
    int             routing;
    double          route_start_x, route_start_y;
    int             route_net_id;
};

/* =========================================================================
 * Helpers
 * ========================================================================= */
static double snap_to_grid(double v)
{
    return round(v / PCB_GRID_FINE) * PCB_GRID_FINE;
}

static DC_PcbEditMode get_mode(DC_PcbCanvas *c)
{
    if (c->editor) return dc_pcb_editor_get_mode(c->editor);
    return DC_PCB_MODE_SELECT;
}

/* =========================================================================
 * Coordinate transforms
 * ========================================================================= */
void dc_pcb_canvas_screen_to_world(DC_PcbCanvas *c, double sx, double sy,
                                    double *wx, double *wy)
{
    if (!c) return;
    int w = gtk_widget_get_width(c->drawing_area);
    int h = gtk_widget_get_height(c->drawing_area);
    if (wx) *wx = (sx - w / 2.0) / c->zoom + c->pan_x;
    if (wy) *wy = (sy - h / 2.0) / c->zoom + c->pan_y;
}

void dc_pcb_canvas_world_to_screen(DC_PcbCanvas *c, double wx, double wy,
                                    double *sx, double *sy)
{
    if (!c) return;
    int w = gtk_widget_get_width(c->drawing_area);
    int h = gtk_widget_get_height(c->drawing_area);
    if (sx) *sx = (wx - c->pan_x) * c->zoom + w / 2.0;
    if (sy) *sy = (wy - c->pan_y) * c->zoom + h / 2.0;
}

/* =========================================================================
 * Hit testing
 * ========================================================================= */
static double
point_to_segment_dist(double px, double py,
                      double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1, dy = y2 - y1;
    double len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) {
        double ex = px - x1, ey = py - y1;
        return sqrt(ex * ex + ey * ey);
    }
    double t = ((px - x1) * dx + (py - y1) * dy) / len2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double cx = x1 + t * dx, cy = y1 + t * dy;
    double ex = px - cx, ey = py - cy;
    return sqrt(ex * ex + ey * ey);
}

static int
pcb_hit_footprint(DC_PcbCanvas *c, double wx, double wy)
{
    if (!c->pcb) return -1;
    double r = PCB_HIT_RADIUS_PX / c->zoom;
    double hw = PCB_FP_HALF_W + r;
    double hh = PCB_FP_HALF_H + r;
    for (size_t i = 0; i < dc_epcb_footprint_count(c->pcb); i++) {
        DC_PcbFootprint *fp = dc_epcb_get_footprint(c->pcb, i);
        if (!c->layer_visible[fp->layer]) continue;
        if (fabs(wx - fp->x) < hw && fabs(wy - fp->y) < hh)
            return (int)i;
    }
    return -1;
}

static int
pcb_hit_track(DC_PcbCanvas *c, double wx, double wy)
{
    if (!c->pcb) return -1;
    double best = PCB_TRACK_HIT_PX / c->zoom;
    int hit = -1;
    for (size_t i = 0; i < dc_epcb_track_count(c->pcb); i++) {
        DC_PcbTrack *t = dc_epcb_get_track(c->pcb, i);
        if (!c->layer_visible[t->layer]) continue;
        double threshold = t->width / 2.0 + PCB_TRACK_HIT_PX / c->zoom;
        double d = point_to_segment_dist(wx, wy, t->x1, t->y1, t->x2, t->y2);
        if (d < threshold && d < best) {
            best = d;
            hit = (int)i;
        }
    }
    return hit;
}

static int
pcb_hit_via(DC_PcbCanvas *c, double wx, double wy)
{
    if (!c->pcb) return -1;
    for (size_t i = 0; i < dc_epcb_via_count(c->pcb); i++) {
        DC_PcbVia *v = dc_epcb_get_via(c->pcb, i);
        double r = v->size / 2.0 + PCB_VIA_HIT_EXTRA;
        double dx = wx - v->x, dy = wy - v->y;
        if (dx * dx + dy * dy < r * r)
            return (int)i;
    }
    return -1;
}

static int
pcb_hit_pad(DC_PcbCanvas *c, double wx, double wy, int *out_fp_idx)
{
    if (!c->pcb) return -1;
    for (size_t fi = 0; fi < dc_epcb_footprint_count(c->pcb); fi++) {
        DC_PcbFootprint *fp = dc_epcb_get_footprint(c->pcb, fi);
        if (!c->layer_visible[fp->layer]) continue;
        if (!fp->pads) continue;
        for (size_t pi = 0; pi < dc_array_length(fp->pads); pi++) {
            DC_PcbPad *pad = dc_array_get(fp->pads, pi);
            double px = fp->x + pad->x;
            double py = fp->y + pad->y;
            double hw = pad->size_x / 2.0 + PCB_PAD_HIT_EXTRA;
            double hh = pad->size_y / 2.0 + PCB_PAD_HIT_EXTRA;
            if (fabs(wx - px) < hw && fabs(wy - py) < hh) {
                if (out_fp_idx) *out_fp_idx = (int)fi;
                return (int)pi;
            }
        }
    }
    return -1;
}

static void
pcb_hit_any(DC_PcbCanvas *c, double wx, double wy,
            DC_PcbSelType *out_type, int *out_index)
{
    int idx;
    /* Priority: via > footprint > track > zone */
    if ((idx = pcb_hit_via(c, wx, wy)) >= 0) {
        *out_type = DC_PCB_SEL_VIA; *out_index = idx; return;
    }
    if ((idx = pcb_hit_footprint(c, wx, wy)) >= 0) {
        *out_type = DC_PCB_SEL_FOOTPRINT; *out_index = idx; return;
    }
    if ((idx = pcb_hit_track(c, wx, wy)) >= 0) {
        *out_type = DC_PCB_SEL_TRACK; *out_index = idx; return;
    }
    /* Zone hit: simple check if inside bounding box of any zone */
    if (c->pcb) {
        for (size_t i = 0; i < dc_epcb_zone_count(c->pcb); i++) {
            DC_PcbZone *z = dc_epcb_get_zone(c->pcb, i);
            if (!c->layer_visible[z->layer]) continue;
            if (z->outline && dc_array_length(z->outline) >= 2) {
                DC_PcbZoneVertex *v0 = dc_array_get(z->outline, 0);
                DC_PcbZoneVertex *v2 = dc_array_get(z->outline, 2);
                double minx = fmin(v0->x, v2->x), maxx = fmax(v0->x, v2->x);
                double miny = fmin(v0->y, v2->y), maxy = fmax(v0->y, v2->y);
                if (wx >= minx && wx <= maxx && wy >= miny && wy <= maxy) {
                    *out_type = DC_PCB_SEL_ZONE; *out_index = (int)i; return;
                }
            }
        }
    }
    *out_type = DC_PCB_SEL_NONE;
    *out_index = -1;
}

/* =========================================================================
 * Selection helpers
 * ========================================================================= */
static void
get_sel_position(DC_PcbCanvas *c, double *x, double *y,
                 double *x2, double *y2)
{
    if (!c->pcb || c->sel_index < 0) return;
    switch (c->sel_type) {
    case DC_PCB_SEL_FOOTPRINT: {
        DC_PcbFootprint *fp = dc_epcb_get_footprint(c->pcb, (size_t)c->sel_index);
        if (fp) { *x = fp->x; *y = fp->y; }
    } break;
    case DC_PCB_SEL_TRACK: {
        DC_PcbTrack *t = dc_epcb_get_track(c->pcb, (size_t)c->sel_index);
        if (t) { *x = t->x1; *y = t->y1; if (x2) *x2 = t->x2; if (y2) *y2 = t->y2; }
    } break;
    case DC_PCB_SEL_VIA: {
        DC_PcbVia *v = dc_epcb_get_via(c->pcb, (size_t)c->sel_index);
        if (v) { *x = v->x; *y = v->y; }
    } break;
    default: break;
    }
}

static void
set_sel_position(DC_PcbCanvas *c, double x, double y,
                 double x2, double y2)
{
    if (!c->pcb || c->sel_index < 0) return;
    switch (c->sel_type) {
    case DC_PCB_SEL_FOOTPRINT: {
        DC_PcbFootprint *fp = dc_epcb_get_footprint(c->pcb, (size_t)c->sel_index);
        if (fp) { fp->x = x; fp->y = y; }
    } break;
    case DC_PCB_SEL_TRACK: {
        DC_PcbTrack *t = dc_epcb_get_track(c->pcb, (size_t)c->sel_index);
        if (t) { t->x1 = x; t->y1 = y; t->x2 = x2; t->y2 = y2; }
    } break;
    case DC_PCB_SEL_VIA: {
        DC_PcbVia *v = dc_epcb_get_via(c->pcb, (size_t)c->sel_index);
        if (v) { v->x = x; v->y = y; }
    } break;
    default: break;
    }
}

static void
rotate_selected(DC_PcbCanvas *c)
{
    if (!c->pcb || c->sel_index < 0) return;
    if (c->sel_type == DC_PCB_SEL_FOOTPRINT) {
        DC_PcbFootprint *fp = dc_epcb_get_footprint(c->pcb, (size_t)c->sel_index);
        if (fp) fp->angle = fmod(fp->angle + 90.0, 360.0);
    }
    gtk_widget_queue_draw(c->drawing_area);
}

static void
flip_selected(DC_PcbCanvas *c)
{
    if (!c->pcb || c->sel_index < 0) return;
    if (c->sel_type == DC_PCB_SEL_FOOTPRINT) {
        DC_PcbFootprint *fp = dc_epcb_get_footprint(c->pcb, (size_t)c->sel_index);
        if (fp) {
            fp->layer = (fp->layer == DC_PCB_LAYER_F_CU)
                        ? DC_PCB_LAYER_B_CU : DC_PCB_LAYER_F_CU;
        }
    }
    gtk_widget_queue_draw(c->drawing_area);
}

static void
delete_selected(DC_PcbCanvas *c)
{
    if (!c->pcb || c->sel_index < 0) return;
    size_t idx = (size_t)c->sel_index;
    switch (c->sel_type) {
    case DC_PCB_SEL_FOOTPRINT: dc_epcb_remove_footprint(c->pcb, idx); break;
    case DC_PCB_SEL_TRACK:     dc_epcb_remove_track(c->pcb, idx); break;
    case DC_PCB_SEL_VIA:       dc_epcb_remove_via(c->pcb, idx); break;
    case DC_PCB_SEL_ZONE:      dc_epcb_remove_zone(c->pcb, idx); break;
    default: return;
    }
    c->sel_type = DC_PCB_SEL_NONE;
    c->sel_index = -1;
    if (c->editor) dc_pcb_editor_update_ratsnest(c->editor);
    gtk_widget_queue_draw(c->drawing_area);
}

/* =========================================================================
 * Grid rendering
 * ========================================================================= */
static void draw_grid(DC_PcbCanvas *c, cairo_t *cr, int width, int height)
{
    double wl, wt, wr, wb;
    dc_pcb_canvas_screen_to_world(c, 0, 0, &wl, &wt);
    dc_pcb_canvas_screen_to_world(c, width, height, &wr, &wb);

    double fine_grid = PCB_GRID_FINE;
    double coarse_grid = PCB_GRID_COARSE;

    double screen_fine = fine_grid * c->zoom;
    if (screen_fine < 4.0) fine_grid = coarse_grid;

    /* Fine grid dots */
    if (screen_fine >= 4.0) {
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.4);
        double sx_start = floor(wl / fine_grid) * fine_grid;
        double sy_start = floor(wt / fine_grid) * fine_grid;
        for (double gy = sy_start; gy <= wb; gy += fine_grid) {
            for (double gx = sx_start; gx <= wr; gx += fine_grid) {
                double sx, sy;
                dc_pcb_canvas_world_to_screen(c, gx, gy, &sx, &sy);
                cairo_rectangle(cr, sx - 0.5, sy - 0.5, 1, 1);
            }
        }
        cairo_fill(cr);
    }

    /* Coarse grid lines */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.3);
    cairo_set_line_width(cr, 1.0);
    double cx_start = floor(wl / coarse_grid) * coarse_grid;
    for (double gx = cx_start; gx <= wr; gx += coarse_grid) {
        double sx, sy_unused;
        dc_pcb_canvas_world_to_screen(c, gx, 0, &sx, &sy_unused);
        cairo_move_to(cr, sx, 0);
        cairo_line_to(cr, sx, height);
    }
    double cy_start = floor(wt / coarse_grid) * coarse_grid;
    for (double gy = cy_start; gy <= wb; gy += coarse_grid) {
        double sx_unused, sy;
        dc_pcb_canvas_world_to_screen(c, 0, gy, &sx_unused, &sy);
        cairo_move_to(cr, 0, sy);
        cairo_line_to(cr, width, sy);
    }
    cairo_stroke(cr);

    /* Origin crosshair */
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.5);
    double ox, oy;
    dc_pcb_canvas_world_to_screen(c, 0, 0, &ox, &oy);
    cairo_move_to(cr, ox, 0); cairo_line_to(cr, ox, height);
    cairo_move_to(cr, 0, oy); cairo_line_to(cr, width, oy);
    cairo_stroke(cr);
}

/* =========================================================================
 * PCB element rendering
 * ========================================================================= */
static void draw_pcb(DC_PcbCanvas *c, cairo_t *cr)
{
    if (!c->pcb) return;

    /* Draw zones */
    for (size_t i = 0; i < dc_epcb_zone_count(c->pcb); i++) {
        DC_PcbZone *z = dc_epcb_get_zone(c->pcb, i);
        if (!c->layer_visible[z->layer]) continue;

        int selected = (c->sel_type == DC_PCB_SEL_ZONE && (int)i == c->sel_index);
        LayerColor lc = get_layer_color(z->layer);
        if (selected)
            cairo_set_source_rgba(cr, 0.3, 0.8, 1.0, 0.4);
        else
            cairo_set_source_rgba(cr, lc.r, lc.g, lc.b, lc.a * 0.2);

        if (z->outline && dc_array_length(z->outline) > 2) {
            DC_PcbZoneVertex *v0 = dc_array_get(z->outline, 0);
            double sx, sy;
            dc_pcb_canvas_world_to_screen(c, v0->x, v0->y, &sx, &sy);
            cairo_move_to(cr, sx, sy);
            for (size_t j = 1; j < dc_array_length(z->outline); j++) {
                DC_PcbZoneVertex *v = dc_array_get(z->outline, j);
                dc_pcb_canvas_world_to_screen(c, v->x, v->y, &sx, &sy);
                cairo_line_to(cr, sx, sy);
            }
            cairo_close_path(cr);
            cairo_fill(cr);
        }
    }

    /* Draw tracks */
    for (size_t i = 0; i < dc_epcb_track_count(c->pcb); i++) {
        DC_PcbTrack *t = dc_epcb_get_track(c->pcb, i);
        if (!c->layer_visible[t->layer]) continue;

        int selected = (c->sel_type == DC_PCB_SEL_TRACK && (int)i == c->sel_index);
        LayerColor lc = get_layer_color(t->layer);
        if (selected)
            cairo_set_source_rgba(cr, 0.3, 0.8, 1.0, 0.9);
        else
            cairo_set_source_rgba(cr, lc.r, lc.g, lc.b, lc.a);

        double w = t->width * c->zoom;
        if (w < 1.0) w = 1.0;
        cairo_set_line_width(cr, selected ? w + 2.0 : w);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

        double sx1, sy1, sx2, sy2;
        dc_pcb_canvas_world_to_screen(c, t->x1, t->y1, &sx1, &sy1);
        dc_pcb_canvas_world_to_screen(c, t->x2, t->y2, &sx2, &sy2);
        cairo_move_to(cr, sx1, sy1);
        cairo_line_to(cr, sx2, sy2);
        cairo_stroke(cr);
    }

    /* Draw vias */
    for (size_t i = 0; i < dc_epcb_via_count(c->pcb); i++) {
        DC_PcbVia *v = dc_epcb_get_via(c->pcb, i);
        int selected = (c->sel_type == DC_PCB_SEL_VIA && (int)i == c->sel_index);

        double sx, sy;
        dc_pcb_canvas_world_to_screen(c, v->x, v->y, &sx, &sy);
        double r = (v->size / 2.0) * c->zoom;
        if (r < 2.0) r = 2.0;
        double dr = (v->drill / 2.0) * c->zoom;

        if (selected)
            cairo_set_source_rgba(cr, 0.3, 0.8, 1.0, 0.9);
        else
            cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.9);
        cairo_arc(cr, sx, sy, r, 0, 2 * G_PI);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 0.1, 0.1, 0.12, 1.0);
        cairo_arc(cr, sx, sy, dr, 0, 2 * G_PI);
        cairo_fill(cr);
    }

    /* Draw footprints */
    for (size_t i = 0; i < dc_epcb_footprint_count(c->pcb); i++) {
        DC_PcbFootprint *fp = dc_epcb_get_footprint(c->pcb, i);
        if (!c->layer_visible[fp->layer]) continue;

        double sx, sy;
        dc_pcb_canvas_world_to_screen(c, fp->x, fp->y, &sx, &sy);

        int selected = (c->sel_type == DC_PCB_SEL_FOOTPRINT && (int)i == c->sel_index);
        LayerColor lc = get_layer_color(fp->layer);

        double bw = 3.0 * c->zoom;
        double bh = 2.0 * c->zoom;
        if (selected)
            cairo_set_source_rgba(cr, 0.3, 0.8, 1.0, 0.8);
        else
            cairo_set_source_rgba(cr, lc.r, lc.g, lc.b, 0.5);

        cairo_set_line_width(cr, selected ? 2.5 : 1.5);
        cairo_rectangle(cr, sx - bw / 2, sy - bh / 2, bw, bh);
        cairo_stroke(cr);

        /* Pads */
        if (fp->pads) {
            for (size_t pi = 0; pi < dc_array_length(fp->pads); pi++) {
                DC_PcbPad *pad = dc_array_get(fp->pads, pi);
                double px, py;
                dc_pcb_canvas_world_to_screen(c, fp->x + pad->x, fp->y + pad->y, &px, &py);
                double pw = pad->size_x * c->zoom / 2.0;
                double ph = pad->size_y * c->zoom / 2.0;
                if (pw < 1.5) pw = 1.5;
                if (ph < 1.5) ph = 1.5;

                cairo_set_source_rgba(cr, lc.r, lc.g, lc.b, lc.a);
                cairo_rectangle(cr, px - pw, py - ph, pw * 2, ph * 2);
                cairo_fill(cr);
            }
        }

        /* Reference label */
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_BOLD);
        double font_size = 10.0;
        if (c->zoom > 5.0) font_size = 10.0 * (c->zoom / 5.0);
        if (font_size > 18.0) font_size = 18.0;
        cairo_set_font_size(cr, font_size);
        if (fp->reference) {
            cairo_move_to(cr, sx - bw / 2, sy - bh / 2 - 3);
            cairo_show_text(cr, fp->reference);
        }
    }

    /* Draw ratsnest */
    if (c->ratsnest) {
        cairo_set_source_rgba(cr, 0.3, 0.8, 0.3, 0.6);
        cairo_set_line_width(cr, 1.0);
        double dash[] = { 4.0, 4.0 };
        cairo_set_dash(cr, dash, 2, 0);

        for (size_t i = 0; i < dc_ratsnest_line_count(c->ratsnest); i++) {
            const DC_RatsnestLine *rl = dc_ratsnest_get_line(c->ratsnest, i);
            double sx1, sy1, sx2, sy2;
            dc_pcb_canvas_world_to_screen(c, rl->x1, rl->y1, &sx1, &sy1);
            dc_pcb_canvas_world_to_screen(c, rl->x2, rl->y2, &sx2, &sy2);
            cairo_move_to(cr, sx1, sy1);
            cairo_line_to(cr, sx2, sy2);
        }
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0);
    }
}

/* =========================================================================
 * Overlay rendering
 * ========================================================================= */
static void
draw_overlay(DC_PcbCanvas *c, cairo_t *cr, int width, int height)
{
    DC_PcbEditMode mode = get_mode(c);

    /* Crosshair cursor */
    if (mode == DC_PCB_MODE_ROUTE || mode == DC_PCB_MODE_PLACE_VIA ||
        mode == DC_PCB_MODE_PLACE_FOOTPRINT) {
        double sx, sy;
        dc_pcb_canvas_world_to_screen(c, c->cursor_wx, c->cursor_wy, &sx, &sy);
        cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 0.5);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, sx, 0);
        cairo_line_to(cr, sx, height);
        cairo_move_to(cr, 0, sy);
        cairo_line_to(cr, width, sy);
        cairo_stroke(cr);
    }

    /* Route preview */
    if (c->routing) {
        double sx1, sy1, sx2, sy2;
        dc_pcb_canvas_world_to_screen(c, c->route_start_x, c->route_start_y, &sx1, &sy1);
        dc_pcb_canvas_world_to_screen(c, c->cursor_wx, c->cursor_wy, &sx2, &sy2);
        LayerColor lc = get_layer_color(c->active_layer);
        cairo_set_source_rgba(cr, lc.r, lc.g, lc.b, 0.7);
        /* Use design rule track width for preview */
        double tw = 0.25; /* default */
        if (c->pcb) {
            DC_PcbDesignRules *dr = dc_epcb_get_design_rules(c->pcb);
            if (dr) tw = dr->track_width;
        }
        double w = tw * c->zoom;
        if (w < 2.0) w = 2.0;
        cairo_set_line_width(cr, w);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, sx1, sy1);
        cairo_line_to(cr, sx2, sy2);
        cairo_stroke(cr);
    }
}

/* =========================================================================
 * Draw callback
 * ========================================================================= */
static void on_draw(GtkDrawingArea *area, cairo_t *cr,
                     int width, int height, gpointer userdata)
{
    (void)area;
    DC_PcbCanvas *c = userdata;

    cairo_set_source_rgb(cr, 0.08, 0.08, 0.10);
    cairo_paint(cr);

    draw_grid(c, cr, width, height);
    draw_pcb(c, cr);
    draw_overlay(c, cr, width, height);
}

/* =========================================================================
 * Event handlers
 * ========================================================================= */
static void on_scroll(GtkEventControllerScroll *ctrl, double dx, double dy,
                       gpointer userdata)
{
    (void)ctrl; (void)dx;
    DC_PcbCanvas *c = userdata;
    double factor = (dy < 0) ? 1.15 : 1.0 / 1.15;
    double new_zoom = c->zoom * factor;
    if (new_zoom < PCB_ZOOM_MIN) new_zoom = PCB_ZOOM_MIN;
    if (new_zoom > PCB_ZOOM_MAX) new_zoom = PCB_ZOOM_MAX;
    c->zoom = new_zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

/* Button 1 click — mode-aware dispatch */
static void
on_click_pressed(GtkGestureClick *gesture, int n_press,
                 double x, double y, gpointer userdata)
{
    (void)gesture;
    DC_PcbCanvas *c = userdata;

    gtk_widget_grab_focus(c->drawing_area);

    double wx, wy;
    dc_pcb_canvas_screen_to_world(c, x, y, &wx, &wy);
    double swx = snap_to_grid(wx);
    double swy = snap_to_grid(wy);

    DC_PcbEditMode mode = get_mode(c);

    switch (mode) {
    case DC_PCB_MODE_SELECT: {
        DC_PcbSelType type;
        int idx;
        pcb_hit_any(c, wx, wy, &type, &idx);
        if (type != DC_PCB_SEL_NONE) {
            c->sel_type = type;
            c->sel_index = idx;
            c->moving = 1;
            c->move_start_wx = wx;
            c->move_start_wy = wy;
            get_sel_position(c, &c->move_orig_x, &c->move_orig_y,
                             &c->move_orig_x2, &c->move_orig_y2);
        } else {
            c->sel_type = DC_PCB_SEL_NONE;
            c->sel_index = -1;
        }
        gtk_widget_queue_draw(c->drawing_area);
    } break;

    case DC_PCB_MODE_ROUTE: {
        if (!c->routing) {
            /* Start route — try to pick up net from pad */
            c->routing = 1;
            c->route_start_x = swx;
            c->route_start_y = swy;
            c->route_net_id = 0;
            int fp_idx;
            int pad_idx = pcb_hit_pad(c, wx, wy, &fp_idx);
            if (pad_idx >= 0 && c->pcb) {
                DC_PcbFootprint *fp = dc_epcb_get_footprint(c->pcb, (size_t)fp_idx);
                if (fp && fp->pads) {
                    DC_PcbPad *pad = dc_array_get(fp->pads, (size_t)pad_idx);
                    c->route_net_id = pad->net_id;
                    c->route_start_x = snap_to_grid(fp->x + pad->x);
                    c->route_start_y = snap_to_grid(fp->y + pad->y);
                }
            }
        } else {
            /* Commit route segment */
            if (c->pcb && (swx != c->route_start_x || swy != c->route_start_y)) {
                DC_PcbDesignRules *dr = dc_epcb_get_design_rules(c->pcb);
                double tw = dr ? dr->track_width : 0.25;
                dc_epcb_add_track(c->pcb,
                    c->route_start_x, c->route_start_y,
                    swx, swy, tw, c->active_layer, c->route_net_id);
                if (c->editor) dc_pcb_editor_update_ratsnest(c->editor);
            }
            c->route_start_x = swx;
            c->route_start_y = swy;

            /* Check if landing on pad — finish route */
            int fp_idx;
            int pad_idx = pcb_hit_pad(c, wx, wy, &fp_idx);
            if (pad_idx >= 0) {
                c->routing = 0;
            }

            /* Double-click ends chain */
            if (n_press >= 2)
                c->routing = 0;
        }
        gtk_widget_queue_draw(c->drawing_area);
    } break;

    case DC_PCB_MODE_PLACE_VIA: {
        if (c->pcb) {
            DC_PcbDesignRules *dr = dc_epcb_get_design_rules(c->pcb);
            double vs = dr ? dr->via_size : 0.8;
            double vd = dr ? dr->via_drill : 0.4;
            dc_epcb_add_via(c->pcb, swx, swy, vs, vd, 0);
            if (c->editor) dc_pcb_editor_update_ratsnest(c->editor);
            gtk_widget_queue_draw(c->drawing_area);
        }
    } break;

    case DC_PCB_MODE_PLACE_FOOTPRINT:
    case DC_PCB_MODE_ZONE:
    case DC_PCB_MODE_MEASURE:
        break;
    }
}

static void
on_click_released(GtkGestureClick *gesture, int n_press,
                  double x, double y, gpointer userdata)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    DC_PcbCanvas *c = userdata;
    c->moving = 0;
}

/* Button 2 drag — pan */
static void
on_pan_begin(GtkGestureDrag *gesture, double x, double y, gpointer userdata)
{
    (void)gesture;
    DC_PcbCanvas *c = userdata;
    c->panning = 1;
    c->pan_drag_sx = x;
    c->pan_drag_sy = y;
    c->pan_drag_wx = c->pan_x;
    c->pan_drag_wy = c->pan_y;
}

static void
on_pan_update(GtkGestureDrag *gesture, double off_x, double off_y,
              gpointer userdata)
{
    (void)gesture;
    DC_PcbCanvas *c = userdata;
    if (!c->panning) return;
    c->pan_x = c->pan_drag_wx - off_x / c->zoom;
    c->pan_y = c->pan_drag_wy - off_y / c->zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

static void
on_pan_end(GtkGestureDrag *gesture, double off_x, double off_y,
           gpointer userdata)
{
    (void)gesture; (void)off_x; (void)off_y;
    DC_PcbCanvas *c = userdata;
    c->panning = 0;
}

/* Motion — cursor tracking + move-drag */
static void
on_motion(GtkEventControllerMotion *ctrl, double x, double y,
          gpointer userdata)
{
    (void)ctrl;
    DC_PcbCanvas *c = userdata;

    c->cursor_sx = x;
    c->cursor_sy = y;
    dc_pcb_canvas_screen_to_world(c, x, y, &c->cursor_wx, &c->cursor_wy);
    c->cursor_wx = snap_to_grid(c->cursor_wx);
    c->cursor_wy = snap_to_grid(c->cursor_wy);

    /* Move-drag selected element */
    if (c->moving && c->sel_index >= 0) {
        double wx, wy;
        dc_pcb_canvas_screen_to_world(c, x, y, &wx, &wy);
        double dx = snap_to_grid(wx) - snap_to_grid(c->move_start_wx);
        double dy = snap_to_grid(wy) - snap_to_grid(c->move_start_wy);
        set_sel_position(c, c->move_orig_x + dx, c->move_orig_y + dy,
                         c->move_orig_x2 + dx, c->move_orig_y2 + dy);
    }

    DC_PcbEditMode mode = get_mode(c);
    if (c->moving || c->routing ||
        mode == DC_PCB_MODE_ROUTE || mode == DC_PCB_MODE_PLACE_VIA ||
        mode == DC_PCB_MODE_PLACE_FOOTPRINT)
        gtk_widget_queue_draw(c->drawing_area);
}

/* Keyboard shortcuts */
static gboolean
on_key_pressed(GtkEventControllerKey *ctrl, guint keyval,
               guint keycode, GdkModifierType state, gpointer userdata)
{
    (void)ctrl; (void)keycode; (void)state;
    DC_PcbCanvas *c = userdata;

    switch (keyval) {
    case GDK_KEY_Escape:
        if (c->routing) {
            c->routing = 0;
            gtk_widget_queue_draw(c->drawing_area);
        } else {
            c->sel_type = DC_PCB_SEL_NONE;
            c->sel_index = -1;
            if (c->editor) dc_pcb_editor_set_mode(c->editor, DC_PCB_MODE_SELECT);
            gtk_widget_queue_draw(c->drawing_area);
        }
        return TRUE;

    case GDK_KEY_x: case GDK_KEY_X:
        if (c->editor) dc_pcb_editor_set_mode(c->editor, DC_PCB_MODE_ROUTE);
        return TRUE;

    case GDK_KEY_v: case GDK_KEY_V:
        if (c->routing && c->pcb) {
            /* Insert via mid-route and switch layer */
            DC_PcbDesignRules *dr = dc_epcb_get_design_rules(c->pcb);
            double vs = dr ? dr->via_size : 0.8;
            double vd = dr ? dr->via_drill : 0.4;

            /* Commit segment to via location */
            double swx = c->cursor_wx, swy = c->cursor_wy;
            if (swx != c->route_start_x || swy != c->route_start_y) {
                double tw = dr ? dr->track_width : 0.25;
                dc_epcb_add_track(c->pcb, c->route_start_x, c->route_start_y,
                                    swx, swy, tw, c->active_layer, c->route_net_id);
            }
            dc_epcb_add_via(c->pcb, swx, swy, vs, vd, c->route_net_id);
            c->route_start_x = swx;
            c->route_start_y = swy;

            /* Switch layer */
            c->active_layer = (c->active_layer == DC_PCB_LAYER_F_CU)
                              ? DC_PCB_LAYER_B_CU : DC_PCB_LAYER_F_CU;
            if (c->editor) dc_pcb_editor_update_ratsnest(c->editor);
            gtk_widget_queue_draw(c->drawing_area);
        } else {
            if (c->editor) dc_pcb_editor_set_mode(c->editor, DC_PCB_MODE_PLACE_VIA);
        }
        return TRUE;

    case GDK_KEY_f: case GDK_KEY_F:
        flip_selected(c);
        return TRUE;

    case GDK_KEY_m: case GDK_KEY_M:
        /* Move mode — same as select for now */
        if (c->editor) dc_pcb_editor_set_mode(c->editor, DC_PCB_MODE_SELECT);
        return TRUE;

    case GDK_KEY_r: case GDK_KEY_R:
        rotate_selected(c);
        return TRUE;

    case GDK_KEY_Delete: case GDK_KEY_BackSpace:
        delete_selected(c);
        return TRUE;

    case GDK_KEY_plus: case GDK_KEY_equal: {
        /* Switch to next copper layer */
        int layer = c->active_layer;
        if (layer == DC_PCB_LAYER_F_CU) layer = DC_PCB_LAYER_B_CU;
        else layer = DC_PCB_LAYER_F_CU;
        c->active_layer = layer;
        gtk_widget_queue_draw(c->drawing_area);
        return TRUE;
    }
    case GDK_KEY_minus: {
        int layer = c->active_layer;
        if (layer == DC_PCB_LAYER_B_CU) layer = DC_PCB_LAYER_F_CU;
        else layer = DC_PCB_LAYER_B_CU;
        c->active_layer = layer;
        gtk_widget_queue_draw(c->drawing_area);
        return TRUE;
    }

    default:
        return FALSE;
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
DC_PcbCanvas *dc_pcb_canvas_new(void)
{
    DC_PcbCanvas *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->zoom = PCB_ZOOM_DEFAULT;
    c->sel_index = -1;
    c->active_layer = DC_PCB_LAYER_F_CU;
    memset(c->layer_visible, 1, sizeof(c->layer_visible));

    c->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(c->drawing_area, TRUE);
    gtk_widget_set_vexpand(c->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(c->drawing_area),
                                    on_draw, c, NULL);

    /* Scroll to zoom */
    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), c);
    gtk_widget_add_controller(c->drawing_area, scroll);

    /* Button 1: click for select/place */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_click_pressed), c);
    g_signal_connect(click, "released", G_CALLBACK(on_click_released), c);
    gtk_widget_add_controller(c->drawing_area, GTK_EVENT_CONTROLLER(click));

    /* Button 2: drag for pan */
    GtkGesture *pan_drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(pan_drag), 2);
    g_signal_connect(pan_drag, "drag-begin", G_CALLBACK(on_pan_begin), c);
    g_signal_connect(pan_drag, "drag-update", G_CALLBACK(on_pan_update), c);
    g_signal_connect(pan_drag, "drag-end", G_CALLBACK(on_pan_end), c);
    gtk_widget_add_controller(c->drawing_area, GTK_EVENT_CONTROLLER(pan_drag));

    /* Group gestures */
    gtk_gesture_group(GTK_GESTURE(click), GTK_GESTURE(pan_drag));

    /* Motion for cursor tracking */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), c);
    gtk_widget_add_controller(c->drawing_area, motion);

    /* Key controller for shortcuts */
    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), c);
    gtk_widget_add_controller(c->drawing_area, key);

    gtk_widget_set_focusable(c->drawing_area, TRUE);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "PCB canvas created");
    return c;
}

void dc_pcb_canvas_free(DC_PcbCanvas *c)
{
    if (!c) return;
    free(c);
}

GtkWidget *dc_pcb_canvas_widget(DC_PcbCanvas *c)
{
    return c ? c->drawing_area : NULL;
}

void dc_pcb_canvas_set_pcb(DC_PcbCanvas *c, DC_EPcb *pcb)
{
    if (!c) return;
    c->pcb = pcb;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_pcb_canvas_set_library(DC_PcbCanvas *c, DC_ELibrary *lib)
{
    if (c) c->lib = lib;
}

void dc_pcb_canvas_set_ratsnest(DC_PcbCanvas *c, DC_Ratsnest *rn)
{
    if (!c) return;
    c->ratsnest = rn;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_pcb_canvas_set_editor(DC_PcbCanvas *c, DC_PcbEditor *editor)
{
    if (c) c->editor = editor;
}

void dc_pcb_canvas_set_zoom(DC_PcbCanvas *c, double zoom)
{
    if (!c) return;
    if (zoom < PCB_ZOOM_MIN) zoom = PCB_ZOOM_MIN;
    if (zoom > PCB_ZOOM_MAX) zoom = PCB_ZOOM_MAX;
    c->zoom = zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

double dc_pcb_canvas_get_zoom(const DC_PcbCanvas *c) { return c ? c->zoom : 1.0; }

void dc_pcb_canvas_set_pan(DC_PcbCanvas *c, double x, double y)
{
    if (!c) return;
    c->pan_x = x; c->pan_y = y;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_pcb_canvas_get_pan(const DC_PcbCanvas *c, double *x, double *y)
{
    if (!c) return;
    if (x) *x = c->pan_x;
    if (y) *y = c->pan_y;
}

void dc_pcb_canvas_queue_redraw(DC_PcbCanvas *c)
{
    if (c && c->drawing_area) gtk_widget_queue_draw(c->drawing_area);
}

void dc_pcb_canvas_set_layer_visible(DC_PcbCanvas *c, int layer_id, int visible)
{
    if (!c || layer_id < 0 || layer_id >= DC_PCB_LAYER_COUNT) return;
    c->layer_visible[layer_id] = (unsigned char)(visible ? 1 : 0);
    gtk_widget_queue_draw(c->drawing_area);
}

int dc_pcb_canvas_get_layer_visible(const DC_PcbCanvas *c, int layer_id)
{
    if (!c || layer_id < 0 || layer_id >= DC_PCB_LAYER_COUNT) return 0;
    return c->layer_visible[layer_id];
}

void dc_pcb_canvas_set_active_layer(DC_PcbCanvas *c, int layer_id)
{
    if (c) c->active_layer = layer_id;
}

int dc_pcb_canvas_get_active_layer(const DC_PcbCanvas *c)
{
    return c ? c->active_layer : DC_PCB_LAYER_F_CU;
}

/* =========================================================================
 * Selection API
 * ========================================================================= */
DC_PcbSelType dc_pcb_canvas_get_sel_type(const DC_PcbCanvas *c)
{
    return c ? c->sel_type : DC_PCB_SEL_NONE;
}

int dc_pcb_canvas_get_sel_index(const DC_PcbCanvas *c)
{
    return c ? c->sel_index : -1;
}

void dc_pcb_canvas_select(DC_PcbCanvas *c, DC_PcbSelType type, int index)
{
    if (!c) return;
    c->sel_type = type;
    c->sel_index = index;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_pcb_canvas_deselect(DC_PcbCanvas *c)
{
    if (!c) return;
    c->sel_type = DC_PCB_SEL_NONE;
    c->sel_index = -1;
    gtk_widget_queue_draw(c->drawing_area);
}

/* Legacy compat */
int dc_pcb_canvas_get_selected_footprint(const DC_PcbCanvas *c)
{
    if (!c || c->sel_type != DC_PCB_SEL_FOOTPRINT) return -1;
    return c->sel_index;
}

void dc_pcb_canvas_set_selected_footprint(DC_PcbCanvas *c, int i)
{
    if (!c) return;
    if (i >= 0) {
        c->sel_type = DC_PCB_SEL_FOOTPRINT;
        c->sel_index = i;
    } else {
        c->sel_type = DC_PCB_SEL_NONE;
        c->sel_index = -1;
    }
    gtk_widget_queue_draw(c->drawing_area);
}

int dc_pcb_canvas_render_to_png(DC_PcbCanvas *c, const char *path, int width, int height)
{
    if (!c || !path) return -1;
    if (width <= 0) width = gtk_widget_get_width(c->drawing_area);
    if (height <= 0) height = gtk_widget_get_height(c->drawing_area);
    if (width <= 0 || height <= 0) return -1;

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);
    on_draw(GTK_DRAWING_AREA(c->drawing_area), cr, width, height, c);
    cairo_status_t status = cairo_surface_write_to_png(surface, path);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return (status == CAIRO_STATUS_SUCCESS) ? 0 : -1;
}
