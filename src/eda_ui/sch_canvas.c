#define _POSIX_C_SOURCE 200809L

#include "sch_canvas.h"
#include "sch_editor.h"
#include "sch_symbol_render.h"
#include "eda/eda_schematic.h"
#include "eda/eda_library.h"
#include "core/log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Constants
 * ========================================================================= */
#define SCH_GRID_MILS    50.0    /* 50-mil (1.27mm) grid — KiCad standard */
#define SCH_ZOOM_MIN     0.1
#define SCH_ZOOM_MAX     20.0
#define SCH_ZOOM_DEFAULT 2.0     /* pixels per mil */

#define SCH_HIT_RADIUS_PX  8.0  /* hit test radius in screen pixels */
#define SCH_SYMBOL_HALF_W  100.0 /* default symbol bbox half-width (mils) */
#define SCH_SYMBOL_HALF_H  100.0
#define SCH_WIRE_HIT_PX    6.0  /* wire hit distance in screen pixels */
#define SCH_JUNCTION_R     25.0 /* junction hit radius (mils) */
#define SCH_LABEL_W        150.0
#define SCH_LABEL_H        50.0
#define SCH_POWER_R        30.0

/* =========================================================================
 * Internal state
 * ========================================================================= */
struct DC_SchCanvas {
    GtkWidget      *drawing_area;

    /* View */
    double          zoom;        /* pixels per mil */
    double          pan_x;       /* world X center (mils) */
    double          pan_y;       /* world Y center (mils) */

    /* Data (borrowed) */
    DC_ESchematic  *sch;
    DC_ELibrary    *lib;
    DC_SchEditor   *editor;      /* back-pointer for mode queries */

    /* Selection */
    DC_SchSelType   sel_type;
    int             sel_index;   /* -1 = none */

    /* Cursor */
    double          cursor_wx, cursor_wy; /* world coords, grid-snapped */
    double          cursor_sx, cursor_sy; /* raw screen coords */

    /* Pan state (button 2 drag) */
    int             panning;
    double          pan_drag_sx, pan_drag_sy;
    double          pan_drag_wx, pan_drag_wy;

    /* Move-drag state (button 1 in select mode) */
    int             moving;
    double          move_start_wx, move_start_wy;
    double          move_orig_x, move_orig_y;      /* element original pos */
    double          move_orig_x2, move_orig_y2;    /* wire endpoint 2 */

    /* Wire drawing state */
    int             wire_drawing;
    double          wire_start_x, wire_start_y;
};

/* =========================================================================
 * Helpers
 * ========================================================================= */
static double snap_to_grid(double v)
{
    return round(v / SCH_GRID_MILS) * SCH_GRID_MILS;
}

static DC_SchEditMode get_mode(DC_SchCanvas *c)
{
    if (c->editor) return dc_sch_editor_get_mode(c->editor);
    return DC_SCH_MODE_SELECT;
}

/* =========================================================================
 * Coordinate transforms
 * ========================================================================= */
void
dc_sch_canvas_screen_to_world(DC_SchCanvas *c,
                               double sx, double sy,
                               double *wx, double *wy)
{
    if (!c) return;
    int w = gtk_widget_get_width(c->drawing_area);
    int h = gtk_widget_get_height(c->drawing_area);
    if (wx) *wx = (sx - w / 2.0) / c->zoom + c->pan_x;
    if (wy) *wy = (sy - h / 2.0) / c->zoom + c->pan_y;
}

