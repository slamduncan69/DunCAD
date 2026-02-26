#include "app_window.h"
#include "core/log.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Private state attached to each window instance via g_object_set_data().
 * We use string keys rather than a custom GObject subclass to keep Phase 1
 * simple; Phase 2 should migrate to a proper GtkApplicationWindow subclass.
 *
 * TODO: Phase 2 — convert EFAppWindow to a GObject subclass with typed
 * instance data instead of g_object_set_data() string keys.
 * ---------------------------------------------------------------------- */
#define EF_KEY_HEADER_BAR  "ef-header-bar"
#define EF_KEY_STATUS_LABEL "ef-status-label"

/* -------------------------------------------------------------------------
 * Menu bar construction
 *
 * GTK4 uses GMenuModel (GMenu/GMenuItem) rather than legacy GtkMenuBar.
 * The menu model is attached to the window via gtk_application_window_set_show_menubar.
 * ---------------------------------------------------------------------- */
static GMenuModel *
build_menu_model(void)
{
    GMenu *menu_bar = g_menu_new();

    /* File menu */
    GMenu *file_menu = g_menu_new();
    g_menu_append(file_menu, "New Project",  "win.new-project");
    g_menu_append(file_menu, "Open Project", "win.open-project");
    g_menu_append(file_menu, "Save",         "win.save");
    g_menu_append(file_menu, "Quit",         "app.quit");
    g_menu_append_submenu(menu_bar, "File", G_MENU_MODEL(file_menu));
    g_object_unref(file_menu);

    /* Edit menu */
    GMenu *edit_menu = g_menu_new();
    g_menu_append(edit_menu, "Undo",       "win.undo");
    g_menu_append(edit_menu, "Redo",       "win.redo");
    g_menu_append(edit_menu, "Preferences","win.preferences");
    g_menu_append_submenu(menu_bar, "Edit", G_MENU_MODEL(edit_menu));
    g_object_unref(edit_menu);

    /* View menu */
    GMenu *view_menu = g_menu_new();
    g_menu_append(view_menu, "Toggle Left Panel",   "win.toggle-left");
    g_menu_append(view_menu, "Toggle Right Panel",  "win.toggle-right");
    g_menu_append_submenu(menu_bar, "View", G_MENU_MODEL(view_menu));
    g_object_unref(view_menu);

    /* Tools menu */
    GMenu *tools_menu = g_menu_new();
    g_menu_append(tools_menu, "OpenSCAD",  "win.tool-openscad");
    g_menu_append(tools_menu, "KiCad",     "win.tool-kicad");
    g_menu_append_submenu(menu_bar, "Tools", G_MENU_MODEL(tools_menu));
    g_object_unref(tools_menu);

    /* Help menu */
    GMenu *help_menu = g_menu_new();
    g_menu_append(help_menu, "About",         "win.about");
    g_menu_append(help_menu, "Documentation", "win.documentation");
    g_menu_append_submenu(menu_bar, "Help", G_MENU_MODEL(help_menu));
    g_object_unref(help_menu);

    return G_MENU_MODEL(menu_bar);
}

/* -------------------------------------------------------------------------
 * Placeholder panel content helpers
 * ---------------------------------------------------------------------- */
static GtkWidget *
make_placeholder_panel(const char *label_text)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *label = gtk_label_new(label_text);
    gtk_widget_set_opacity(label, 0.35);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_vexpand(label, TRUE);

    gtk_box_append(GTK_BOX(box), label);
    return box;
}

/* -------------------------------------------------------------------------
 * ef_app_window_create
 * ---------------------------------------------------------------------- */
