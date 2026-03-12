#ifndef DC_TRANSFORM_PANEL_H
#define DC_TRANSFORM_PANEL_H

/*
 * transform_panel.h — Viewport overlay for editing object translate/rotate.
 *
 * Shows 6 entry fields (tx/ty/tz, rx/ry/rz) overlaid on the GL viewport.
 * Editing a value live-updates the SCAD source code in the code editor.
 */

#include <gtk/gtk.h>

typedef struct DC_TransformPanel DC_TransformPanel;
typedef struct DC_CodeEditor DC_CodeEditor;

/* Create a new transform panel. Returns NULL on failure. */
DC_TransformPanel *dc_transform_panel_new(void);

/* Free the panel. Safe with NULL. */
void dc_transform_panel_free(DC_TransformPanel *tp);

/* Get the top-level widget (to add as overlay child). */
GtkWidget *dc_transform_panel_widget(DC_TransformPanel *tp);

/* Set the code editor for live updates. Borrowed pointer. */
void dc_transform_panel_set_code_editor(DC_TransformPanel *tp,
                                         DC_CodeEditor *ed);

/* Show the panel for a selected object.
 * stmt_text: the SCAD statement text (to parse for current values).
 * line_start/line_end: 1-based line range in the editor. */
void dc_transform_panel_show(DC_TransformPanel *tp,
                              const char *stmt_text,
                              int line_start, int line_end);

/* Hide the panel (object deselected). */
void dc_transform_panel_hide(DC_TransformPanel *tp);

/* Set a callback to invoke when Enter is pressed in an entry field. */
typedef void (*DC_TransformEnterCb)(void *userdata);
void dc_transform_panel_set_enter_callback(DC_TransformPanel *tp,
                                            DC_TransformEnterCb cb,
                                            void *userdata);

/* Get current translate values from the panel. */
void dc_transform_panel_get_translate(DC_TransformPanel *tp,
                                       double *x, double *y, double *z);

/* Set translate values programmatically and update the code editor.
 * Batches all 3 entry updates into a single code replacement. */
void dc_transform_panel_set_translate(DC_TransformPanel *tp,
                                       double x, double y, double z);

/* Update translate display without modifying code (for live drag preview). */
void dc_transform_panel_set_translate_preview(DC_TransformPanel *tp,
                                               double x, double y, double z);

#endif /* DC_TRANSFORM_PANEL_H */