void
dc_sch_canvas_world_to_screen(DC_SchCanvas *c,
                               double wx, double wy,
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
static int
sch_hit_symbol(DC_SchCanvas *c, double wx, double wy)
{
    if (!c->sch) return -1;
    double r = SCH_HIT_RADIUS_PX / c->zoom;
    double hw = SCH_SYMBOL_HALF_W + r;
    double hh = SCH_SYMBOL_HALF_H + r;
    for (size_t i = 0; i < dc_eschematic_symbol_count(c->sch); i++) {
        DC_SchSymbol *sym = dc_eschematic_get_symbol(c->sch, i);
        if (fabs(wx - sym->x) < hw && fabs(wy - sym->y) < hh)
            return (int)i;
    }
    return -1;
}

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
sch_hit_wire(DC_SchCanvas *c, double wx, double wy)
{
    if (!c->sch) return -1;
    double threshold = SCH_WIRE_HIT_PX / c->zoom;
    double best = threshold;
    int hit = -1;
    for (size_t i = 0; i < dc_eschematic_wire_count(c->sch); i++) {
        DC_SchWire *w = dc_eschematic_get_wire(c->sch, i);
        double d = point_to_segment_dist(wx, wy, w->x1, w->y1, w->x2, w->y2);
        if (d < best) {
            best = d;
            hit = (int)i;
        }
    }
    return hit;
}

static int
sch_hit_junction(DC_SchCanvas *c, double wx, double wy)
{
    if (!c->sch) return -1;
    double r = SCH_JUNCTION_R + SCH_HIT_RADIUS_PX / c->zoom;
    for (size_t i = 0; i < dc_eschematic_junction_count(c->sch); i++) {
        DC_SchJunction *j = dc_eschematic_get_junction(c->sch, i);
        double dx = wx - j->x, dy = wy - j->y;
        if (dx * dx + dy * dy < r * r)
            return (int)i;
    }
    return -1;
}

static int
sch_hit_label(DC_SchCanvas *c, double wx, double wy)
{
    if (!c->sch) return -1;
    double r = SCH_HIT_RADIUS_PX / c->zoom;
    for (size_t i = 0; i < dc_eschematic_label_count(c->sch); i++) {
        DC_SchLabel *l = dc_eschematic_get_label(c->sch, i);
        if (wx >= l->x - r && wx <= l->x + SCH_LABEL_W + r &&
            wy >= l->y - SCH_LABEL_H - r && wy <= l->y + r)
            return (int)i;
    }
    return -1;
}

static int
sch_hit_power_port(DC_SchCanvas *c, double wx, double wy)
{
    if (!c->sch) return -1;
    double r = SCH_POWER_R + SCH_HIT_RADIUS_PX / c->zoom;
    for (size_t i = 0; i < dc_eschematic_power_port_count(c->sch); i++) {
        DC_SchPowerPort *pp = dc_eschematic_get_power_port(c->sch, i);
        double dx = wx - pp->x, dy = wy - pp->y;
        if (dx * dx + dy * dy < r * r)
            return (int)i;
    }
    return -1;
}

/* Combined hit test — returns type + index via out params */
static void
sch_hit_any(DC_SchCanvas *c, double wx, double wy,
            DC_SchSelType *out_type, int *out_index)
{
    int idx;
    /* Priority: symbol > junction > label > power port > wire */
    if ((idx = sch_hit_symbol(c, wx, wy)) >= 0) {
        *out_type = DC_SCH_SEL_SYMBOL; *out_index = idx; return;
    }
    if ((idx = sch_hit_junction(c, wx, wy)) >= 0) {
        *out_type = DC_SCH_SEL_JUNCTION; *out_index = idx; return;
    }
    if ((idx = sch_hit_label(c, wx, wy)) >= 0) {
        *out_type = DC_SCH_SEL_LABEL; *out_index = idx; return;
    }
    if ((idx = sch_hit_power_port(c, wx, wy)) >= 0) {
        *out_type = DC_SCH_SEL_POWER_PORT; *out_index = idx; return;
    }
    if ((idx = sch_hit_wire(c, wx, wy)) >= 0) {
        *out_type = DC_SCH_SEL_WIRE; *out_index = idx; return;
    }
    *out_type = DC_SCH_SEL_NONE;
    *out_index = -1;
}

/* =========================================================================
 * Selection helpers
 * ========================================================================= */
static void
get_sel_position(DC_SchCanvas *c, double *x, double *y,
                 double *x2, double *y2)
{
    if (!c->sch || c->sel_index < 0) return;
    switch (c->sel_type) {
    case DC_SCH_SEL_SYMBOL: {
        DC_SchSymbol *s = dc_eschematic_get_symbol(c->sch, (size_t)c->sel_index);
        if (s) { *x = s->x; *y = s->y; }
    } break;
    case DC_SCH_SEL_WIRE: {
        DC_SchWire *w = dc_eschematic_get_wire(c->sch, (size_t)c->sel_index);
        if (w) { *x = w->x1; *y = w->y1; if (x2) *x2 = w->x2; if (y2) *y2 = w->y2; }
    } break;
    case DC_SCH_SEL_JUNCTION: {
        DC_SchJunction *j = dc_eschematic_get_junction(c->sch, (size_t)c->sel_index);
        if (j) { *x = j->x; *y = j->y; }
    } break;
    case DC_SCH_SEL_LABEL: {
        DC_SchLabel *l = dc_eschematic_get_label(c->sch, (size_t)c->sel_index);
        if (l) { *x = l->x; *y = l->y; }
    } break;
    case DC_SCH_SEL_POWER_PORT: {
        DC_SchPowerPort *pp = dc_eschematic_get_power_port(c->sch, (size_t)c->sel_index);
        if (pp) { *x = pp->x; *y = pp->y; }
    } break;
    default: break;
    }
}

static void
set_sel_position(DC_SchCanvas *c, double x, double y,
                 double x2, double y2)
{
    if (!c->sch || c->sel_index < 0) return;
    switch (c->sel_type) {
    case DC_SCH_SEL_SYMBOL: {
        DC_SchSymbol *s = dc_eschematic_get_symbol(c->sch, (size_t)c->sel_index);
        if (s) { s->x = x; s->y = y; }
    } break;
    case DC_SCH_SEL_WIRE: {
        DC_SchWire *w = dc_eschematic_get_wire(c->sch, (size_t)c->sel_index);
        if (w) { w->x1 = x; w->y1 = y; w->x2 = x2; w->y2 = y2; }
    } break;
    case DC_SCH_SEL_JUNCTION: {
        DC_SchJunction *j = dc_eschematic_get_junction(c->sch, (size_t)c->sel_index);
        if (j) { j->x = x; j->y = y; }
    } break;
    case DC_SCH_SEL_LABEL: {
        DC_SchLabel *l = dc_eschematic_get_label(c->sch, (size_t)c->sel_index);
        if (l) { l->x = x; l->y = y; }
    } break;
    case DC_SCH_SEL_POWER_PORT: {
        DC_SchPowerPort *pp = dc_eschematic_get_power_port(c->sch, (size_t)c->sel_index);
        if (pp) { pp->x = x; pp->y = y; }
    } break;
    default: break;
    }
}

static void
rotate_selected(DC_SchCanvas *c)
{
    if (!c->sch || c->sel_index < 0) return;
    if (c->sel_type == DC_SCH_SEL_SYMBOL) {
        DC_SchSymbol *s = dc_eschematic_get_symbol(c->sch, (size_t)c->sel_index);
        if (s) { s->angle = fmod(s->angle + 90.0, 360.0); }
    } else if (c->sel_type == DC_SCH_SEL_LABEL) {
        DC_SchLabel *l = dc_eschematic_get_label(c->sch, (size_t)c->sel_index);
        if (l) { l->angle = fmod(l->angle + 90.0, 360.0); }
    } else if (c->sel_type == DC_SCH_SEL_POWER_PORT) {
        DC_SchPowerPort *pp = dc_eschematic_get_power_port(c->sch, (size_t)c->sel_index);
        if (pp) { pp->angle = fmod(pp->angle + 90.0, 360.0); }
    }
    gtk_widget_queue_draw(c->drawing_area);
}

static void
mirror_selected(DC_SchCanvas *c)
{
    if (!c->sch || c->sel_index < 0) return;
    if (c->sel_type == DC_SCH_SEL_SYMBOL) {
        DC_SchSymbol *s = dc_eschematic_get_symbol(c->sch, (size_t)c->sel_index);
        if (s) s->mirror = !s->mirror;
    }
    gtk_widget_queue_draw(c->drawing_area);
}

static void
delete_selected(DC_SchCanvas *c)
{
    if (!c->sch || c->sel_index < 0) return;
    size_t idx = (size_t)c->sel_index;
    switch (c->sel_type) {
    case DC_SCH_SEL_SYMBOL:     dc_eschematic_remove_symbol(c->sch, idx); break;
    case DC_SCH_SEL_WIRE:       dc_eschematic_remove_wire(c->sch, idx); break;
    case DC_SCH_SEL_JUNCTION:   dc_eschematic_remove_junction(c->sch, idx); break;
    case DC_SCH_SEL_LABEL:      dc_eschematic_remove_label(c->sch, idx); break;
    case DC_SCH_SEL_POWER_PORT: dc_eschematic_remove_power_port(c->sch, idx); break;
    default: return;
    }
    c->sel_type = DC_SCH_SEL_NONE;
    c->sel_index = -1;
    gtk_widget_queue_draw(c->drawing_area);
}

/* =========================================================================
 * Grid rendering
 * ========================================================================= */
static void
draw_grid(DC_SchCanvas *c, cairo_t *cr, int width, int height)
{
    double wl, wt, wr, wb;
    dc_sch_canvas_screen_to_world(c, 0, 0, &wl, &wt);
    dc_sch_canvas_screen_to_world(c, width, height, &wr, &wb);

    double grid = SCH_GRID_MILS;
    double screen_grid = grid * c->zoom;
    while (screen_grid < 10.0) {
        grid *= 2.0;
        screen_grid *= 2.0;
    }

    /* Minor grid dots */
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.3);
    double start_x = floor(wl / grid) * grid;
    double start_y = floor(wt / grid) * grid;
    for (double gy = start_y; gy <= wb; gy += grid) {
        for (double gx = start_x; gx <= wr; gx += grid) {
            double sx, sy;
            dc_sch_canvas_world_to_screen(c, gx, gy, &sx, &sy);
            cairo_rectangle(cr, sx - 0.5, sy - 0.5, 1, 1);
        }
    }
    cairo_fill(cr);

    /* Major grid lines (every 200 mils = 4 grid steps) */
    double major = grid * 4.0;
    cairo_set_source_rgba(cr, 0.4, 0.4, 0.4, 0.2);
    cairo_set_line_width(cr, 1.0);

    double ms_x = floor(wl / major) * major;
    for (double gx = ms_x; gx <= wr; gx += major) {
        double sx, sy_unused;
        dc_sch_canvas_world_to_screen(c, gx, 0, &sx, &sy_unused);
        cairo_move_to(cr, sx, 0);
        cairo_line_to(cr, sx, height);
    }
    double ms_y = floor(wt / major) * major;
    for (double gy = ms_y; gy <= wb; gy += major) {
        double sx_unused, sy;
        dc_sch_canvas_world_to_screen(c, 0, gy, &sx_unused, &sy);
        cairo_move_to(cr, 0, sy);
        cairo_line_to(cr, width, sy);
    }
    cairo_stroke(cr);

    /* Origin crosshair */
    cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.4);
    double ox, oy;
    dc_sch_canvas_world_to_screen(c, 0, 0, &ox, &oy);
    cairo_move_to(cr, ox, 0);
    cairo_line_to(cr, ox, height);
    cairo_move_to(cr, 0, oy);
    cairo_line_to(cr, width, oy);
    cairo_stroke(cr);
}

