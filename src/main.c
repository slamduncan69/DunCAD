#include "ui/app_window.h"
#include "core/log.h"

#include <gtk/gtk.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Application lifecycle callbacks
 * ---------------------------------------------------------------------- */

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    GtkWidget *window = dc_app_window_create(app);
    gtk_window_present(GTK_WINDOW(window));

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "DunCAD activated");
}

static void
on_shutdown(GtkApplication *app, gpointer user_data)
{
    (void)app;
    (void)user_data;

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "DunCAD shutting down");
    dc_log_shutdown();
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(int argc, char **argv)
{
    /* Initialise structured logger — writes to duncad.log in CWD */
    dc_log_init("duncad.log");
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "DunCAD starting up");

    GtkApplication *app = gtk_application_new(
        "io.duncad.ide",
        G_APPLICATION_DEFAULT_FLAGS
    );

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
