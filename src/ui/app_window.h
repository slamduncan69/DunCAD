#ifndef DC_APP_WINDOW_H
#define DC_APP_WINDOW_H

/*
 * app_window.h — Top-level GTK4 application window for DunCAD.
 *
 * All UI construction lives in app_window.c.  main.c only calls
 * dc_app_window_create() and gtk_widget_show().
 *
 * Ownership:
 *   - dc_app_window_create() returns a GtkWidget * owned by GTK (it is
 *     a GtkApplicationWindow whose lifetime is managed by GApplication).
 *   - The caller must not free the returned widget directly; GTK handles
 *     destruction when the window is closed.
 */

#include <gtk/gtk.h>

/* -------------------------------------------------------------------------
 * dc_app_window_create — construct and return the main application window.
 *
 * Parameters:
 *   app — the GtkApplication instance; must not be NULL
 *
 * Returns: a GtkWidget * (GtkApplicationWindow); owned by GTK.
 *
 * The window is not yet shown; caller must call gtk_widget_show() or
 * gtk_window_present().
 * ---------------------------------------------------------------------- */
GtkWidget *dc_app_window_create(GtkApplication *app);

/* -------------------------------------------------------------------------
 * dc_app_window_set_project_name — update the header bar subtitle/title to
 * reflect the currently open project name.
 *
 * Parameters:
 *   window       — the GtkWidget * returned by dc_app_window_create()
 *   project_name — NUL-terminated string; NULL or empty defaults to
 *                  "No Project"
 * ---------------------------------------------------------------------- */
void dc_app_window_set_project_name(GtkWidget *window, const char *project_name);

/* -------------------------------------------------------------------------
 * dc_app_window_set_status — update the status bar text.
 *
 * Parameters:
 *   window — the GtkWidget * returned by dc_app_window_create()
 *   text   — NUL-terminated string to display; NULL clears the status bar
 * ---------------------------------------------------------------------- */
void dc_app_window_set_status(GtkWidget *window, const char *text);

#endif /* DC_APP_WINDOW_H */
