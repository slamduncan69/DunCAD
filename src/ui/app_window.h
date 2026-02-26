#ifndef EF_APP_WINDOW_H
#define EF_APP_WINDOW_H

/*
 * app_window.h — Top-level GTK4 application window for ElectroForge IDE.
 *
 * All UI construction lives in app_window.c.  main.c only calls
 * ef_app_window_create() and gtk_widget_show().
 *
 * Ownership:
 *   - ef_app_window_create() returns a GtkWidget * owned by GTK (it is
 *     a GtkApplicationWindow whose lifetime is managed by GApplication).
 *   - The caller must not free the returned widget directly; GTK handles
 *     destruction when the window is closed.
 */

#include <gtk/gtk.h>

/* -------------------------------------------------------------------------
 * ef_app_window_create — construct and return the main application window.
 *
 * Parameters:
 *   app — the GtkApplication instance; must not be NULL
 *
 * Returns: a GtkWidget * (GtkApplicationWindow); owned by GTK.
 *
 * The window is not yet shown; caller must call gtk_widget_show() or
 * gtk_window_present().
 * ---------------------------------------------------------------------- */
GtkWidget *ef_app_window_create(GtkApplication *app);

/* -------------------------------------------------------------------------
 * ef_app_window_set_project_name — update the header bar subtitle/title to
 * reflect the currently open project name.
 *
 * Parameters:
 *   window       — the GtkWidget * returned by ef_app_window_create()
 *   project_name — NUL-terminated string; NULL or empty defaults to
 *                  "No Project"
 * ---------------------------------------------------------------------- */
void ef_app_window_set_project_name(GtkWidget *window, const char *project_name);

/* -------------------------------------------------------------------------
 * ef_app_window_set_status — update the status bar text.
 *
 * Parameters:
 *   window — the GtkWidget * returned by ef_app_window_create()
 *   text   — NUL-terminated string to display; NULL clears the status bar
 * ---------------------------------------------------------------------- */
void ef_app_window_set_status(GtkWidget *window, const char *text);

#endif /* EF_APP_WINDOW_H */