/* =========================================================================
 * Schematic element rendering
 * ========================================================================= */
static void
draw_schematic(DC_SchCanvas *c, cairo_t *cr)
{
    if (!c->sch) return;

    /* Draw wires */
    for (size_t i = 0; i < dc_eschematic_wire_count(c->sch); i++) {
        DC_SchWire *w = dc_eschematic_get_wire(c->sch, i);
        int selected = (c->sel_type == DC_SCH_SEL_WIRE && (int)i == c->sel_index);
        if (selected)
            cairo_set_source_rgb(cr, 0.3, 0.9, 1.0);
        else
            cairo_set_source_rgb(cr, 0.0, 0.6, 0.0);
        cairo_set_line_width(cr, selected ? 3.0 : 2.0);
        double sx1, sy1, sx2, sy2;
        dc_sch_canvas_world_to_screen(c, w->x1, w->y1, &sx1, &sy1);
        dc_sch_canvas_world_to_screen(c, w->x2, w->y2, &sx2, &sy2);
        cairo_move_to(cr, sx1, sy1);
        cairo_line_to(cr, sx2, sy2);
        cairo_stroke(cr);
    }

    /* Draw junctions */
    for (size_t i = 0; i < dc_eschematic_junction_count(c->sch); i++) {
        DC_SchJunction *j = dc_eschematic_get_junction(c->sch, i);
        int selected = (c->sel_type == DC_SCH_SEL_JUNCTION && (int)i == c->sel_index);
        if (selected)
            cairo_set_source_rgb(cr, 0.3, 0.9, 1.0);
        else
            cairo_set_source_rgb(cr, 0.0, 0.6, 0.0);
        double sx, sy;
        dc_sch_canvas_world_to_screen(c, j->x, j->y, &sx, &sy);
        cairo_arc(cr, sx, sy, selected ? 6.0 : 4.0, 0, 2 * G_PI);
        cairo_fill(cr);
    }

    /* Draw symbols */
    for (size_t i = 0; i < dc_eschematic_symbol_count(c->sch); i++) {
        DC_SchSymbol *sym = dc_eschematic_get_symbol(c->sch, i);
        int selected = (c->sel_type == DC_SCH_SEL_SYMBOL && (int)i == c->sel_index);
        dc_sch_symbol_render(cr, c, sym, c->lib, selected);
    }

    /* Draw labels */
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);
    for (size_t i = 0; i < dc_eschematic_label_count(c->sch); i++) {
        DC_SchLabel *l = dc_eschematic_get_label(c->sch, i);
        int selected = (c->sel_type == DC_SCH_SEL_LABEL && (int)i == c->sel_index);
        if (selected)
            cairo_set_source_rgb(cr, 0.3, 0.9, 1.0);
        else
            cairo_set_source_rgb(cr, 0.2, 0.2, 0.8);
        double sx, sy;
        dc_sch_canvas_world_to_screen(c, l->x, l->y, &sx, &sy);
        cairo_move_to(cr, sx, sy - 4);
        cairo_show_text(cr, l->name);
    }

    /* Draw power ports */
    for (size_t i = 0; i < dc_eschematic_power_port_count(c->sch); i++) {
        DC_SchPowerPort *pp = dc_eschematic_get_power_port(c->sch, i);
        int selected = (c->sel_type == DC_SCH_SEL_POWER_PORT && (int)i == c->sel_index);
        if (selected)
            cairo_set_source_rgb(cr, 0.3, 0.9, 1.0);
        else
            cairo_set_source_rgb(cr, 0.8, 0.0, 0.0);
        double sx, sy;
        dc_sch_canvas_world_to_screen(c, pp->x, pp->y, &sx, &sy);
        cairo_arc(cr, sx, sy, 5.0, 0, 2 * G_PI);
        cairo_stroke(cr);
        cairo_move_to(cr, sx + 8, sy + 4);
        cairo_show_text(cr, pp->name);
    }
}

