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

    GtkWidget *window = ef_app_window_create(app);
    gtk_window_present(GTK_WINDOW(window));

    ef_log(EF_LOG_INFO, EF_LOG_EVENT_APP, "ElectroForge IDE activated");
}

static void
on_shutdown(GtkApplication *app, gpointer user_data)
{
    (void)app;
    (void)user_data;

    ef_log(EF_LOG_INFO, EF_LOG_EVENT_APP, "ElectroForge IDE shutting down");
    ef_log_shutdown();
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(int argc, char **argv)
{
    /* Initialise structured logger — writes to electroforge.log in CWD */
    ef_log_init("electroforge.log");
    ef_log(EF_LOG_INFO, EF_LOG_EVENT_APP, "ElectroForge IDE starting up");

    GtkApplication *app = gtk_application_new(
        "io.electroforge.ide",
        G_APPLICATION_DEFAULT_FLAGS
    );

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
