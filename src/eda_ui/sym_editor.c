#define _POSIX_C_SOURCE 200809L

#include "sym_editor.h"
#include "sch_symbol_render.h"
#include "eda/eda_library.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Symbol editor dialog
 *
 * +-------------------+---------------------------+
 * | Properties        | Canvas (GtkDrawingArea)   |
 * | Reference: [___]  |                           |
 * | Value: [___]      |  [Interactive symbol      |
 * | Footprint: [___]  |   preview]                |
 * | Description: [__] |                           |
 * |                   |                           |
 * | --- Add ---       |                           |
 * | [Rect][Line][Circ]|                           |
 * | [Arc] [Pin]       |                           |
 * +-------------------+---------------------------+
 * | [Save] [Save As] [Cancel]                     |
 * +-----------------------------------------------+
 * ========================================================================= */

typedef struct {
    GtkWidget  *dialog;
    GtkWidget  *canvas;        /* GtkDrawingArea */
    GtkWidget  *ref_entry;
    GtkWidget  *val_entry;
    GtkWidget  *fp_entry;
    GtkWidget  *desc_entry;
    DC_Sexpr   *sym_clone;     /* owned — cloned AST */
    const char *lib_path;      /* borrowed */
    DC_ELibrary *lib;          /* borrowed */
    int         saved;
    GMainLoop  *loop;

    /* Drag state */
    int         sel_prim;      /* index of selected primitive, -1 for none */
    double      drag_start_x, drag_start_y;
    int         dragging;
} SymEdCtx;

/* =========================================================================
 * Helpers — get/set property in cloned AST
 * ========================================================================= */
static const char *
sym_ed_get_prop(SymEdCtx *ctx, const char *key)
{
    return dc_elibrary_symbol_property(ctx->sym_clone, key);
}

static void
sym_ed_set_prop(SymEdCtx *ctx, const char *key, const char *val)
{
    if (!ctx->sym_clone || !key) return;

    size_t count = 0;
    DC_Sexpr **props = dc_sexpr_find_all(ctx->sym_clone, "property", &count);
    if (props) {
        for (size_t i = 0; i < count; i++) {
            const char *pname = dc_sexpr_value(props[i]);
            if (pname && strcmp(pname, key) == 0) {
                /* Replace the value (child index 2) */
                if (dc_sexpr_child_count(props[i]) >= 3) {
                    DC_Sexpr *new_val = dc_sexpr_new_string(val ? val : "");
                    if (new_val)
                        dc_sexpr_replace_child(props[i], 2, new_val);
                }
                free(props);
                return;
            }
        }
        free(props);
    }

    /* Property not found — add it */
    DC_Sexpr *prop = dc_sexpr_new_list();
    if (!prop) return;
    dc_sexpr_add_child(prop, dc_sexpr_new_atom("property"));
    dc_sexpr_add_child(prop, dc_sexpr_new_string(key));
    dc_sexpr_add_child(prop, dc_sexpr_new_string(val ? val : ""));

    /* Add (at 0 0 0) and (effects ...) for KiCad compat */
    DC_Sexpr *at = dc_sexpr_new_list();
    dc_sexpr_add_child(at, dc_sexpr_new_atom("at"));
    dc_sexpr_add_child(at, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(at, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(at, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(prop, at);

    dc_sexpr_add_child(ctx->sym_clone, prop);
}

/* =========================================================================
 * Canvas drawing
 * ========================================================================= */
static void
on_sym_ed_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
               gpointer userdata)
{
    (void)area;
    SymEdCtx *ctx = userdata;

    /* Dark background */
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.14);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    if (ctx->sym_clone) {
        dc_sch_symbol_render_preview(cr, ctx->sym_clone,
                                      0, 0, (double)width, (double)height);
    }

    /* Crosshair at center */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.5);
    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, width / 2.0, 0);
    cairo_line_to(cr, width / 2.0, height);
    cairo_stroke(cr);
    cairo_move_to(cr, 0, height / 2.0);
    cairo_line_to(cr, width, height / 2.0);
    cairo_stroke(cr);
}

/* =========================================================================
 * Property sync: entries → AST
 * ========================================================================= */
