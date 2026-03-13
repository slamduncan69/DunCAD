#ifndef DC_SHAPE_MENU_H
#define DC_SHAPE_MENU_H

/*
 * shape_menu.h — Right-click context menu and Insert menu for 3D shapes.
 *
 * Provides:
 *   - Right-click on GL viewport: Insert Shape / Modify Shape
 *   - "Insert" menu in the menu bar
 *   - Inserts/wraps SCAD code in the code editor
 */

#include <gtk/gtk.h>

typedef struct DC_CodeEditor DC_CodeEditor;
typedef struct DC_GlViewport DC_GlViewport;
typedef struct DC_TransformPanel DC_TransformPanel;
typedef struct DC_ScadPreview DC_ScadPreview;
typedef struct DC_BezierEditor DC_BezierEditor;

/* Attach the right-click context menu to the GL viewport widget.
 * code_ed:    the code editor to insert/modify SCAD in (borrowed).
 * gl_vp:      the GL viewport for selection state (borrowed).
 * transform:  transform panel to show after adding translate/rotate (borrowed, may be NULL).
 * preview:    scad preview for triggering renders (borrowed, may be NULL).
 * bez_ed:     bezier editor for edge profile editing (borrowed, may be NULL). */
void dc_shape_menu_attach(GtkWidget *gl_widget,
                           DC_CodeEditor *code_ed,
                           DC_GlViewport *gl_vp,
                           DC_TransformPanel *transform,
                           DC_ScadPreview *preview,
                           DC_BezierEditor *bez_ed);

/* Build the "Insert" submenu for the menu bar. Caller owns the result. */
GMenuModel *dc_shape_menu_build_insert_menu(void);

#endif /* DC_SHAPE_MENU_H */
