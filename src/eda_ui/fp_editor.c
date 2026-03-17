#define _POSIX_C_SOURCE 200809L

#include "fp_editor.h"
#include "pcb_footprint_render.h"
#include "eda/eda_library.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Footprint editor dialog
 *
 * +-------------------+---------------------------+
 * | Pads              | Canvas (GtkDrawingArea)   |
 * | [ListView]        |                           |
 * |                   |  [Footprint preview with  |
 * | Pad Properties:   |   layer-colored pads]     |
 * | Number: [___]     |                           |
 * | Type: [combo]     |                           |
 * | Size X: [___]     |                           |
 * | Size Y: [___]     |                           |
 * | [Add Pad]         |                           |
 * +-------------------+---------------------------+
 * | [Save] [Cancel]                               |
 * +-----------------------------------------------+
 * ========================================================================= */

typedef struct {
    GtkWidget  *dialog;
    GtkWidget  *canvas;
    GtkWidget  *pad_list;
    GtkWidget  *num_entry;
    GtkWidget  *size_x_entry;
    GtkWidget  *size_y_entry;
    DC_Sexpr   *fp_clone;      /* owned — cloned AST */
    const char *fp_path;       /* borrowed */
    int         saved;
    GMainLoop  *loop;
    int         sel_pad;       /* index of selected pad in pad list, -1 for none */
} FPEdCtx;

/* =========================================================================
 * Populate pad list
 * ========================================================================= */
static void
populate_pad_list(FPEdCtx *ctx)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ctx->pad_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(ctx->pad_list), child);

    size_t count = 0;
    DC_Sexpr **pads = dc_sexpr_find_all(ctx->fp_clone, "pad", &count);
    if (pads) {
        for (size_t i = 0; i < count; i++) {
            const char *num = dc_sexpr_value(pads[i]);
            const char *type = dc_sexpr_value_at(pads[i], 1);
            char desc[128];
            snprintf(desc, sizeof(desc), "Pad %s (%s)",
                     num ? num : "?", type ? type : "?");
            GtkWidget *label = gtk_label_new(desc);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_widget_set_margin_start(label, 4);
            gtk_widget_set_margin_top(label, 2);
            gtk_widget_set_margin_bottom(label, 2);
            gtk_list_box_append(GTK_LIST_BOX(ctx->pad_list), label);
        }
        free(pads);
    }
}

/* =========================================================================
 * Canvas drawing
 * ========================================================================= */
