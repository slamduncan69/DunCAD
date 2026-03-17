#define _POSIX_C_SOURCE 200809L

#include "sch_editor.h"
#include "sch_canvas.h"
#include "eda/eda_schematic.h"
#include "eda/eda_library.h"
#include "core/error.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal state
 * ========================================================================= */
struct DC_SchEditor {
    GtkWidget      *box;         /* top-level GtkBox (V) */
    GtkWidget      *toolbar;     /* top status bar */
    DC_SchCanvas   *canvas;
    DC_ESchematic  *sch;         /* owned */
    DC_ELibrary    *lib;         /* borrowed */
    DC_SchEditMode  mode;
    char           *current_path; /* owned, NULL if untitled */
    DC_SchPlaceCallback place_cb;
    void               *place_cb_data;
};

/* =========================================================================
 * Toolbar callbacks
 * ========================================================================= */
static void on_mode_select(GtkButton *b, gpointer d)
    { (void)b; ((DC_SchEditor*)d)->mode = DC_SCH_MODE_SELECT; }
static void on_mode_wire(GtkButton *b, gpointer d)
    { (void)b; ((DC_SchEditor*)d)->mode = DC_SCH_MODE_WIRE; }
static void on_mode_symbol(GtkButton *b, gpointer d)
{
    (void)b;
    DC_SchEditor *ed = d;
    if (ed->place_cb) {
        ed->place_cb(DC_SCH_MODE_PLACE_SYMBOL, ed->place_cb_data);
    } else {
        ed->mode = DC_SCH_MODE_PLACE_SYMBOL;
    }
}
static void on_mode_label(GtkButton *b, gpointer d)
    { (void)b; ((DC_SchEditor*)d)->mode = DC_SCH_MODE_PLACE_LABEL; }
static void on_mode_move(GtkButton *b, gpointer d)
    { (void)b; ((DC_SchEditor*)d)->mode = DC_SCH_MODE_MOVE; }

/* =========================================================================
 * Helper: add a tool button to a vertical toolbar
 * ========================================================================= */
static GtkWidget *
add_tool_btn(GtkWidget *box, const char *label, GCallback cb, gpointer data)
{
    GtkWidget *btn = gtk_button_new_with_label(label);
    gtk_widget_set_size_request(btn, 36, 36);
    gtk_widget_set_tooltip_text(btn, label);
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
DC_SchEditor *
dc_sch_editor_new(void)
{
    DC_SchEditor *ed = calloc(1, sizeof(*ed));
    if (!ed) return NULL;

    ed->sch = dc_eschematic_new();
    if (!ed->sch) { free(ed); return NULL; }

    ed->canvas = dc_sch_canvas_new();
    if (!ed->canvas) { dc_eschematic_free(ed->sch); free(ed); return NULL; }

    dc_sch_canvas_set_schematic(ed->canvas, ed->sch);
    dc_sch_canvas_set_editor(ed->canvas, ed);

    /* Build UI */
    ed->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Top toolbar — status label */
    ed->toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(ed->toolbar, 4);
    gtk_widget_set_margin_end(ed->toolbar, 4);
    gtk_widget_set_margin_top(ed->toolbar, 2);
    gtk_widget_set_margin_bottom(ed->toolbar, 2);

    GtkWidget *title = gtk_label_new("Schematic Editor");
    gtk_widget_set_hexpand(title, TRUE);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_append(GTK_BOX(ed->toolbar), title);

    gtk_box_append(GTK_BOX(ed->box), ed->toolbar);
    gtk_box_append(GTK_BOX(ed->box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Horizontal layout: canvas + right-side vertical toolbar */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(hbox, TRUE);

    /* Canvas (center, expandable) */
    GtkWidget *canvas_w = dc_sch_canvas_widget(ed->canvas);
    gtk_widget_set_hexpand(canvas_w, TRUE);
    gtk_box_append(GTK_BOX(hbox), canvas_w);

    /* Right-side vertical tool toolbar (KiCad-style) */
    gtk_box_append(GTK_BOX(hbox),
                   gtk_separator_new(GTK_ORIENTATION_VERTICAL));

    GtkWidget *tool_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(tool_bar, 2);
    gtk_widget_set_margin_bottom(tool_bar, 2);
    gtk_widget_set_size_request(tool_bar, 40, -1);

    add_tool_btn(tool_bar, "Sel",  G_CALLBACK(on_mode_select), ed);
    add_tool_btn(tool_bar, "Wire", G_CALLBACK(on_mode_wire), ed);
    add_tool_btn(tool_bar, "Sym",  G_CALLBACK(on_mode_symbol), ed);
    add_tool_btn(tool_bar, "Lbl",  G_CALLBACK(on_mode_label), ed);
    add_tool_btn(tool_bar, "Mov",  G_CALLBACK(on_mode_move), ed);

    gtk_box_append(GTK_BOX(hbox), tool_bar);

    gtk_box_append(GTK_BOX(ed->box), hbox);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Schematic editor created");
    return ed;
}

void
dc_sch_editor_free(DC_SchEditor *ed)
{
    if (!ed) return;
    dc_sch_canvas_free(ed->canvas);
    dc_eschematic_free(ed->sch);
    free(ed->current_path);
    free(ed);
}

GtkWidget *
dc_sch_editor_widget(DC_SchEditor *ed)
{
    return ed ? ed->box : NULL;
}

DC_ESchematic *
dc_sch_editor_get_schematic(DC_SchEditor *ed)
{
    return ed ? ed->sch : NULL;
}

DC_SchCanvas *
dc_sch_editor_get_canvas(DC_SchEditor *ed)
{
    return ed ? ed->canvas : NULL;
}

void
dc_sch_editor_set_library(DC_SchEditor *ed, DC_ELibrary *lib)
{
    if (!ed) return;
    ed->lib = lib;
    dc_sch_canvas_set_library(ed->canvas, lib);
}

int
dc_sch_editor_load(DC_SchEditor *ed, const char *path)
{
    if (!ed || !path) return -1;

    DC_Error err = {0};
    DC_ESchematic *sch = dc_eschematic_load(path, &err);
    if (!sch) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_EDA,
               "Failed to load schematic: %s", err.message);
        return -1;
    }

    dc_eschematic_free(ed->sch);
    ed->sch = sch;
    dc_sch_canvas_set_schematic(ed->canvas, ed->sch);

    free(ed->current_path);
    ed->current_path = strdup(path);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Loaded schematic: %s", path);
    return 0;
}

int
dc_sch_editor_save(DC_SchEditor *ed, const char *path)
{
    if (!ed) return -1;
    const char *save_path = path ? path : ed->current_path;
    if (!save_path) return -1;

    DC_Error err = {0};
    int rc = dc_eschematic_save(ed->sch, save_path, &err);
    if (rc != 0) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_EDA,
               "Failed to save schematic: %s", err.message);
        return -1;
    }

    if (path && (!ed->current_path || strcmp(path, ed->current_path) != 0)) {
        free(ed->current_path);
        ed->current_path = strdup(path);
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Saved schematic: %s", save_path);
    return 0;
}

void dc_sch_editor_set_mode(DC_SchEditor *ed, DC_SchEditMode mode)
{
    if (ed) ed->mode = mode;
}

DC_SchEditMode dc_sch_editor_get_mode(const DC_SchEditor *ed)
{
    return ed ? ed->mode : DC_SCH_MODE_SELECT;
}

void dc_sch_editor_set_place_callback(DC_SchEditor *ed,
                                        DC_SchPlaceCallback cb, void *userdata)
{
    if (!ed) return;
    ed->place_cb = cb;
    ed->place_cb_data = userdata;
}
