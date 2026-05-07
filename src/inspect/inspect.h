#ifndef DC_INSPECT_H
#define DC_INSPECT_H

/*
 * inspect.h — Unix socket inspection server for DunCAD.
 *
 * Provides bidirectional control of the running DunCAD instance via a
 * Unix domain socket at DC_INSPECT_SOCK_PATH. Commands are newline-
 * terminated text; responses are JSON followed by newline.
 *
 * The server runs in the GLib main loop (same thread as GTK), so all
 * command handlers can safely call GTK and editor functions.
 *
 * Use the companion CLI tool `duncad-inspect` to interact.
 */

#include <gtk/gtk.h>

#define DC_INSPECT_SOCK_PATH "/tmp/duncad.sock"

/* Start the inspect server with access to the full application window.
 * All subsystems (bezier editor, code editor, GL viewport, SCAD preview,
 * transform panel) are extracted from the window via getters.
 * Returns 0 on success, -1 on failure. */
int dc_inspect_start(GtkWidget *window);

/* Stop the server and clean up the socket file. */
void dc_inspect_stop(void);

/* Execute an inspect command in-process.
 * Returns a malloc'd response string (caller must free). */
char *dc_inspect_dispatch(const char *cmd);

/* Set the active bezier mesh for 2D editor interaction.
 * mesh_ptr is a const ts_bezier_mesh* — borrowed, must stay alive. */
void dc_inspect_set_bezier_mesh(const void *mesh_ptr);

/* Serialize the live bezier mesh to Cubeiform source with explicit CPs.
 * Returns a malloc'd string like "bezier_mesh {\n  grid(2, 3);\n  cp[0][0] = ...\n}\n"
 * or NULL if no mesh exists. Caller must free(). */
char *dc_inspect_bezier_mesh_to_cubeiform(void);

/* Publish a borrowed voxel grid for read-only inspect commands
 * (voxel_state, marching_cubes). Caller retains ownership and must
 * keep the grid alive until publishing NULL or a new pointer.
 * Pass NULL to clear the borrow. If the inspect module currently
 * owns a grid (from voxel_sphere/box/csg/etc.), that grid is freed
 * before adopting the borrow. */
void dc_inspect_set_voxel_grid(const void *grid);

#endif /* DC_INSPECT_H */
