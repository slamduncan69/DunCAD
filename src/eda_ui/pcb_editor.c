#define _POSIX_C_SOURCE 200809L

#include "pcb_editor.h"
#include "pcb_canvas.h"
#include "pcb_layer_panel.h"
#include "eda/eda_pcb.h"
#include "eda/eda_ratsnest.h"
#include "eda/eda_library.h"
#include "core/error.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal state
 * ========================================================================= */
struct DC_PcbEditor {
    GtkWidget       *box;          /* top-level GtkBox (V) */
    GtkWidget       *hbox;         /* horizontal: canvas + right panel */
    GtkWidget       *toolbar;      /* top horizontal toolbar (status) */
    DC_PcbCanvas    *canvas;
    DC_PcbLayerPanel *layer_panel;
    DC_EPcb         *pcb;          /* owned */
    DC_ELibrary     *lib;          /* borrowed */
    DC_Ratsnest     *ratsnest;     /* owned */
    DC_PcbEditMode   mode;
    char            *current_path; /* owned, NULL if untitled */
    DC_PcbPlaceCallback place_cb;
    void               *place_cb_data;
};

/* =========================================================================
 * Layer panel callback
 * ========================================================================= */
static void
on_layer_changed(int layer_id, int visible, void *userdata)
{
    DC_PcbEditor *ed = userdata;
    dc_pcb_canvas_set_layer_visible(ed->canvas, layer_id, visible);
}

static void
on_active_layer_changed(int layer_id, void *userdata)
{
    DC_PcbEditor *ed = userdata;
    dc_pcb_canvas_set_active_layer(ed->canvas, layer_id);
}

/* =========================================================================
 * Toolbar callbacks
 * ========================================================================= */
static void on_mode_select(GtkButton *b, gpointer d)
    { (void)b; ((DC_PcbEditor*)d)->mode = DC_PCB_MODE_SELECT; }
static void on_mode_route(GtkButton *b, gpointer d)
    { (void)b; ((DC_PcbEditor*)d)->mode = DC_PCB_MODE_ROUTE; }
static void on_mode_via(GtkButton *b, gpointer d)
    { (void)b; ((DC_PcbEditor*)d)->mode = DC_PCB_MODE_PLACE_VIA; }
static void on_mode_footprint(GtkButton *b, gpointer d)
{
    (void)b;
    DC_PcbEditor *ed = d;
    ed->mode = DC_PCB_MODE_PLACE_FOOTPRINT;
    if (ed->place_cb)
        ed->place_cb(DC_PCB_MODE_PLACE_FOOTPRINT, ed->place_cb_data);
}
static void on_mode_zone(GtkButton *b, gpointer d)
    { (void)b; ((DC_PcbEditor*)d)->mode = DC_PCB_MODE_ZONE; }
static void on_mode_measure(GtkButton *b, gpointer d)
    { (void)b; ((DC_PcbEditor*)d)->mode = DC_PCB_MODE_MEASURE; }

/* =========================================================================
 * Helper: add a tool button to a vertical toolbar
 * ========================================================================= */
