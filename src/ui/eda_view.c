#define _POSIX_C_SOURCE 200809L

#include "eda_view.h"
#include "code_editor.h"
#include "eda_ui/sch_editor.h"
#include "eda_ui/sch_canvas.h"
#include "eda_ui/pcb_editor.h"
#include "eda_ui/pcb_canvas.h"
#include "eda/eda_schematic.h"
#include "eda/eda_pcb.h"
#include "eda/eda_cubeiform_export.h"
#include "cubeiform/cubeiform_eda.h"
#include "voxel/voxel.h"
#include "gl/gl_viewport.h"
#include "ui/app_window.h"
#include "ui/scad_preview.h"
#include "core/error.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal state
 * ========================================================================= */
struct DC_EdaView {
    GtkWidget      *paned;       /* top-level GtkPaned (H) */
    DC_SchEditor   *sch_editor;  /* owned */
    DC_PcbEditor   *pcb_editor;  /* owned */
    DC_CodeEditor  *code_editor; /* owned */
    DC_VoxelGrid   *voxel_grid;  /* owned — last voxel output, for lifetime */
};

/* =========================================================================
 * Toolbar callbacks
 * ========================================================================= */
static void
on_execute(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    dc_eda_view_execute_cubeiform(userdata);
}

static void
on_export(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    dc_eda_view_export_to_cubeiform(userdata);
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
DC_EdaView *
dc_eda_view_new(void)
{
    DC_EdaView *v = calloc(1, sizeof(*v));
    if (!v) return NULL;

    v->sch_editor = dc_sch_editor_new();
    v->pcb_editor = dc_pcb_editor_new();
    v->code_editor = dc_code_editor_new();

    if (!v->sch_editor || !v->pcb_editor || !v->code_editor) {
        dc_sch_editor_free(v->sch_editor);
        dc_pcb_editor_free(v->pcb_editor);
        dc_code_editor_free(v->code_editor);
        free(v);
        return NULL;
    }

    /* Build layout: LEFT (code editor) | RIGHT (notebook: schematic+PCB) */
    v->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

    /* Left: code editor + sync buttons */
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(left_box, 250, -1);

    /* Sync toolbar */
    GtkWidget *sync_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(sync_bar, 4);
    gtk_widget_set_margin_end(sync_bar, 4);
    gtk_widget_set_margin_top(sync_bar, 2);
    gtk_widget_set_margin_bottom(sync_bar, 2);

    GtkWidget *btn_exec = gtk_button_new_with_label("Execute");
    GtkWidget *btn_export = gtk_button_new_with_label("Export");
    GtkWidget *label = gtk_label_new("Cubeiform EDA");

    g_signal_connect(btn_exec, "clicked", G_CALLBACK(on_execute), v);
    g_signal_connect(btn_export, "clicked", G_CALLBACK(on_export), v);

    gtk_box_append(GTK_BOX(sync_bar), btn_exec);
    gtk_box_append(GTK_BOX(sync_bar), btn_export);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 1.0);
    gtk_box_append(GTK_BOX(sync_bar), label);

    gtk_box_append(GTK_BOX(left_box), sync_bar);
    gtk_box_append(GTK_BOX(left_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *code_w = dc_code_editor_widget(v->code_editor);
    gtk_widget_set_vexpand(code_w, TRUE);
    gtk_box_append(GTK_BOX(left_box), code_w);

    /* Right: GtkNotebook with Schematic and PCB tabs */
    GtkWidget *notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(notebook, TRUE);
    gtk_widget_set_size_request(notebook, 500, -1);

    /* Schematic page */
    GtkWidget *sch_w = dc_sch_editor_widget(v->sch_editor);
    gtk_widget_set_vexpand(sch_w, TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), sch_w,
                              gtk_label_new("Schematic"));

    /* PCB page */
    GtkWidget *pcb_w = dc_pcb_editor_widget(v->pcb_editor);
    gtk_widget_set_vexpand(pcb_w, TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), pcb_w,
                              gtk_label_new("PCB"));

    gtk_paned_set_start_child(GTK_PANED(v->paned), left_box);
    gtk_paned_set_end_child(GTK_PANED(v->paned), notebook);
    gtk_paned_set_position(GTK_PANED(v->paned), 300);

    /* Set initial Cubeiform template */
    dc_code_editor_set_text(v->code_editor,
        "// Cubeiform EDA — write schematic and PCB here\n"
        "\n"
        "schematic {\n"
        "    // component R1 = \"Device:R_Small\" at 100, 50;\n"
        "}\n"
        "\n"
        "pcb {\n"
        "    // outline { rect(50, 30); }\n"
        "    // place R1 at 10, 15 on F.Cu;\n"
        "}\n");

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "EDA view created");
    return v;
}