/* =========================================================================
 * Overlay rendering (cursor crosshair, wire preview)
 * ========================================================================= */
static void
draw_overlay(DC_SchCanvas *c, cairo_t *cr, int width, int height)
{
    DC_SchEditMode mode = get_mode(c);

    /* Crosshair cursor at grid-snapped position */
    if (mode == DC_SCH_MODE_WIRE || mode == DC_SCH_MODE_PLACE_SYMBOL ||
        mode == DC_SCH_MODE_PLACE_LABEL) {
        double sx, sy;
        dc_sch_canvas_world_to_screen(c, c->cursor_wx, c->cursor_wy, &sx, &sy);
        cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 0.5);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, sx, 0);
        cairo_line_to(cr, sx, height);
        cairo_move_to(cr, 0, sy);
        cairo_line_to(cr, width, sy);
        cairo_stroke(cr);
    }

    /* Wire preview (green dashed) */
    if (c->wire_drawing) {
        double sx1, sy1, sx2, sy2;
        dc_sch_canvas_world_to_screen(c, c->wire_start_x, c->wire_start_y, &sx1, &sy1);
        dc_sch_canvas_world_to_screen(c, c->cursor_wx, c->cursor_wy, &sx2, &sy2);
        cairo_set_source_rgba(cr, 0.0, 0.9, 0.0, 0.7);
        cairo_set_line_width(cr, 2.0);
        double dash[] = { 6.0, 4.0 };
        cairo_set_dash(cr, dash, 2, 0);
        cairo_move_to(cr, sx1, sy1);
        cairo_line_to(cr, sx2, sy2);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0);
    }
}

