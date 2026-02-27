#ifndef DC_BEZIER_EDITOR_H
#define DC_BEZIER_EDITOR_H

/*
 * bezier_editor.h — Single quadratic bezier curve editor.
 *
 * Place exactly 3 control points (P0, P1, P2). P0 and P2 are
 * on-curve endpoints. P1 is the off-curve control point.
 * The quadratic bezier B(t) = (1-t)^2 P0 + 2(1-t)t P1 + t^2 P2
 * is drawn and updates live as you drag any point.
 */

#include <gtk/gtk.h>

typedef struct DC_BezierEditor DC_BezierEditor;

DC_BezierEditor *dc_bezier_editor_new(void);
void             dc_bezier_editor_free(DC_BezierEditor *editor);
GtkWidget       *dc_bezier_editor_widget(DC_BezierEditor *editor);
void             dc_bezier_editor_set_window(DC_BezierEditor *editor,
                                             GtkWidget *window);
int              dc_bezier_editor_point_count(const DC_BezierEditor *editor);
int              dc_bezier_editor_selected_point(const DC_BezierEditor *editor);

#endif /* DC_BEZIER_EDITOR_H */