GtkWidget *
ef_app_window_create(GtkApplication *app)
{
    /* --- Root window --- */
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "ElectroForge IDE");
    gtk_window_set_default_size(GTK_WINDOW(window), 1400, 900);

    /* Enable the application menu bar */
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(window), TRUE);

    /* Attach the GMenuModel to the application */
    GMenuModel *menu_model = build_menu_model();
    gtk_application_set_menubar(app, menu_model);
    g_object_unref(menu_model);

    /* --- Header bar --- */
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);

    /* Title widget: show app name + project name */
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *title_label = gtk_label_new("ElectroForge IDE");
    {
        /* Make the title label bold via markup */
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes(GTK_LABEL(title_label), attrs);
        pango_attr_list_unref(attrs);
    }

    GtkWidget *project_label = gtk_label_new("No Project");
    {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
        gtk_label_set_attributes(GTK_LABEL(project_label), attrs);
        pango_attr_list_unref(attrs);
    }
    gtk_widget_set_opacity(project_label, 0.6);

    gtk_box_append(GTK_BOX(title_box), title_label);
    gtk_box_append(GTK_BOX(title_box), project_label);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_box);

    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    /* Store the project label so we can update it later */
    g_object_set_data(G_OBJECT(window), EF_KEY_HEADER_BAR,
                      (gpointer)project_label);

    /* --- Outer vertical box: content area + status bar --- */
    GtkWidget *outer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), outer_box);

    /* --- Three-pane layout using nested GtkPaned --- */
    /*
     * Structure:
     *   outer_paned (horizontal)
     *     ├─ left panel  (placeholder — future: file/component tree)
     *     └─ right_paned (horizontal)
     *         ├─ center panel (placeholder — future: main editor/canvas)
     *         └─ right panel  (placeholder — future: properties/inspector)
     */
    GtkWidget *outer_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *right_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

    GtkWidget *left_panel   = make_placeholder_panel("Left Panel\n(Component Tree)");
    GtkWidget *center_panel = make_placeholder_panel("Center Panel\n(Editor / Canvas)");
    GtkWidget *right_panel  = make_placeholder_panel("Right Panel\n(Inspector / Properties)");

    /* Wrap panels in scrolled windows for future-proofing */
    GtkWidget *left_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(left_scroll), left_panel);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(left_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *center_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(center_scroll), center_panel);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(center_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *right_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(right_scroll), right_panel);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(right_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    /* Assemble panes */
    gtk_paned_set_start_child(GTK_PANED(right_paned), center_scroll);
    gtk_paned_set_end_child(GTK_PANED(right_paned), right_scroll);
    gtk_paned_set_position(GTK_PANED(right_paned), 900);
    gtk_paned_set_resize_start_child(GTK_PANED(right_paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(right_paned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(right_paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(right_paned), FALSE);

    gtk_paned_set_start_child(GTK_PANED(outer_paned), left_scroll);
    gtk_paned_set_end_child(GTK_PANED(outer_paned), right_paned);
    gtk_paned_set_position(GTK_PANED(outer_paned), 240);
    gtk_paned_set_resize_start_child(GTK_PANED(outer_paned), FALSE);
    gtk_paned_set_resize_end_child(GTK_PANED(outer_paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(outer_paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(outer_paned), FALSE);

    gtk_widget_set_vexpand(outer_paned, TRUE);
    gtk_widget_set_hexpand(outer_paned, TRUE);
    gtk_box_append(GTK_BOX(outer_box), outer_paned);

    /* --- Status bar --- */
    GtkWidget *status_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(status_frame, "statusbar");

    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(status_box, 6);
    gtk_widget_set_margin_end(status_box, 6);
    gtk_widget_set_margin_top(status_box, 2);
    gtk_widget_set_margin_bottom(status_box, 2);

    GtkWidget *status_label = gtk_label_new("Ready");
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(status_label, TRUE);
    {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
        gtk_label_set_attributes(GTK_LABEL(status_label), attrs);
        pango_attr_list_unref(attrs);
    }

    gtk_box_append(GTK_BOX(status_box), status_label);
    gtk_frame_set_child(GTK_FRAME(status_frame), status_box);
    gtk_box_append(GTK_BOX(outer_box), status_frame);

    /* Store the status label for later updates */
    g_object_set_data(G_OBJECT(window), EF_KEY_STATUS_LABEL,
                      (gpointer)status_label);

    ef_log(EF_LOG_INFO, EF_LOG_EVENT_APP, "application window created");

    return window;
}

/* -------------------------------------------------------------------------
 * ef_app_window_set_project_name
 * ---------------------------------------------------------------------- */
void
ef_app_window_set_project_name(GtkWidget *window, const char *project_name)
{
    if (!window) return;

    GtkWidget *label = g_object_get_data(G_OBJECT(window), EF_KEY_HEADER_BAR);
    if (!label) return;

    const char *display = (project_name && project_name[0] != '\0')
                          ? project_name
                          : "No Project";
    gtk_label_set_text(GTK_LABEL(label), display);
}

/* -------------------------------------------------------------------------
 * ef_app_window_set_status
 * ---------------------------------------------------------------------- */
void
ef_app_window_set_status(GtkWidget *window, const char *text)
{
    if (!window) return;

    GtkWidget *label = g_object_get_data(G_OBJECT(window), EF_KEY_STATUS_LABEL);
    if (!label) return;

    gtk_label_set_text(GTK_LABEL(label), text ? text : "");
}