static void
on_fp_ed_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
              gpointer userdata)
{
    (void)area;
    FPEdCtx *ctx = userdata;

    /* Dark PCB-green background */
    cairo_set_source_rgb(cr, 0.1, 0.15, 0.1);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    if (ctx->fp_clone) {
        dc_pcb_footprint_render_preview(cr, ctx->fp_clone,
                                         0, 0, (double)width, (double)height);
    }

    /* Crosshair */
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
 * Pad selection and editing
 * ========================================================================= */
static DC_Sexpr *
get_pad_at(FPEdCtx *ctx, int index)
{
    if (index < 0) return NULL;
    size_t count = 0;
    DC_Sexpr **pads = dc_sexpr_find_all(ctx->fp_clone, "pad", &count);
    if (!pads) return NULL;
    DC_Sexpr *pad = ((size_t)index < count) ? pads[index] : NULL;
    free(pads);
    return pad;
}

static void
on_pad_selected(GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
    (void)box;
    FPEdCtx *ctx = userdata;
    if (!row) { ctx->sel_pad = -1; return; }

    ctx->sel_pad = gtk_list_box_row_get_index(row);
    DC_Sexpr *pad = get_pad_at(ctx, ctx->sel_pad);
    if (!pad) return;

    /* Update property entries */
    const char *num = dc_sexpr_value(pad);
    if (num)
        gtk_editable_set_text(GTK_EDITABLE(ctx->num_entry), num);

    DC_Sexpr *size = dc_sexpr_find(pad, "size");
    if (size) {
        const char *sx = dc_sexpr_value_at(size, 0);
        const char *sy = dc_sexpr_value_at(size, 1);
        if (sx) gtk_editable_set_text(GTK_EDITABLE(ctx->size_x_entry), sx);
        if (sy) gtk_editable_set_text(GTK_EDITABLE(ctx->size_y_entry), sy);
    }
}

static void
on_pad_prop_changed(GtkEditable *editable, gpointer userdata)
{
    (void)editable;
    FPEdCtx *ctx = userdata;
    if (ctx->sel_pad < 0) return;

    DC_Sexpr *pad = get_pad_at(ctx, ctx->sel_pad);
    if (!pad) return;

    /* Update pad number */
    const char *num = gtk_editable_get_text(GTK_EDITABLE(ctx->num_entry));
    if (num && dc_sexpr_child_count(pad) >= 2) {
        dc_sexpr_set_value(pad->children[1], num);
    }

    /* Update pad size */
    DC_Sexpr *size = dc_sexpr_find(pad, "size");
    if (size && dc_sexpr_child_count(size) >= 3) {
        const char *sx = gtk_editable_get_text(GTK_EDITABLE(ctx->size_x_entry));
        const char *sy = gtk_editable_get_text(GTK_EDITABLE(ctx->size_y_entry));
        if (sx) dc_sexpr_set_value(size->children[1], sx);
        if (sy) dc_sexpr_set_value(size->children[2], sy);
    }

    gtk_widget_queue_draw(ctx->canvas);
}

/* =========================================================================
 * Add pad
 * ========================================================================= */
static void
on_add_pad(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    FPEdCtx *ctx = userdata;

    /* Count existing pads for auto-numbering */
    size_t count = 0;
    DC_Sexpr **pads = dc_sexpr_find_all(ctx->fp_clone, "pad", &count);
    free(pads);

    char num_str[16];
    snprintf(num_str, sizeof(num_str), "%zu", count + 1);

    DC_Sexpr *pad = dc_sexpr_new_list();
    dc_sexpr_add_child(pad, dc_sexpr_new_atom("pad"));
    dc_sexpr_add_child(pad, dc_sexpr_new_string(num_str));
    dc_sexpr_add_child(pad, dc_sexpr_new_atom("smd"));
    dc_sexpr_add_child(pad, dc_sexpr_new_atom("roundrect"));

    DC_Sexpr *at = dc_sexpr_new_list();
    dc_sexpr_add_child(at, dc_sexpr_new_atom("at"));
    dc_sexpr_add_child(at, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(at, dc_sexpr_new_atom("0"));
    dc_sexpr_add_child(pad, at);

    DC_Sexpr *size = dc_sexpr_new_list();
    dc_sexpr_add_child(size, dc_sexpr_new_atom("size"));
    dc_sexpr_add_child(size, dc_sexpr_new_atom("1.0"));
    dc_sexpr_add_child(size, dc_sexpr_new_atom("1.0"));
    dc_sexpr_add_child(pad, size);

    DC_Sexpr *layers = dc_sexpr_new_list();
    dc_sexpr_add_child(layers, dc_sexpr_new_atom("layers"));
    dc_sexpr_add_child(layers, dc_sexpr_new_string("F.Cu"));
    dc_sexpr_add_child(layers, dc_sexpr_new_string("F.Paste"));
    dc_sexpr_add_child(layers, dc_sexpr_new_string("F.Mask"));
    dc_sexpr_add_child(pad, layers);

    DC_Sexpr *rratio = dc_sexpr_new_list();
    dc_sexpr_add_child(rratio, dc_sexpr_new_atom("roundrect_rratio"));
    dc_sexpr_add_child(rratio, dc_sexpr_new_atom("0.25"));
    dc_sexpr_add_child(pad, rratio);

    dc_sexpr_add_child(ctx->fp_clone, pad);

    populate_pad_list(ctx);
    gtk_widget_queue_draw(ctx->canvas);
}

/* =========================================================================
 * Save
 * ========================================================================= */
static int
save_footprint(FPEdCtx *ctx)
{
    if (!ctx->fp_path || !ctx->fp_clone) return -1;

    DC_Error err = {0};
    char *output = dc_sexpr_write_pretty(ctx->fp_clone, &err);
    if (!output) return -1;

    FILE *f = fopen(ctx->fp_path, "w");
    if (!f) { free(output); return -1; }
    fputs(output, f);
    fclose(f);
    free(output);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Footprint saved to %s", ctx->fp_path);
    return 0;
}

static void
on_fp_save_clicked(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    FPEdCtx *ctx = userdata;
    if (save_footprint(ctx) == 0) {
        ctx->saved = 1;
        g_main_loop_quit(ctx->loop);
    }
}

static void
on_fp_ed_cancel_clicked(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    FPEdCtx *ctx = userdata;
    ctx->saved = 0;
    g_main_loop_quit(ctx->loop);
}

static gboolean
on_fp_ed_close_request(GtkWindow *win, gpointer userdata)
{
    (void)win;
    FPEdCtx *ctx = userdata;
    ctx->saved = 0;
    g_main_loop_quit(ctx->loop);
    return TRUE;
}

/* =========================================================================
 * Public API
 * ========================================================================= */
int
dc_fp_editor_run(GtkWindow *parent, const DC_Sexpr *fp_def,
                  const char *fp_path)
{
    if (!fp_def) return 0;

    FPEdCtx ctx = {0};
    ctx.fp_clone = dc_sexpr_clone(fp_def);
    if (!ctx.fp_clone) return 0;
    ctx.fp_path = fp_path;
    ctx.sel_pad = -1;
    ctx.loop = g_main_loop_new(NULL, FALSE);

    ctx.dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(ctx.dialog), "Footprint Editor");
    gtk_window_set_default_size(GTK_WINDOW(ctx.dialog), 750, 500);
    gtk_window_set_modal(GTK_WINDOW(ctx.dialog), TRUE);
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(ctx.dialog), parent);

    g_signal_connect(ctx.dialog, "close-request",
                     G_CALLBACK(on_fp_ed_close_request), &ctx);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_vexpand(hbox, TRUE);

    /* Left: pad list + properties */
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(left_box, 220, -1);

    GtkWidget *pad_frame = gtk_frame_new("Pads");
    GtkWidget *pad_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pad_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(pad_scroll, TRUE);
    ctx.pad_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx.pad_list), GTK_SELECTION_SINGLE);
    g_signal_connect(ctx.pad_list, "row-selected", G_CALLBACK(on_pad_selected), &ctx);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(pad_scroll), ctx.pad_list);
    gtk_frame_set_child(GTK_FRAME(pad_frame), pad_scroll);
    gtk_box_append(GTK_BOX(left_box), pad_frame);

    /* Pad properties */
    GtkWidget *pprop_frame = gtk_frame_new("Pad Properties");
    GtkWidget *pprop_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(pprop_grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(pprop_grid), 4);
    gtk_widget_set_margin_start(pprop_grid, 6);
    gtk_widget_set_margin_end(pprop_grid, 6);
    gtk_widget_set_margin_top(pprop_grid, 4);
    gtk_widget_set_margin_bottom(pprop_grid, 4);

    gtk_grid_attach(GTK_GRID(pprop_grid), gtk_label_new("Number:"), 0, 0, 1, 1);
    ctx.num_entry = gtk_entry_new();
    g_signal_connect(ctx.num_entry, "changed", G_CALLBACK(on_pad_prop_changed), &ctx);
    gtk_grid_attach(GTK_GRID(pprop_grid), ctx.num_entry, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(pprop_grid), gtk_label_new("Size X:"), 0, 1, 1, 1);
    ctx.size_x_entry = gtk_entry_new();
    g_signal_connect(ctx.size_x_entry, "changed", G_CALLBACK(on_pad_prop_changed), &ctx);
    gtk_grid_attach(GTK_GRID(pprop_grid), ctx.size_x_entry, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(pprop_grid), gtk_label_new("Size Y:"), 0, 2, 1, 1);
    ctx.size_y_entry = gtk_entry_new();
    g_signal_connect(ctx.size_y_entry, "changed", G_CALLBACK(on_pad_prop_changed), &ctx);
    gtk_grid_attach(GTK_GRID(pprop_grid), ctx.size_y_entry, 1, 2, 1, 1);

    gtk_frame_set_child(GTK_FRAME(pprop_frame), pprop_grid);
    gtk_box_append(GTK_BOX(left_box), pprop_frame);

    /* Add pad button */
    GtkWidget *btn_add_pad = gtk_button_new_with_label("Add Pad");
    g_signal_connect(btn_add_pad, "clicked", G_CALLBACK(on_add_pad), &ctx);
    gtk_box_append(GTK_BOX(left_box), btn_add_pad);

    gtk_box_append(GTK_BOX(hbox), left_box);

    /* Right: canvas */
    GtkWidget *canvas_frame = gtk_frame_new("Footprint");
    ctx.canvas = gtk_drawing_area_new();
    gtk_widget_set_hexpand(ctx.canvas, TRUE);
    gtk_widget_set_vexpand(ctx.canvas, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ctx.canvas),
                                    on_fp_ed_draw, &ctx, NULL);
    gtk_frame_set_child(GTK_FRAME(canvas_frame), ctx.canvas);
    gtk_box_append(GTK_BOX(hbox), canvas_frame);

    gtk_box_append(GTK_BOX(vbox), hbox);

    /* Button bar */
    GtkWidget *btn_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_bar, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btn_bar, 4);

    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *btn_save = gtk_button_new_with_label("Save");
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_fp_ed_cancel_clicked), &ctx);
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_fp_save_clicked), &ctx);
    gtk_box_append(GTK_BOX(btn_bar), btn_cancel);
    gtk_box_append(GTK_BOX(btn_bar), btn_save);
    gtk_box_append(GTK_BOX(vbox), btn_bar);

    gtk_window_set_child(GTK_WINDOW(ctx.dialog), vbox);

    populate_pad_list(&ctx);

    gtk_window_present(GTK_WINDOW(ctx.dialog));
    g_main_loop_run(ctx.loop);
    g_main_loop_unref(ctx.loop);

    gtk_window_destroy(GTK_WINDOW(ctx.dialog));
    dc_sexpr_free(ctx.fp_clone);

    return ctx.saved;
}
