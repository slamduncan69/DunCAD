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
    GtkWidget      *toolbar;     /* mode buttons */
    DC_SchCanvas   *canvas;
    DC_ESchematic  *sch;         /* owned */
    DC_ELibrary    *lib;         /* borrowed */
    DC_SchEditMode  mode;
    char           *current_path; /* owned, NULL if untitled */
};

/* =========================================================================
 * Toolbar callbacks
 * ========================================================================= */
static void
on_mode_select(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    DC_SchEditor *ed = userdata;
    ed->mode = DC_SCH_MODE_SELECT;
}

static void
on_mode_wire(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    DC_SchEditor *ed = userdata;
    ed->mode = DC_SCH_MODE_WIRE;
}

static void
on_mode_symbol(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    DC_SchEditor *ed = userdata;
    ed->mode = DC_SCH_MODE_PLACE_SYMBOL;
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

    /* Build UI */
    ed->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Toolbar */
    ed->toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(ed->toolbar, 4);
    gtk_widget_set_margin_end(ed->toolbar, 4);
    gtk_widget_set_margin_top(ed->toolbar, 2);
    gtk_widget_set_margin_bottom(ed->toolbar, 2);

    GtkWidget *btn_select = gtk_button_new_with_label("Select");
    GtkWidget *btn_wire = gtk_button_new_with_label("Wire");
    GtkWidget *btn_symbol = gtk_button_new_with_label("Symbol");

    g_signal_connect(btn_select, "clicked", G_CALLBACK(on_mode_select), ed);
    g_signal_connect(btn_wire, "clicked", G_CALLBACK(on_mode_wire), ed);
    g_signal_connect(btn_symbol, "clicked", G_CALLBACK(on_mode_symbol), ed);

    gtk_box_append(GTK_BOX(ed->toolbar), btn_select);
    gtk_box_append(GTK_BOX(ed->toolbar), btn_wire);
    gtk_box_append(GTK_BOX(ed->toolbar), btn_symbol);

    /* Mode label */
    GtkWidget *mode_label = gtk_label_new("Schematic Editor");
    gtk_widget_set_hexpand(mode_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(mode_label), 1.0);
    gtk_box_append(GTK_BOX(ed->toolbar), mode_label);

    gtk_box_append(GTK_BOX(ed->box), ed->toolbar);

    /* Separator */
    gtk_box_append(GTK_BOX(ed->box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Canvas */
    GtkWidget *canvas_w = dc_sch_canvas_widget(ed->canvas);
    gtk_widget_set_vexpand(canvas_w, TRUE);
    gtk_box_append(GTK_BOX(ed->box), canvas_w);

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