/* =========================================================================
 * Draw callback
 * ========================================================================= */
static void
on_draw(GtkDrawingArea *area, cairo_t *cr,
        int width, int height, gpointer userdata)
{
    (void)area;
    DC_SchCanvas *c = userdata;

    /* Background */
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.14);
    cairo_paint(cr);

    draw_grid(c, cr, width, height);
    draw_schematic(c, cr);
    draw_overlay(c, cr, width, height);
}

/* =========================================================================
 * Event handlers — mode-aware dispatch
 * ========================================================================= */
static void
on_scroll(GtkEventControllerScroll *ctrl, double dx, double dy,
          gpointer userdata)
{
    (void)ctrl; (void)dx;
    DC_SchCanvas *c = userdata;

    double factor = (dy < 0) ? 1.15 : 1.0 / 1.15;
    double new_zoom = c->zoom * factor;
    if (new_zoom < SCH_ZOOM_MIN) new_zoom = SCH_ZOOM_MIN;
    if (new_zoom > SCH_ZOOM_MAX) new_zoom = SCH_ZOOM_MAX;
    c->zoom = new_zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

/* Button 1 click — mode-aware dispatch */
static void
on_click_pressed(GtkGestureClick *gesture, int n_press,
                 double x, double y, gpointer userdata)
{
    (void)gesture;
    DC_SchCanvas *c = userdata;

    gtk_widget_grab_focus(c->drawing_area);

    double wx, wy;
    dc_sch_canvas_screen_to_world(c, x, y, &wx, &wy);
    double swx = snap_to_grid(wx);
    double swy = snap_to_grid(wy);

    DC_SchEditMode mode = get_mode(c);

    switch (mode) {
    case DC_SCH_MODE_SELECT:
    case DC_SCH_MODE_MOVE: {
        DC_SchSelType type;
        int idx;
        sch_hit_any(c, wx, wy, &type, &idx);
        if (type != DC_SCH_SEL_NONE) {
            c->sel_type = type;
            c->sel_index = idx;
            /* Start move-drag */
            c->moving = 1;
            c->move_start_wx = wx;
            c->move_start_wy = wy;
            get_sel_position(c, &c->move_orig_x, &c->move_orig_y,
                             &c->move_orig_x2, &c->move_orig_y2);
        } else {
            /* Click on empty space — deselect */
            c->sel_type = DC_SCH_SEL_NONE;
            c->sel_index = -1;
        }
        gtk_widget_queue_draw(c->drawing_area);
    } break;

    case DC_SCH_MODE_WIRE: {
        if (!c->wire_drawing) {
            /* Start wire */
            c->wire_drawing = 1;
            c->wire_start_x = swx;
            c->wire_start_y = swy;
        } else {
            /* Commit wire segment */
            if (c->sch && (swx != c->wire_start_x || swy != c->wire_start_y)) {
                dc_eschematic_add_wire(c->sch, c->wire_start_x, c->wire_start_y,
                                        swx, swy);

                /* Auto-junction: check if endpoint lands on existing wire midpoint */
                for (size_t i = 0; i < dc_eschematic_wire_count(c->sch); i++) {
                    DC_SchWire *w = dc_eschematic_get_wire(c->sch, i);
                    /* Skip the wire we just added (last one) */
                    if (i == dc_eschematic_wire_count(c->sch) - 1) continue;
                    double d = point_to_segment_dist(swx, swy,
                                                     w->x1, w->y1, w->x2, w->y2);
                    if (d < 1.0) {
                        /* Check it's not an endpoint */
                        int is_endpoint = (fabs(swx - w->x1) < 1.0 && fabs(swy - w->y1) < 1.0) ||
                                          (fabs(swx - w->x2) < 1.0 && fabs(swy - w->y2) < 1.0);
                        if (!is_endpoint) {
                            dc_eschematic_add_junction(c->sch, swx, swy);
                        }
                    }
                }
            }
            /* Continue chain from endpoint */
            c->wire_start_x = swx;
            c->wire_start_y = swy;

            /* Double-click ends chain */
            if (n_press >= 2) {
                c->wire_drawing = 0;
            }
        }
        gtk_widget_queue_draw(c->drawing_area);
    } break;

    case DC_SCH_MODE_PLACE_SYMBOL:
    case DC_SCH_MODE_PLACE_LABEL:
        /* Placement handled by editor via library browser — just record position */
        break;
    }
}

static void
on_click_released(GtkGestureClick *gesture, int n_press,
                  double x, double y, gpointer userdata)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    DC_SchCanvas *c = userdata;
    c->moving = 0;
}

