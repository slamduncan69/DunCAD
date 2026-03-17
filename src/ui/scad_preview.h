#ifndef DC_SCAD_PREVIEW_H
#define DC_SCAD_PREVIEW_H

/*
 * scad_preview.h — OpenSCAD 3D preview panel.
 *
 * Renders the current code editor contents through OpenSCAD and
 * displays the resulting PNG in a zoomable image view.
 *
 * Ownership:
 *   - DC_ScadPreview is opaque; created with dc_scad_preview_new(),
 *     freed with dc_scad_preview_free().
 *   - dc_scad_preview_widget() returns a borrowed GtkWidget*.
 */

#include <gtk/gtk.h>

typedef struct DC_ScadPreview DC_ScadPreview;

/* Forward declaration */
typedef struct DC_CodeEditor DC_CodeEditor;

/* Create a new preview panel. Returns NULL on failure. */
DC_ScadPreview *dc_scad_preview_new(void);

/* Free the preview. Safe with NULL. */
void dc_scad_preview_free(DC_ScadPreview *pv);

/* Get the top-level widget (GtkBox: toolbar + image area). */
GtkWidget *dc_scad_preview_widget(DC_ScadPreview *pv);

/* Set the code editor to pull SCAD source from when rendering. */
void dc_scad_preview_set_code_editor(DC_ScadPreview *pv, DC_CodeEditor *ed);

/* Trigger a render (preserves camera position). */
void dc_scad_preview_render(DC_ScadPreview *pv);

/* Trigger a render and refit camera to scene (F5 / explicit render). */
void dc_scad_preview_render_refit(DC_ScadPreview *pv);

/* Get the GL viewport (for setting pick callbacks, etc.). Borrowed pointer. */
struct DC_GlViewport *dc_scad_preview_get_viewport(DC_ScadPreview *pv);

/* Get the transform panel (for pick callback wiring). Borrowed pointer. */
struct DC_TransformPanel *dc_scad_preview_get_transform(DC_ScadPreview *pv);

/* Get the current render status text (borrowed, from status label). */
const char *dc_scad_preview_get_status(DC_ScadPreview *pv);

/* Returns 1 if a render (preview or HQ) is in progress. */
int dc_scad_preview_is_rendering(DC_ScadPreview *pv);

/* Voxel resolution: how many cells per longest axis when voxelizing STL.
 * Default 64. Higher = smoother but slower. Maps to OpenSCAD $fn. */
void dc_scad_preview_set_voxel_resolution(DC_ScadPreview *pv, int resolution);
int  dc_scad_preview_get_voxel_resolution(DC_ScadPreview *pv);

#endif /* DC_SCAD_PREVIEW_H */