static void
sync_props_to_ast(SymEdCtx *ctx)
{
    sym_ed_set_prop(ctx, "Reference",
                    gtk_editable_get_text(GTK_EDITABLE(ctx->ref_entry)));
    sym_ed_set_prop(ctx, "Value",
                    gtk_editable_get_text(GTK_EDITABLE(ctx->val_entry)));
    sym_ed_set_prop(ctx, "Footprint",
                    gtk_editable_get_text(GTK_EDITABLE(ctx->fp_entry)));
    sym_ed_set_prop(ctx, "Description",
                    gtk_editable_get_text(GTK_EDITABLE(ctx->desc_entry)));
}

/* =========================================================================
 * Add primitives
 * ========================================================================= */
static DC_Sexpr *
find_draw_unit(DC_Sexpr *sym)
{
    /* Find the _0_1 sub-symbol (graphics unit) */
    for (size_t i = 0; i < dc_sexpr_child_count(sym); i++) {
        DC_Sexpr *child = sym->children[i];
        if (child->type != DC_SEXPR_LIST) continue;
        const char *tag = dc_sexpr_tag(child);
        if (!tag || strcmp(tag, "symbol") != 0) continue;
        const char *name = dc_sexpr_value(child);
        if (name && strstr(name, "_0_1"))
            return child;
    }
    /* Fallback: first sub-symbol or the symbol itself */
    for (size_t i = 0; i < dc_sexpr_child_count(sym); i++) {
        DC_Sexpr *child = sym->children[i];
        if (child->type != DC_SEXPR_LIST) continue;
        if (strcmp(dc_sexpr_tag(child), "symbol") == 0)
            return child;
    }
    return sym;
}

static void
on_add_rect(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    SymEdCtx *ctx = userdata;
    DC_Sexpr *unit = find_draw_unit(ctx->sym_clone);

    DC_Sexpr *rect = dc_sexpr_new_list();
    dc_sexpr_add_child(rect, dc_sexpr_new_atom("rectangle"));

    DC_Sexpr *start = dc_sexpr_new_list();
    dc_sexpr_add_child(start, dc_sexpr_new_atom("start"));
    dc_sexpr_add_child(start, dc_sexpr_new_atom("-1.27"));
    dc_sexpr_add_child(start, dc_sexpr_new_atom("-1.27"));
    dc_sexpr_add_child(rect, start);

    DC_Sexpr *end = dc_sexpr_new_list();
    dc_sexpr_add_child(end, dc_sexpr_new_atom("end"));
    dc_sexpr_add_child(end, dc_sexpr_new_atom("1.27"));
    dc_sexpr_add_child(end, dc_sexpr_new_atom("1.27"));
    dc_sexpr_add_child(rect, end);

    DC_Sexpr *stroke = dc_sexpr_new_list();
    dc_sexpr_add_child(stroke, dc_sexpr_new_atom("stroke"));
    DC_Sexpr *sw = dc_sexpr_new_list();
    dc_sexpr_add_child(sw, dc_sexpr_new_atom("width"));
    dc_sexpr_add_child(sw, dc_sexpr_new_atom("0.254"));
    dc_sexpr_add_child(stroke, sw);
    dc_sexpr_add_child(rect, stroke);

    DC_Sexpr *fill = dc_sexpr_new_list();
    dc_sexpr_add_child(fill, dc_sexpr_new_atom("fill"));
    DC_Sexpr *ft = dc_sexpr_new_list();
    dc_sexpr_add_child(ft, dc_sexpr_new_atom("type"));
    dc_sexpr_add_child(ft, dc_sexpr_new_atom("none"));
    dc_sexpr_add_child(fill, ft);
    dc_sexpr_add_child(rect, fill);

    dc_sexpr_add_child(unit, rect);
    gtk_widget_queue_draw(ctx->canvas);
}