/* Button 2 drag — pan */
static void
on_pan_begin(GtkGestureDrag *gesture, double x, double y, gpointer userdata)
{
    (void)gesture;
    DC_SchCanvas *c = userdata;
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
    DC_SchCanvas *c = userdata;
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
    DC_SchCanvas *c = userdata;
    c->panning = 0;
}

/* Motion — cursor tracking + move-drag */
static void
on_motion(GtkEventControllerMotion *ctrl, double x, double y,
          gpointer userdata)
{
    (void)ctrl;
    DC_SchCanvas *c = userdata;

    c->cursor_sx = x;
    c->cursor_sy = y;
    dc_sch_canvas_screen_to_world(c, x, y, &c->cursor_wx, &c->cursor_wy);
    c->cursor_wx = snap_to_grid(c->cursor_wx);
    c->cursor_wy = snap_to_grid(c->cursor_wy);

    /* Move-drag selected element */
    if (c->moving && c->sel_index >= 0) {
        double wx, wy;
        dc_sch_canvas_screen_to_world(c, x, y, &wx, &wy);
        double dx = snap_to_grid(wx) - snap_to_grid(c->move_start_wx);
        double dy = snap_to_grid(wy) - snap_to_grid(c->move_start_wy);
        double new_x = c->move_orig_x + dx;
        double new_y = c->move_orig_y + dy;
        double new_x2 = c->move_orig_x2 + dx;
        double new_y2 = c->move_orig_y2 + dy;
        set_sel_position(c, new_x, new_y, new_x2, new_y2);
    }

    /* Redraw for crosshair/preview updates */
    DC_SchEditMode mode = get_mode(c);
    if (c->moving || c->wire_drawing ||
        mode == DC_SCH_MODE_WIRE || mode == DC_SCH_MODE_PLACE_SYMBOL ||
        mode == DC_SCH_MODE_PLACE_LABEL)
        gtk_widget_queue_draw(c->drawing_area);
}