static GtkWidget *
add_tool_btn(GtkWidget *box, const char *label, GCallback cb, gpointer data)
{
    GtkWidget *btn = gtk_button_new_with_label(label);
    gtk_widget_set_size_request(btn, 36, 36);
    gtk_widget_set_tooltip_text(btn, label);
    /* Make button compact */
    gtk_widget_set_margin_start(btn, 1);
    gtk_widget_set_margin_end(btn, 1);
    gtk_widget_set_margin_top(btn, 1);
    gtk_widget_set_margin_bottom(btn, 1);
    g_signal_connect(btn, "clicked", cb, data);
    gtk_box_append(GTK_BOX(box), btn);
    return btn;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
DC_PcbEditor *dc_pcb_editor_new(void)
{
    DC_PcbEditor *ed = calloc(1, sizeof(*ed));
    if (!ed) return NULL;

    ed->pcb = dc_epcb_new();
    if (!ed->pcb) { free(ed); return NULL; }

    ed->canvas = dc_pcb_canvas_new();
    if (!ed->canvas) { dc_epcb_free(ed->pcb); free(ed); return NULL; }

    dc_pcb_canvas_set_pcb(ed->canvas, ed->pcb);
    dc_pcb_canvas_set_editor(ed->canvas, ed);

    ed->layer_panel = dc_pcb_layer_panel_new();

    /* Build UI */
    ed->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Top toolbar — just a status label */
    ed->toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(ed->toolbar, 4);
    gtk_widget_set_margin_end(ed->toolbar, 4);
    gtk_widget_set_margin_top(ed->toolbar, 2);
    gtk_widget_set_margin_bottom(ed->toolbar, 2);

    GtkWidget *title = gtk_label_new("PCB Editor");
    gtk_widget_set_hexpand(title, TRUE);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_append(GTK_BOX(ed->toolbar), title);

    gtk_box_append(GTK_BOX(ed->box), ed->toolbar);
    gtk_box_append(GTK_BOX(ed->box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Horizontal layout: canvas (center) + right panel */
    ed->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(ed->hbox, TRUE);

    /* Canvas (center, expandable) */
    GtkWidget *canvas_w = dc_pcb_canvas_widget(ed->canvas);
    gtk_widget_set_hexpand(canvas_w, TRUE);
    gtk_box_append(GTK_BOX(ed->hbox), canvas_w);

    /* Right-side vertical tool toolbar (KiCad-style) */
    gtk_box_append(GTK_BOX(ed->hbox),
                   gtk_separator_new(GTK_ORIENTATION_VERTICAL));

    GtkWidget *tool_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(tool_bar, 2);
    gtk_widget_set_margin_bottom(tool_bar, 2);
    gtk_widget_set_size_request(tool_bar, 40, -1);

    /* Tool buttons — short labels like KiCad's right toolbar */
    add_tool_btn(tool_bar, "Sel",  G_CALLBACK(on_mode_select), ed);
    add_tool_btn(tool_bar, "Rte",  G_CALLBACK(on_mode_route), ed);
    add_tool_btn(tool_bar, "Via",  G_CALLBACK(on_mode_via), ed);
    add_tool_btn(tool_bar, "FP",   G_CALLBACK(on_mode_footprint), ed);
    add_tool_btn(tool_bar, "Zone", G_CALLBACK(on_mode_zone), ed);
    add_tool_btn(tool_bar, "Msr",  G_CALLBACK(on_mode_measure), ed);

    /* Spacer to push layers down */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(tool_bar), spacer);

    gtk_box_append(GTK_BOX(ed->hbox), tool_bar);

    /* Layer panel (right side, after toolbar) */
    gtk_box_append(GTK_BOX(ed->hbox),
                   gtk_separator_new(GTK_ORIENTATION_VERTICAL));

    if (ed->layer_panel) {
        GtkWidget *lp_w = dc_pcb_layer_panel_widget(ed->layer_panel);
        gtk_widget_set_size_request(lp_w, 140, -1);
        gtk_box_append(GTK_BOX(ed->hbox), lp_w);

        dc_pcb_layer_panel_set_visibility_callback(ed->layer_panel,
                                                     on_layer_changed, ed);
        dc_pcb_layer_panel_set_active_callback(ed->layer_panel,
                                                 on_active_layer_changed, ed);
    }

    gtk_box_append(GTK_BOX(ed->box), ed->hbox);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "PCB editor created");
    return ed;
}

void dc_pcb_editor_free(DC_PcbEditor *ed)
{
    if (!ed) return;
    dc_pcb_canvas_free(ed->canvas);
    dc_pcb_layer_panel_free(ed->layer_panel);
    dc_epcb_free(ed->pcb);
    dc_ratsnest_free(ed->ratsnest);
    free(ed->current_path);
    free(ed);
}

GtkWidget *dc_pcb_editor_widget(DC_PcbEditor *ed) { return ed ? ed->box : NULL; }
DC_EPcb *dc_pcb_editor_get_pcb(DC_PcbEditor *ed) { return ed ? ed->pcb : NULL; }
DC_PcbCanvas *dc_pcb_editor_get_canvas(DC_PcbEditor *ed) { return ed ? ed->canvas : NULL; }

void dc_pcb_editor_set_library(DC_PcbEditor *ed, DC_ELibrary *lib)
{
    if (!ed) return;
    ed->lib = lib;
    dc_pcb_canvas_set_library(ed->canvas, lib);
}

int dc_pcb_editor_load(DC_PcbEditor *ed, const char *path)
{
    if (!ed || !path) return -1;
    DC_Error err = {0};
    DC_EPcb *pcb = dc_epcb_load(path, &err);
    if (!pcb) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_EDA, "Failed to load PCB: %s", err.message);
        return -1;
    }
    dc_epcb_free(ed->pcb);
    ed->pcb = pcb;
    dc_pcb_canvas_set_pcb(ed->canvas, ed->pcb);
    free(ed->current_path);
    ed->current_path = strdup(path);
    dc_pcb_editor_update_ratsnest(ed);
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Loaded PCB: %s", path);
    return 0;
}

int dc_pcb_editor_save(DC_PcbEditor *ed, const char *path)
{
    if (!ed) return -1;
    const char *sp = path ? path : ed->current_path;
    if (!sp) return -1;
    DC_Error err = {0};
    int rc = dc_epcb_save(ed->pcb, sp, &err);
    if (rc != 0) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_EDA, "Failed to save PCB: %s", err.message);
        return -1;
    }
    if (path && (!ed->current_path || strcmp(path, ed->current_path) != 0)) {
        free(ed->current_path);
        ed->current_path = strdup(path);
    }
    return 0;
}

void dc_pcb_editor_set_mode(DC_PcbEditor *ed, DC_PcbEditMode m) { if (ed) ed->mode = m; }
DC_PcbEditMode dc_pcb_editor_get_mode(const DC_PcbEditor *ed) { return ed ? ed->mode : DC_PCB_MODE_SELECT; }

void dc_pcb_editor_update_ratsnest(DC_PcbEditor *ed)
{
    if (!ed) return;
    dc_ratsnest_free(ed->ratsnest);
    ed->ratsnest = dc_ratsnest_compute(ed->pcb);
    dc_pcb_canvas_set_ratsnest(ed->canvas, ed->ratsnest);
}

void dc_pcb_editor_set_place_callback(DC_PcbEditor *ed,
                                        DC_PcbPlaceCallback cb, void *userdata)
{
    if (!ed) return;
    ed->place_cb = cb;
    ed->place_cb_data = userdata;
}
