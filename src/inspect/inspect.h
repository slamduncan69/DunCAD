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

#endif /* DC_INSPECT_H */