static void
on_add_line(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    SymEdCtx *ctx = userdata;
    DC_Sexpr *unit = find_draw_unit(ctx->sym_clone);

    DC_Sexpr *poly = dc_sexpr_new_list();
    dc_sexpr_add_child(poly, dc_sexpr_new_atom("polyline"));

    DC_Sexpr *pts = dc_sexpr_new_list();
    dc_sexpr_add_child(pts, dc_sexpr_new_atom("pts"));

    DC_Sexpr *xy1 = dc_sexpr_new_list();
    dc_sexpr_add_child(xy1, dc_sexpr_new_atom("xy"));
    dc_sexpr_add_child(xy1, dc_sexpr_new_atom("-1.27"));
    dc_sexpr_add_child(xy1, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(pts, xy1);

    DC_Sexpr *xy2 = dc_sexpr_new_list();
    dc_sexpr_add_child(xy2, dc_sexpr_new_atom("xy"));
    dc_sexpr_add_child(xy2, dc_sexpr_new_atom("1.27"));
    dc_sexpr_add_child(xy2, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(pts, xy2);

    dc_sexpr_add_child(poly, pts);

    DC_Sexpr *stroke = dc_sexpr_new_list();
    dc_sexpr_add_child(stroke, dc_sexpr_new_atom("stroke"));
    DC_Sexpr *sw = dc_sexpr_new_list();
    dc_sexpr_add_child(sw, dc_sexpr_new_atom("width"));
    dc_sexpr_add_child(sw, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(stroke, sw);
    dc_sexpr_add_child(poly, stroke);

    DC_Sexpr *fill = dc_sexpr_new_list();
    dc_sexpr_add_child(fill, dc_sexpr_new_atom("fill"));
    DC_Sexpr *ft = dc_sexpr_new_list();
    dc_sexpr_add_child(ft, dc_sexpr_new_atom("type"));
    dc_sexpr_add_child(ft, dc_sexpr_new_atom("none"));
    dc_sexpr_add_child(fill, ft);
    dc_sexpr_add_child(poly, fill);

    dc_sexpr_add_child(unit, poly);
    gtk_widget_queue_draw(ctx->canvas);
}

static void
on_add_circle(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    SymEdCtx *ctx = userdata;
    DC_Sexpr *unit = find_draw_unit(ctx->sym_clone);

    DC_Sexpr *circ = dc_sexpr_new_list();
    dc_sexpr_add_child(circ, dc_sexpr_new_atom("circle"));

    DC_Sexpr *center = dc_sexpr_new_list();
    dc_sexpr_add_child(center, dc_sexpr_new_atom("center"));
    dc_sexpr_add_child(center, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(center, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(circ, center);

    DC_Sexpr *radius = dc_sexpr_new_list();
    dc_sexpr_add_child(radius, dc_sexpr_new_atom("radius"));
    dc_sexpr_add_child(radius, dc_sexpr_new_atom("1.27"));
    dc_sexpr_add_child(circ, radius);

    DC_Sexpr *stroke = dc_sexpr_new_list();
    dc_sexpr_add_child(stroke, dc_sexpr_new_atom("stroke"));
    DC_Sexpr *sw = dc_sexpr_new_list();
    dc_sexpr_add_child(sw, dc_sexpr_new_atom("width"));
    dc_sexpr_add_child(sw, dc_sexpr_new_atom("0.254"));
    dc_sexpr_add_child(stroke, sw);
    dc_sexpr_add_child(circ, stroke);

    DC_Sexpr *fill = dc_sexpr_new_list();
    dc_sexpr_add_child(fill, dc_sexpr_new_atom("fill"));
    DC_Sexpr *ft = dc_sexpr_new_list();
    dc_sexpr_add_child(ft, dc_sexpr_new_atom("type"));
    dc_sexpr_add_child(ft, dc_sexpr_new_atom("none"));
    dc_sexpr_add_child(fill, ft);
    dc_sexpr_add_child(circ, fill);

    dc_sexpr_add_child(unit, circ);
    gtk_widget_queue_draw(ctx->canvas);
}

static void
on_add_pin(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    SymEdCtx *ctx = userdata;

    /* Pins go in _1_1 (pin unit) — find or create */
    DC_Sexpr *pin_unit = NULL;
    for (size_t i = 0; i < dc_sexpr_child_count(ctx->sym_clone); i++) {
        DC_Sexpr *child = ctx->sym_clone->children[i];
        if (child->type != DC_SEXPR_LIST) continue;
        const char *t = dc_sexpr_tag(child);
        if (!t || strcmp(t, "symbol") != 0) continue;
        const char *n = dc_sexpr_value(child);
        if (n && strstr(n, "_1_1")) { pin_unit = child; break; }
    }
    if (!pin_unit) pin_unit = find_draw_unit(ctx->sym_clone);

    DC_Sexpr *pin = dc_sexpr_new_list();
    dc_sexpr_add_child(pin, dc_sexpr_new_atom("pin"));
    dc_sexpr_add_child(pin, dc_sexpr_new_atom("passive"));
    dc_sexpr_add_child(pin, dc_sexpr_new_atom("line"));

    DC_Sexpr *at = dc_sexpr_new_list();
    dc_sexpr_add_child(at, dc_sexpr_new_atom("at"));
    dc_sexpr_add_child(at, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(at, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(at, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(pin, at);

    DC_Sexpr *len = dc_sexpr_new_list();
    dc_sexpr_add_child(len, dc_sexpr_new_atom("length"));
    dc_sexpr_add_child(len, dc_sexpr_new_atom("2.54"));
    dc_sexpr_add_child(pin, len);

    /* Pin name and number */
    DC_Sexpr *pname = dc_sexpr_new_list();
    dc_sexpr_add_child(pname, dc_sexpr_new_atom("name"));
    dc_sexpr_add_child(pname, dc_sexpr_new_string("~"));
    dc_sexpr_add_child(pin, pname);

    DC_Sexpr *pnum = dc_sexpr_new_list();
    dc_sexpr_add_child(pnum, dc_sexpr_new_atom("number"));
    dc_sexpr_add_child(pnum, dc_sexpr_new_string("0"));
    dc_sexpr_add_child(pin, pnum);

    dc_sexpr_add_child(pin_unit, pin);
    gtk_widget_queue_draw(ctx->canvas);
}

/* =========================================================================
 * Save
 * ========================================================================= */
static int
save_symbol(SymEdCtx *ctx)
{
    if (!ctx->lib_path || !ctx->sym_clone) return -1;

    sync_props_to_ast(ctx);

    /* Read the entire library file, find the symbol, replace it */
    FILE *f = fopen(ctx->lib_path, "r");
    if (!f) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_EDA, "Cannot open %s for reading", ctx->lib_path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (!text) { fclose(f); return -1; }
    size_t rd = fread(text, 1, (size_t)size, f);
    text[rd] = '\0';
    fclose(f);

    DC_Error err = {0};
    DC_Sexpr *lib_ast = dc_sexpr_parse(text, &err);
    free(text);
    if (!lib_ast) return -1;

    /* Find the symbol by name and replace */
    const char *sym_name = dc_sexpr_value(ctx->sym_clone);
    int replaced = 0;
    for (size_t i = 0; i < dc_sexpr_child_count(lib_ast); i++) {
        DC_Sexpr *child = lib_ast->children[i];
        if (child->type != DC_SEXPR_LIST) continue;
        const char *tag = dc_sexpr_tag(child);
        if (!tag || strcmp(tag, "symbol") != 0) continue;
        const char *name = dc_sexpr_value(child);
        if (name && sym_name && strcmp(name, sym_name) == 0) {
            DC_Sexpr *clone = dc_sexpr_clone(ctx->sym_clone);
            if (clone) {
                dc_sexpr_replace_child(lib_ast, i, clone);
                replaced = 1;
            }
            break;
        }
    }

    if (!replaced) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_EDA,
               "Symbol '%s' not found in library file", sym_name ? sym_name : "?");
        dc_sexpr_free(lib_ast);
        return -1;
    }

    /* Write back */
    char *output = dc_sexpr_write_pretty(lib_ast, &err);
    dc_sexpr_free(lib_ast);
    if (!output) return -1;

    f = fopen(ctx->lib_path, "w");
    if (!f) { free(output); return -1; }
    fputs(output, f);
    fclose(f);
    free(output);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Symbol '%s' saved to %s",
           sym_name ? sym_name : "?", ctx->lib_path);
    return 0;
}

/* =========================================================================
 * Dialog callbacks
 * ========================================================================= */
static void
on_save_clicked(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    SymEdCtx *ctx = userdata;
    if (save_symbol(ctx) == 0) {
        ctx->saved = 1;
        g_main_loop_quit(ctx->loop);
    }
}

static void
on_sym_cancel_clicked(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    SymEdCtx *ctx = userdata;
    ctx->saved = 0;
    g_main_loop_quit(ctx->loop);
}

static gboolean
on_sym_close_request(GtkWindow *win, gpointer userdata)
{
    (void)win;
    SymEdCtx *ctx = userdata;
    ctx->saved = 0;
    g_main_loop_quit(ctx->loop);
    return TRUE;
}

static void
on_prop_changed(GtkEditable *editable, gpointer userdata)
{
    (void)editable;
    SymEdCtx *ctx = userdata;
    sync_props_to_ast(ctx);
    gtk_widget_queue_draw(ctx->canvas);
}

/* =========================================================================
 * Public API
 * ========================================================================= */
int
dc_sym_editor_run(GtkWindow *parent, const DC_Sexpr *sym_def,
                   const char *lib_path, DC_ELibrary *lib)
{
    if (!sym_def) return 0;

    SymEdCtx ctx = {0};
    ctx.sym_clone = dc_sexpr_clone(sym_def);
    if (!ctx.sym_clone) return 0;
    ctx.lib_path = lib_path;
    ctx.lib = lib;
    ctx.sel_prim = -1;
    ctx.loop = g_main_loop_new(NULL, FALSE);

    ctx.dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(ctx.dialog), "Symbol Editor");
    gtk_window_set_default_size(GTK_WINDOW(ctx.dialog), 800, 550);
    gtk_window_set_modal(GTK_WINDOW(ctx.dialog), TRUE);
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(ctx.dialog), parent);

    g_signal_connect(ctx.dialog, "close-request",
                     G_CALLBACK(on_sym_close_request), &ctx);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_vexpand(hbox, TRUE);

    /* Left: properties panel */
    GtkWidget *prop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(prop_box, 220, -1);

    GtkWidget *prop_frame = gtk_frame_new("Properties");
    GtkWidget *prop_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(prop_grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(prop_grid), 4);
    gtk_widget_set_margin_start(prop_grid, 6);
    gtk_widget_set_margin_end(prop_grid, 6);
    gtk_widget_set_margin_top(prop_grid, 4);
    gtk_widget_set_margin_bottom(prop_grid, 4);

    /* Reference */
    gtk_grid_attach(GTK_GRID(prop_grid), gtk_label_new("Reference:"), 0, 0, 1, 1);
    ctx.ref_entry = gtk_entry_new();
    const char *ref = sym_ed_get_prop(&ctx, "Reference");
    if (ref) gtk_editable_set_text(GTK_EDITABLE(ctx.ref_entry), ref);
    g_signal_connect(ctx.ref_entry, "changed", G_CALLBACK(on_prop_changed), &ctx);
    gtk_grid_attach(GTK_GRID(prop_grid), ctx.ref_entry, 1, 0, 1, 1);

    /* Value */
    gtk_grid_attach(GTK_GRID(prop_grid), gtk_label_new("Value:"), 0, 1, 1, 1);
    ctx.val_entry = gtk_entry_new();
    const char *val = sym_ed_get_prop(&ctx, "Value");
    if (val) gtk_editable_set_text(GTK_EDITABLE(ctx.val_entry), val);
    g_signal_connect(ctx.val_entry, "changed", G_CALLBACK(on_prop_changed), &ctx);
    gtk_grid_attach(GTK_GRID(prop_grid), ctx.val_entry, 1, 1, 1, 1);

    /* Footprint */
    gtk_grid_attach(GTK_GRID(prop_grid), gtk_label_new("Footprint:"), 0, 2, 1, 1);
    ctx.fp_entry = gtk_entry_new();
    const char *fp = sym_ed_get_prop(&ctx, "Footprint");
    if (fp) gtk_editable_set_text(GTK_EDITABLE(ctx.fp_entry), fp);
    g_signal_connect(ctx.fp_entry, "changed", G_CALLBACK(on_prop_changed), &ctx);
    gtk_grid_attach(GTK_GRID(prop_grid), ctx.fp_entry, 1, 2, 1, 1);

    /* Description */
    gtk_grid_attach(GTK_GRID(prop_grid), gtk_label_new("Desc:"), 0, 3, 1, 1);
    ctx.desc_entry = gtk_entry_new();
    const char *desc = sym_ed_get_prop(&ctx, "Description");
    if (desc) gtk_editable_set_text(GTK_EDITABLE(ctx.desc_entry), desc);
    g_signal_connect(ctx.desc_entry, "changed", G_CALLBACK(on_prop_changed), &ctx);
    gtk_grid_attach(GTK_GRID(prop_grid), ctx.desc_entry, 1, 3, 1, 1);

    gtk_frame_set_child(GTK_FRAME(prop_frame), prop_grid);
    gtk_box_append(GTK_BOX(prop_box), prop_frame);

    /* Add primitive buttons */
    GtkWidget *add_frame = gtk_frame_new("Add Primitive");
    GtkWidget *add_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(add_grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(add_grid), 2);
    gtk_widget_set_margin_start(add_grid, 4);
    gtk_widget_set_margin_end(add_grid, 4);
    gtk_widget_set_margin_top(add_grid, 4);
    gtk_widget_set_margin_bottom(add_grid, 4);

    GtkWidget *btn_rect = gtk_button_new_with_label("Rect");
    GtkWidget *btn_line = gtk_button_new_with_label("Line");
    GtkWidget *btn_circ = gtk_button_new_with_label("Circle");
    GtkWidget *btn_pin  = gtk_button_new_with_label("Pin");

    g_signal_connect(btn_rect, "clicked", G_CALLBACK(on_add_rect), &ctx);
    g_signal_connect(btn_line, "clicked", G_CALLBACK(on_add_line), &ctx);
    g_signal_connect(btn_circ, "clicked", G_CALLBACK(on_add_circle), &ctx);
    g_signal_connect(btn_pin,  "clicked", G_CALLBACK(on_add_pin), &ctx);

    gtk_grid_attach(GTK_GRID(add_grid), btn_rect, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(add_grid), btn_line, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(add_grid), btn_circ, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(add_grid), btn_pin,  1, 1, 1, 1);

    gtk_frame_set_child(GTK_FRAME(add_frame), add_grid);
    gtk_box_append(GTK_BOX(prop_box), add_frame);

    gtk_box_append(GTK_BOX(hbox), prop_box);

    /* Right: canvas */
    GtkWidget *canvas_frame = gtk_frame_new("Symbol");
    ctx.canvas = gtk_drawing_area_new();
    gtk_widget_set_hexpand(ctx.canvas, TRUE);
    gtk_widget_set_vexpand(ctx.canvas, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ctx.canvas),
                                    on_sym_ed_draw, &ctx, NULL);
    gtk_frame_set_child(GTK_FRAME(canvas_frame), ctx.canvas);
    gtk_box_append(GTK_BOX(hbox), canvas_frame);

    gtk_box_append(GTK_BOX(vbox), hbox);

    /* Button bar */
    GtkWidget *btn_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_bar, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btn_bar, 4);

    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *btn_save = gtk_button_new_with_label("Save");
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_sym_cancel_clicked), &ctx);
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_clicked), &ctx);
    gtk_box_append(GTK_BOX(btn_bar), btn_cancel);
    gtk_box_append(GTK_BOX(btn_bar), btn_save);
    gtk_box_append(GTK_BOX(vbox), btn_bar);

    gtk_window_set_child(GTK_WINDOW(ctx.dialog), vbox);

    gtk_window_present(GTK_WINDOW(ctx.dialog));
    g_main_loop_run(ctx.loop);
    g_main_loop_unref(ctx.loop);

    gtk_window_destroy(GTK_WINDOW(ctx.dialog));
    dc_sexpr_free(ctx.sym_clone);

    return ctx.saved;
}
