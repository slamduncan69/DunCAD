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
    GtkWidget       *hbox;         /* horizontal: layer panel + canvas */
    GtkWidget       *toolbar;
    DC_PcbCanvas    *canvas;
    DC_PcbLayerPanel *layer_panel;
    DC_EPcb         *pcb;          /* owned */
    DC_ELibrary     *lib;          /* borrowed */
    DC_Ratsnest     *ratsnest;     /* owned */
    DC_PcbEditMode   mode;
    char            *current_path; /* owned, NULL if untitled */
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

    ed->layer_panel = dc_pcb_layer_panel_new();

    /* Build UI */
    ed->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Toolbar */
    ed->toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(ed->toolbar, 4);
    gtk_widget_set_margin_end(ed->toolbar, 4);
    gtk_widget_set_margin_top(ed->toolbar, 2);
    gtk_widget_set_margin_bottom(ed->toolbar, 2);

    GtkWidget *btn_sel = gtk_button_new_with_label("Select");
    GtkWidget *btn_route = gtk_button_new_with_label("Route");
    GtkWidget *btn_via = gtk_button_new_with_label("Via");

    g_signal_connect(btn_sel, "clicked", G_CALLBACK(on_mode_select), ed);
    g_signal_connect(btn_route, "clicked", G_CALLBACK(on_mode_route), ed);
    g_signal_connect(btn_via, "clicked", G_CALLBACK(on_mode_via), ed);

    gtk_box_append(GTK_BOX(ed->toolbar), btn_sel);
    gtk_box_append(GTK_BOX(ed->toolbar), btn_route);
    gtk_box_append(GTK_BOX(ed->toolbar), btn_via);

    GtkWidget *label = gtk_label_new("PCB Editor");
    gtk_widget_set_hexpand(label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 1.0);
    gtk_box_append(GTK_BOX(ed->toolbar), label);

    gtk_box_append(GTK_BOX(ed->box), ed->toolbar);
    gtk_box_append(GTK_BOX(ed->box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Horizontal: layer panel (left) + canvas (right) */
    ed->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(ed->hbox, TRUE);

    if (ed->layer_panel) {
        GtkWidget *lp_w = dc_pcb_layer_panel_widget(ed->layer_panel);
        gtk_widget_set_size_request(lp_w, 140, -1);
        gtk_box_append(GTK_BOX(ed->hbox), lp_w);
        gtk_box_append(GTK_BOX(ed->hbox),
                       gtk_separator_new(GTK_ORIENTATION_VERTICAL));

        dc_pcb_layer_panel_set_visibility_callback(ed->layer_panel,
                                                     on_layer_changed, ed);
        dc_pcb_layer_panel_set_active_callback(ed->layer_panel,
                                                 on_active_layer_changed, ed);
    }

    GtkWidget *canvas_w = dc_pcb_canvas_widget(ed->canvas);
    gtk_widget_set_hexpand(canvas_w, TRUE);
    gtk_box_append(GTK_BOX(ed->hbox), canvas_w);

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
