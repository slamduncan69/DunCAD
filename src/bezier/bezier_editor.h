#ifndef DC_BEZIER_EDITOR_H
#define DC_BEZIER_EDITOR_H

/*
 * bezier_editor.h — Chained quadratic bezier curve editor.
 *
 * Click to place control points. Points are chained as quadratic
 * bezier segments: segment i uses points 2i, 2i+1, 2i+2.
 * Even-indexed points (0, 2, 4, ...) are on-curve endpoints.
 * Odd-indexed points (1, 3, 5, ...) are off-curve control points.
 * Adjacent segments share endpoints. All points are draggable.
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