void
dc_eda_view_free(DC_EdaView *v)
{
    if (!v) return;
    dc_sch_editor_free(v->sch_editor);
    dc_pcb_editor_free(v->pcb_editor);
    dc_code_editor_free(v->code_editor);
    dc_voxel_grid_free(v->voxel_grid);
    free(v);
}

GtkWidget *dc_eda_view_widget(DC_EdaView *v) { return v ? v->paned : NULL; }
DC_SchEditor *dc_eda_view_get_sch_editor(DC_EdaView *v) { return v ? v->sch_editor : NULL; }
DC_PcbEditor *dc_eda_view_get_pcb_editor(DC_EdaView *v) { return v ? v->pcb_editor : NULL; }
DC_CodeEditor *dc_eda_view_get_code_editor(DC_EdaView *v) { return v ? v->code_editor : NULL; }

/* =========================================================================
 * Sync operations
 * ========================================================================= */
int
dc_eda_view_execute_cubeiform(DC_EdaView *v)
{
    if (!v) return -1;

    char *src = dc_code_editor_get_text(v->code_editor);
    if (!src) return -1;

    DC_Error err = {0};
    DC_ESchematic *sch = dc_sch_editor_get_schematic(v->sch_editor);
    DC_EPcb *pcb = dc_pcb_editor_get_pcb(v->pcb_editor);
    DC_VoxelGrid *vox_grid = NULL;

    int rc = dc_cubeiform_execute_full(src, sch, pcb, &vox_grid, NULL, &err);
    free(src);

    if (rc != 0) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_EDA,
               "Cubeiform execution failed: %s", err.message);
        dc_voxel_grid_free(vox_grid);
        return -1;
    }

    /* Refresh canvases */
    dc_sch_canvas_queue_redraw(dc_sch_editor_get_canvas(v->sch_editor));
    dc_pcb_canvas_queue_redraw(dc_pcb_editor_get_canvas(v->pcb_editor));
    dc_pcb_editor_update_ratsnest(v->pcb_editor);

    /* If voxel block produced a grid, display it in the 3D viewport */
    if (vox_grid) {
        /* Get the GL viewport from the app window via the ScadPreview */
        DC_ScadPreview *pv = dc_app_window_get_scad_preview(
            gtk_widget_get_ancestor(dc_eda_view_widget(v), GTK_TYPE_WINDOW));
        if (pv) {
            DC_GlViewport *gl_vp = dc_scad_preview_get_viewport(pv);
            if (gl_vp) {
                dc_gl_viewport_set_voxel_grid(gl_vp, vox_grid);
                /* Switch to 3D CAD tab to see the result */
                GtkWidget *win = gtk_widget_get_ancestor(
                    dc_eda_view_widget(v), GTK_TYPE_WINDOW);
                if (win) dc_app_window_set_tab(win, "3d_cad");
            }
        }
        /* Grid must stay alive while viewport references it —
         * store it on the eda_view for lifetime management */
        dc_voxel_grid_free(v->voxel_grid);
        v->voxel_grid = vox_grid;
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Cubeiform executed successfully");
    return 0;
}

int
dc_eda_view_export_to_cubeiform(DC_EdaView *v)
{
    if (!v) return -1;

    DC_Error err = {0};
    DC_ESchematic *sch = dc_sch_editor_get_schematic(v->sch_editor);

    char *dcad = dc_eschematic_to_cubeiform(sch, &err);
    if (!dcad) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_EDA,
               "Export to Cubeiform failed: %s", err.message);
        return -1;
    }

    dc_code_editor_set_text(v->code_editor, dcad);
    free(dcad);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Exported schematic to Cubeiform");
    return 0;
}
