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
#include "scad/scad_export.h"

typedef struct DC_BezierEditor DC_BezierEditor;

DC_BezierEditor *dc_bezier_editor_new(void);
void             dc_bezier_editor_free(DC_BezierEditor *editor);
GtkWidget       *dc_bezier_editor_widget(DC_BezierEditor *editor);
void             dc_bezier_editor_set_window(DC_BezierEditor *editor,
                                             GtkWidget *window);
int              dc_bezier_editor_point_count(const DC_BezierEditor *editor);
int              dc_bezier_editor_selected_point(const DC_BezierEditor *editor);
int              dc_bezier_editor_is_closed(const DC_BezierEditor *editor);
int              dc_bezier_editor_get_point(const DC_BezierEditor *editor,
                                            int index,
                                            double *x, double *y);
void             dc_bezier_editor_set_point(DC_BezierEditor *editor,
                                            int index,
                                            double x, double y);
int              dc_bezier_editor_is_juncture(const DC_BezierEditor *editor,
                                              int index);
int              dc_bezier_editor_get_chain_mode(const DC_BezierEditor *editor);

/* Programmatic point selection (-1 to deselect). */
void             dc_bezier_editor_select(DC_BezierEditor *editor, int index);

/* Add a point at world coordinates (like clicking on empty canvas).
 * Returns 0 on success, -1 on failure. */
int              dc_bezier_editor_add_point_at(DC_BezierEditor *editor,
                                               double x, double y);

/* Delete the currently selected point. No-op if nothing selected. */
void             dc_bezier_editor_delete_selected(DC_BezierEditor *editor);

/* Set global chain mode (0=off, 1=on). */
void             dc_bezier_editor_set_chain_mode(DC_BezierEditor *editor,
                                                  int on);

/* Set juncture flag for a specific point (0=smooth, 1=juncture). */
void             dc_bezier_editor_set_juncture(DC_BezierEditor *editor,
                                                int index, int on);

/* Get the canvas owned by this editor (for zoom/pan/render access). */
struct DC_BezierCanvas *dc_bezier_editor_get_canvas(DC_BezierEditor *editor);

/* Extract juncture-delimited spans for export. Caller must free with
 * dc_scad_spans_free(). Returns NULL if < 2 points. */
DC_ScadSpan     *dc_bezier_editor_get_spans(const DC_BezierEditor *editor,
                                            int *num_spans);

/* Export current shape to .scad file at path. Returns 0/-1. */
int              dc_bezier_editor_export_scad(DC_BezierEditor *editor,
                                              const char *path,
                                              DC_Error *err);

#endif /* DC_BEZIER_EDITOR_H */
