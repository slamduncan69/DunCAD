#ifndef DC_GL_VIEWPORT_H
#define DC_GL_VIEWPORT_H

/*
 * gl_viewport.h — Real-time OpenGL 3D viewport for mesh display.
 *
 * GtkGLArea-based widget with:
 *   - Orbit/pan/zoom camera (OpenSCAD-style controls)
 *   - Grid floor and axis indicator
 *   - Directional + ambient Phong lighting
 *   - STL mesh rendering
 *
 * Ownership:
 *   - DC_GlViewport is opaque; created with dc_gl_viewport_new(),
 *     freed with dc_gl_viewport_free().
 *   - dc_gl_viewport_widget() returns a borrowed GtkWidget*.
 */

#include <gtk/gtk.h>

typedef struct DC_GlViewport DC_GlViewport;

/* Create a new GL viewport. Returns NULL on failure. */
DC_GlViewport *dc_gl_viewport_new(void);

/* Free the viewport. Safe with NULL. */
void dc_gl_viewport_free(DC_GlViewport *vp);

/* Get the top-level widget (GtkGLArea). */
GtkWidget *dc_gl_viewport_widget(DC_GlViewport *vp);

/* Load an STL mesh file for display. Returns 0 on success. */
int dc_gl_viewport_load_stl(DC_GlViewport *vp, const char *stl_path);

/* Clear the current mesh. */
void dc_gl_viewport_clear_mesh(DC_GlViewport *vp);

/* Reset camera to fit the loaded mesh. */
void dc_gl_viewport_reset_camera(DC_GlViewport *vp);

/* Toggle perspective / orthographic projection. */
void dc_gl_viewport_toggle_ortho(DC_GlViewport *vp);

/* Toggle grid visibility. */
void dc_gl_viewport_toggle_grid(DC_GlViewport *vp);

/* Toggle axis indicator visibility. */
void dc_gl_viewport_toggle_axes(DC_GlViewport *vp);

/* ---- Multi-object support ---- */

/* Clear all loaded objects. */
void dc_gl_viewport_clear_objects(DC_GlViewport *vp);

/* Add a mesh object. Returns object index (0-based), or -1 on failure.
 * line_start/line_end are 1-based SCAD source line range. */
int dc_gl_viewport_add_object(DC_GlViewport *vp, const char *stl_path,
                               int line_start, int line_end);

/* Get the currently selected object index (-1 if none). */
int dc_gl_viewport_get_selected(DC_GlViewport *vp);

/* Get the line range of a selected object. Returns 0 on success. */
int dc_gl_viewport_get_object_lines(DC_GlViewport *vp, int obj_idx,
                                     int *line_start, int *line_end);

/* Select an object by index (-1 to deselect). Triggers pick callback. */
void dc_gl_viewport_select_object(DC_GlViewport *vp, int obj_idx);

/* Get the number of loaded objects. */
int dc_gl_viewport_get_object_count(DC_GlViewport *vp);

/* ---- Camera state ---- */

/* Get/set camera orbit center (target point). */
void dc_gl_viewport_get_camera_center(DC_GlViewport *vp, float *x, float *y, float *z);
void dc_gl_viewport_set_camera_center(DC_GlViewport *vp, float x, float y, float z);

/* Get/set camera distance from orbit center. */
float dc_gl_viewport_get_camera_dist(DC_GlViewport *vp);
void dc_gl_viewport_set_camera_dist(DC_GlViewport *vp, float dist);

/* Get/set camera angles (theta=azimuth, phi=elevation, degrees). */
void dc_gl_viewport_get_camera_angles(DC_GlViewport *vp, float *theta, float *phi);
void dc_gl_viewport_set_camera_angles(DC_GlViewport *vp, float theta, float phi);

/* Get projection mode: 0=perspective, 1=ortho. */
int dc_gl_viewport_get_ortho(DC_GlViewport *vp);

/* Get visibility flags: grid, axes. */
int dc_gl_viewport_get_grid(DC_GlViewport *vp);
int dc_gl_viewport_get_axes(DC_GlViewport *vp);

/* Selection callback — called when user clicks an object.
 * obj_idx is -1 if clicked on background. */
typedef void (*DC_GlPickCb)(int obj_idx, int line_start, int line_end,
                             void *userdata);
void dc_gl_viewport_set_pick_callback(DC_GlViewport *vp,
                                       DC_GlPickCb cb, void *userdata);

/* Move callback — called when user drags a selected object.
 * phase: 0=drag started, 1=moving, 2=drag ended.
 * dx/dy/dz is cumulative world-space delta from drag start.
 * Axis constraints (Z/X/C keys) are already applied. */
typedef void (*DC_GlMoveCb)(int obj_idx, int phase,
                             float dx, float dy, float dz,
                             void *userdata);
void dc_gl_viewport_set_move_callback(DC_GlViewport *vp,
                                       DC_GlMoveCb cb, void *userdata);

/* Set a viewport-space translate offset for an object (live preview).
 * Applied as a model matrix during rendering — does not modify mesh data.
 * Reset to (0,0,0) when objects are cleared/reloaded. */
void dc_gl_viewport_set_object_translate(DC_GlViewport *vp, int obj_idx,
                                          float x, float y, float z);

/* Fit camera to encompass all loaded objects (combined bounding box).
 * Preserves orbit angles (theta/phi). Rebuilds grid/axes to match. */
void dc_gl_viewport_fit_all_objects(DC_GlViewport *vp);

/* Capture the current viewport to a PNG file. Returns 0 on success.
 * Must be called from the GTK main thread. */
int dc_gl_viewport_capture_png(DC_GlViewport *vp, const char *path);

/* Select an object without firing the pick callback (avoids feedback loops). */
void dc_gl_viewport_select_object_quiet(DC_GlViewport *vp, int obj_idx);

/* Lock/unlock user interaction (picking, object moving).
 * Camera orbit/pan/zoom still work. Used when AI is building. */
void dc_gl_viewport_set_locked(DC_GlViewport *vp, int locked);
int  dc_gl_viewport_get_locked(DC_GlViewport *vp);

#endif /* DC_GL_VIEWPORT_H */
