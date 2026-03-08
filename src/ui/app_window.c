#include "app_window.h"
#include "ui/code_editor.h"
#include "ui/scad_preview.h"
#include "ui/transform_panel.h"
#include "gl/gl_viewport.h"
#include "bezier/bezier_editor.h"
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
#define DC_KEY_HEADER_BAR  "dc-header-bar"
#define DC_KEY_STATUS_LABEL "dc-status-label"

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
 * Pick callback — viewport object click → code editor + transform panel
 * ---------------------------------------------------------------------- */
typedef struct {
    DC_CodeEditor     *code_ed;
    DC_TransformPanel *transform;
} PickCtx;

static void
on_object_picked(int obj_idx, int line_start, int line_end, void *userdata)
{
    PickCtx *ctx = userdata;
    if (obj_idx >= 0 && ctx->code_ed) {
        dc_code_editor_select_lines(ctx->code_ed, line_start, line_end);

        /* Get the statement text from the code editor to populate transform panel */
        char *full = dc_code_editor_get_text(ctx->code_ed);
        if (full && ctx->transform) {
            /* Extract lines line_start..line_end from full text */
            int line = 1;
            const char *p = full;
            const char *start_ptr = NULL;
            const char *end_ptr = NULL;

            while (*p) {
                if (line == line_start && !start_ptr) start_ptr = p;
                if (*p == '\n') {
                    if (line == line_end) { end_ptr = p; break; }
                    line++;
                }
                p++;
            }
            if (!start_ptr) start_ptr = full;
            if (!end_ptr) end_ptr = full + strlen(full);

            size_t len = (size_t)(end_ptr - start_ptr);
            char *stmt = malloc(len + 1);
            if (stmt) {
                memcpy(stmt, start_ptr, len);
                stmt[len] = '\0';
                dc_transform_panel_show(ctx->transform, stmt,
                                         line_start, line_end);
                free(stmt);
            }
            free(full);
        }
    } else if (ctx->transform) {
        dc_transform_panel_hide(ctx->transform);
    }
}

/* -------------------------------------------------------------------------
 * Key handler for window-level shortcuts
 * ---------------------------------------------------------------------- */
static gboolean
on_key_pressed(GtkEventControllerKey *ctrl, guint keyval,
               guint keycode, GdkModifierType mods, gpointer data)
{
    (void)ctrl; (void)keycode; (void)mods;
    GtkWidget *window = data;

    if (keyval == GDK_KEY_F5) {
        DC_ScadPreview *pv = g_object_get_data(G_OBJECT(window),
                                                "dc-scad-preview-ref");
        if (pv) dc_scad_preview_render(pv);
        return TRUE;
    }

    return FALSE;
}

/* -------------------------------------------------------------------------
 * dc_app_window_create
 * ---------------------------------------------------------------------- */