/* Keyboard shortcuts */
static gboolean
on_key_pressed(GtkEventControllerKey *ctrl, guint keyval,
               guint keycode, GdkModifierType state, gpointer userdata)
{
    (void)ctrl; (void)keycode; (void)state;
    DC_SchCanvas *c = userdata;

    switch (keyval) {
    case GDK_KEY_Escape:
        if (c->wire_drawing) {
            c->wire_drawing = 0;
            gtk_widget_queue_draw(c->drawing_area);
        } else {
            c->sel_type = DC_SCH_SEL_NONE;
            c->sel_index = -1;
            if (c->editor) dc_sch_editor_set_mode(c->editor, DC_SCH_MODE_SELECT);
            gtk_widget_queue_draw(c->drawing_area);
        }
        return TRUE;

    case GDK_KEY_w: case GDK_KEY_W:
        if (c->editor) dc_sch_editor_set_mode(c->editor, DC_SCH_MODE_WIRE);
        return TRUE;

    case GDK_KEY_a: case GDK_KEY_A:
        if (c->editor) dc_sch_editor_set_mode(c->editor, DC_SCH_MODE_PLACE_SYMBOL);
        return TRUE;

    case GDK_KEY_l: case GDK_KEY_L:
        if (c->editor) dc_sch_editor_set_mode(c->editor, DC_SCH_MODE_PLACE_LABEL);
        return TRUE;

    case GDK_KEY_r: case GDK_KEY_R:
        rotate_selected(c);
        return TRUE;

    case GDK_KEY_x: case GDK_KEY_X:
        mirror_selected(c);
        return TRUE;

    case GDK_KEY_m: case GDK_KEY_M:
        if (c->editor) dc_sch_editor_set_mode(c->editor, DC_SCH_MODE_MOVE);
        return TRUE;

    case GDK_KEY_Delete: case GDK_KEY_BackSpace:
        delete_selected(c);
        return TRUE;

    default:
        return FALSE;
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
DC_SchCanvas *
dc_sch_canvas_new(void)
{
    DC_SchCanvas *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->zoom = SCH_ZOOM_DEFAULT;
    c->sel_index = -1;

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

    /* Group click and pan gestures */
    gtk_gesture_group(GTK_GESTURE(click), GTK_GESTURE(pan_drag));

    /* Motion for cursor tracking + drag */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), c);
    gtk_widget_add_controller(c->drawing_area, motion);

    /* Key controller for shortcuts */
    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), c);
    gtk_widget_add_controller(c->drawing_area, key);

    gtk_widget_set_focusable(c->drawing_area, TRUE);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Schematic canvas created");
    return c;
}

