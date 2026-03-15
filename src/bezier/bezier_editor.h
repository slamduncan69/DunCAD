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

typedef struct DC_CodeEditor DC_CodeEditor;
typedef struct DC_BezierEditor DC_BezierEditor;

/* ---- Editor modes ---- */
typedef enum {
    DC_MODE_SELECT,           /* no tool: click/bbox select nodes, drag moves selection */
    DC_MODE_CLICK_TO_PLACE,   /* click to place/select/drag points */
    DC_MODE_SPLINE,           /* click to place through-points, auto-generate spline */
    DC_MODE_FREEHAND          /* drag to draw, Schneider fit on release */
} DC_EditorMode;

void          dc_bezier_editor_set_mode(DC_BezierEditor *editor,
                                         DC_EditorMode mode);
DC_EditorMode dc_bezier_editor_get_mode(const DC_BezierEditor *editor);

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

/* Set the code editor reference for Insert SCAD. */
void             dc_bezier_editor_set_code_editor(DC_BezierEditor *editor,
                                                   DC_CodeEditor *code_ed);

/* Insert self-contained bezier SCAD at the code editor cursor.
 * Returns 0 on success, -1 on error. */
int              dc_bezier_editor_insert_scad(DC_BezierEditor *editor,
                                               DC_Error *err);

/* ---- Edge profile editing ---- */

/* Profile metadata — stored when loading an edge profile for editing.
 * Contains the info needed to generate replacement code on apply. */
typedef struct {
    float  centroid[3];     /* SCAD-space centroid of the face */
    float  rot_angles[3];  /* Euler angles to orient extrusion along face normal */
    float  height;          /* object extent along face normal (for extrude height) */
    int    obj_idx;         /* GL viewport object index */
    int    face_idx;        /* face group index */
    int    line_start;      /* source code line range of original object */
    int    line_end;
    int    active;          /* 1 = profile editing mode is active */
} DC_ProfileMeta;

/* Load a 2D profile into the bezier editor, replacing current points.
 * points: array of {x,y} pairs, alternating on-curve/off-curve.
 * count: total number of points (must be even for closed shapes).
 * closed: 1 if the profile is a closed loop.
 * meta: profile metadata for code generation on apply (copied).
 * Returns 0 on success, -1 on failure. */
int dc_bezier_editor_load_profile(DC_BezierEditor *editor,
                                   const double *points, int count,
                                   int closed,
                                   const DC_ProfileMeta *meta);

/* Get the profile metadata (borrowed pointer, NULL if not in profile mode). */
const DC_ProfileMeta *dc_bezier_editor_get_profile_meta(
    const DC_BezierEditor *editor);

/* Clear profile editing mode. Doesn't clear the points. */
void dc_bezier_editor_clear_profile(DC_BezierEditor *editor);

/* Profile apply callback — called when user clicks "Apply Profile".
 * editor: the bezier editor with the modified curve.
 * meta: the profile metadata (centroid, rotation, line range, etc.).
 * userdata: user-provided context. */
typedef void (*DC_ProfileApplyCb)(DC_BezierEditor *editor,
                                   const DC_ProfileMeta *meta,
                                   void *userdata);

/* Set the callback for profile apply. */
void dc_bezier_editor_set_profile_apply_cb(DC_BezierEditor *editor,
                                             DC_ProfileApplyCb cb,
                                             void *userdata);

#endif /* DC_BEZIER_EDITOR_H */