GtkWidget *
dc_app_window_create(GtkApplication *app)
{
    /* --- Root window --- */
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "DunCAD");
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

    GtkWidget *title_label = gtk_label_new("DunCAD");
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
    g_object_set_data(G_OBJECT(window), DC_KEY_HEADER_BAR,
                      (gpointer)project_label);

    /* --- Outer vertical box: content area + status bar --- */
    GtkWidget *outer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), outer_box);

    /* --- Three-pane layout using nested GtkPaned --- */
    /*
     * Structure:
     *   outer_paned (horizontal)
     *     ├─ left: code editor (~400px)
     *     └─ inner_paned (horizontal)
     *         ├─ center: OpenSCAD 3D preview (flexible)
     *         └─ right_paned (vertical, ~400px)
     *             ├─ top: bezier curve editor (square-ish)
     *             └─ bottom: placeholder (future properties)
     */

    /* Create the panels */
    DC_CodeEditor *code_ed = dc_code_editor_new();
    GtkWidget *left_panel = dc_code_editor_widget(code_ed);

    DC_ScadPreview *preview = dc_scad_preview_new();
    dc_scad_preview_set_code_editor(preview, code_ed);
    GtkWidget *center_panel = dc_scad_preview_widget(preview);

    DC_BezierEditor *editor = dc_bezier_editor_new();
    GtkWidget *bezier_widget = dc_bezier_editor_widget(editor);

    GtkWidget *bottom_placeholder = make_placeholder_panel(
        "Properties\n(Future)");

    /* Right pane: vertical split — bezier (top) + placeholder (bottom) */
    GtkWidget *right_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_start_child(GTK_PANED(right_paned), bezier_widget);
    gtk_paned_set_end_child(GTK_PANED(right_paned), bottom_placeholder);
    gtk_paned_set_position(GTK_PANED(right_paned), 400);
    gtk_paned_set_resize_start_child(GTK_PANED(right_paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(right_paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(right_paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(right_paned), FALSE);

    /* Inner pane: center preview + right panel */
    GtkWidget *inner_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(inner_paned), center_panel);
    gtk_paned_set_end_child(GTK_PANED(inner_paned), right_paned);
    gtk_paned_set_position(GTK_PANED(inner_paned), 600);
    gtk_paned_set_resize_start_child(GTK_PANED(inner_paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(inner_paned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(inner_paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(inner_paned), FALSE);

    /* Outer pane: left code editor + inner */
    GtkWidget *outer_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(outer_paned), left_panel);
    gtk_paned_set_end_child(GTK_PANED(outer_paned), inner_paned);
    gtk_paned_set_position(GTK_PANED(outer_paned), 400);
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
    g_object_set_data(G_OBJECT(window), DC_KEY_STATUS_LABEL,
                      (gpointer)status_label);

    /* Wire the editor to the window for status updates and
     * automatic cleanup via destroy-notify. */
    dc_bezier_editor_set_window(editor, window);

    /* Connect bezier editor to code editor for Insert SCAD */
    dc_bezier_editor_set_code_editor(editor, code_ed);

    /* Wire the code editor to the window */
    dc_code_editor_set_window(code_ed, window);
    g_object_set_data_full(G_OBJECT(window), "dc-code-editor", code_ed,
                           (GDestroyNotify)dc_code_editor_free);

    /* Wire the SCAD preview to the window */
    g_object_set_data_full(G_OBJECT(window), "dc-scad-preview", preview,
                           (GDestroyNotify)dc_scad_preview_free);

    /* Wire pick callback: viewport click → code editor + transform panel */
    DC_GlViewport *gl_vp = dc_scad_preview_get_viewport(preview);
    if (gl_vp) {
        /* PickCtx lives as long as the window (freed via destroy-notify) */
        PickCtx *pick_ctx = malloc(sizeof(PickCtx));
        pick_ctx->code_ed = code_ed;
        pick_ctx->transform = dc_scad_preview_get_transform(preview);
        dc_gl_viewport_set_pick_callback(gl_vp, on_object_picked, pick_ctx);
        g_object_set_data_full(G_OBJECT(window), "dc-pick-ctx", pick_ctx, free);
    }

    /* F5 = Render preview (window-level shortcut) */
    g_object_set_data(G_OBJECT(window), "dc-scad-preview-ref", preview);
    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_key_pressed), window);
    gtk_widget_add_controller(window, key_ctrl);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "application window created");

    return window;
}

/* -------------------------------------------------------------------------
 * dc_app_window_set_project_name
 * ---------------------------------------------------------------------- */
void
dc_app_window_set_project_name(GtkWidget *window, const char *project_name)
{
    if (!window) return;

    GtkWidget *label = g_object_get_data(G_OBJECT(window), DC_KEY_HEADER_BAR);
    if (!label) return;

    const char *display = (project_name && project_name[0] != '\0')
                          ? project_name
                          : "No Project";
    gtk_label_set_text(GTK_LABEL(label), display);
}

/* -------------------------------------------------------------------------
 * dc_app_window_set_status
 * ---------------------------------------------------------------------- */
void
dc_app_window_set_status(GtkWidget *window, const char *text)
{
    if (!window) return;

    GtkWidget *label = g_object_get_data(G_OBJECT(window), DC_KEY_STATUS_LABEL);
    if (!label) return;

    gtk_label_set_text(GTK_LABEL(label), text ? text : "");
}

struct DC_BezierEditor *
dc_app_window_get_editor(GtkWidget *window)
{
    if (!window) return NULL;
    return g_object_get_data(G_OBJECT(window), "dc-bezier-editor");
}

struct DC_CodeEditor *
dc_app_window_get_code_editor(GtkWidget *window)
{
    if (!window) return NULL;
    return g_object_get_data(G_OBJECT(window), "dc-code-editor");
}

struct DC_ScadPreview *
dc_app_window_get_scad_preview(GtkWidget *window)
{
    if (!window) return NULL;
    return g_object_get_data(G_OBJECT(window), "dc-scad-preview-ref");
}