void
dc_sch_canvas_free(DC_SchCanvas *c)
{
    if (!c) return;
    free(c);
}

GtkWidget *
dc_sch_canvas_widget(DC_SchCanvas *c)
{
    return c ? c->drawing_area : NULL;
}

void dc_sch_canvas_set_schematic(DC_SchCanvas *c, DC_ESchematic *sch)
{
    if (!c) return;
    c->sch = sch;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_sch_canvas_set_library(DC_SchCanvas *c, DC_ELibrary *lib)
{
    if (!c) return;
    c->lib = lib;
}

void dc_sch_canvas_set_editor(DC_SchCanvas *c, DC_SchEditor *editor)
{
    if (c) c->editor = editor;
}

void dc_sch_canvas_set_zoom(DC_SchCanvas *c, double zoom)
{
    if (!c) return;
    if (zoom < SCH_ZOOM_MIN) zoom = SCH_ZOOM_MIN;
    if (zoom > SCH_ZOOM_MAX) zoom = SCH_ZOOM_MAX;
    c->zoom = zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

double dc_sch_canvas_get_zoom(const DC_SchCanvas *c)
{
    return c ? c->zoom : 1.0;
}

void dc_sch_canvas_set_pan(DC_SchCanvas *c, double x, double y)
{
    if (!c) return;
    c->pan_x = x;
    c->pan_y = y;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_sch_canvas_get_pan(const DC_SchCanvas *c, double *x, double *y)
{
    if (!c) return;
    if (x) *x = c->pan_x;
    if (y) *y = c->pan_y;
}

void dc_sch_canvas_queue_redraw(DC_SchCanvas *c)
{
    if (c && c->drawing_area) gtk_widget_queue_draw(c->drawing_area);
}

/* =========================================================================
 * Selection API
 * ========================================================================= */
DC_SchSelType dc_sch_canvas_get_sel_type(const DC_SchCanvas *c)
{
    return c ? c->sel_type : DC_SCH_SEL_NONE;
}

int dc_sch_canvas_get_sel_index(const DC_SchCanvas *c)
{
    return c ? c->sel_index : -1;
}

void dc_sch_canvas_select(DC_SchCanvas *c, DC_SchSelType type, int index)
{
    if (!c) return;
    c->sel_type = type;
    c->sel_index = index;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_sch_canvas_deselect(DC_SchCanvas *c)
{
    if (!c) return;
    c->sel_type = DC_SCH_SEL_NONE;
    c->sel_index = -1;
    gtk_widget_queue_draw(c->drawing_area);
}

/* Legacy compat */
int dc_sch_canvas_get_selected_symbol(const DC_SchCanvas *c)
{
    if (!c || c->sel_type != DC_SCH_SEL_SYMBOL) return -1;
    return c->sel_index;
}

void dc_sch_canvas_set_selected_symbol(DC_SchCanvas *c, int index)
{
    if (!c) return;
    if (index >= 0) {
        c->sel_type = DC_SCH_SEL_SYMBOL;
        c->sel_index = index;
    } else {
        c->sel_type = DC_SCH_SEL_NONE;
        c->sel_index = -1;
    }
    gtk_widget_queue_draw(c->drawing_area);
}

int
dc_sch_canvas_render_to_png(DC_SchCanvas *c, const char *path,
                             int width, int height)
{
    if (!c || !path) return -1;

    if (width <= 0) width = gtk_widget_get_width(c->drawing_area);
    if (height <= 0) height = gtk_widget_get_height(c->drawing_area);
    if (width <= 0 || height <= 0) return -1;

    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    on_draw(GTK_DRAWING_AREA(c->drawing_area), cr, width, height, c);

    cairo_status_t status = cairo_surface_write_to_png(surface, path);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return (status == CAIRO_STATUS_SUCCESS) ? 0 : -1;
}
