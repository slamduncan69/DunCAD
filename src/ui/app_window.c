#define _POSIX_C_SOURCE 200809L

#include "app_window.h"
#include "ui/code_editor.h"
#include "ui/scad_preview.h"
#include "ui/transform_panel.h"
#include "ui/terminal_panel.h"
#include "ui/ai_chat.h"
#include "ui/shape_menu.h"
#include "ui/eda_view.h"
#include "eda_ui/sch_editor.h"
#include "eda_ui/sch_canvas.h"
#include "eda_ui/pcb_editor.h"
#include "eda_ui/pcb_canvas.h"
#include "eda_ui/eda_library_browser.h"
#include "eda/eda_library.h"
#include "eda/eda_schematic.h"
#include "eda/eda_pcb.h"
#include "gl/gl_viewport.h"
#include "bezier/bezier_editor.h"
#include "inspect/inspect.h"
#include "core/log.h"

#include <math.h>
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
/* The Insert menu is a mutable GMenu that gets swapped when tabs change.
 * It's stored on the window as "dc-insert-menu". */

static void
populate_insert_menu_3d(GMenu *menu)
{
    g_menu_remove_all(menu);
    g_menu_append(menu, "Cube",         "shape.insert-cube");
    g_menu_append(menu, "Sphere",       "shape.insert-sphere");
    g_menu_append(menu, "Cylinder",     "shape.insert-cylinder");
    g_menu_append(menu, "Tetrahedron",  "shape.insert-tetrahedron");
    g_menu_append(menu, "Octahedron",   "shape.insert-octahedron");
    g_menu_append(menu, "Dodecahedron", "shape.insert-dodecahedron");
    g_menu_append(menu, "Icosahedron",  "shape.insert-icosahedron");
}

static void
populate_insert_menu_eda(GMenu *menu)
{
    g_menu_remove_all(menu);
    g_menu_append(menu, "Add Symbol...",   "eda.add-symbol");
    g_menu_append(menu, "Add Wire",        "eda.add-wire");
    g_menu_append(menu, "Add Label",       "eda.add-label");
    g_menu_append(menu, "Add Power Port",  "eda.add-power");
    g_menu_append(menu, "Route Track",     "eda.route-track");
    g_menu_append(menu, "Add Via",         "eda.add-via");
    g_menu_append(menu, "Add Footprint...", "eda.add-footprint");
}

static GMenuModel *
build_menu_model(GMenu **out_insert_menu)
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

    /* Insert menu — mutable, swapped on tab change */
    GMenu *insert_menu = g_menu_new();
    populate_insert_menu_3d(insert_menu);
    g_menu_append_submenu(menu_bar, "Insert", G_MENU_MODEL(insert_menu));
    if (out_insert_menu)
        *out_insert_menu = insert_menu; /* keep alive — stored on window */
    else
        g_object_unref(insert_menu);

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

static DC_ELibrary *ensure_library_loaded(void);

/* -------------------------------------------------------------------------
 * Tab change handler — swap Insert menu contents
 * ---------------------------------------------------------------------- */
static void
on_tab_changed(GtkStack *stack, GParamSpec *pspec, gpointer userdata)
{
    (void)pspec;
    GtkWidget *window = userdata;
    GMenu *insert_menu = g_object_get_data(G_OBJECT(window), "dc-insert-menu");
    if (!insert_menu) return;

    const char *tab = gtk_stack_get_visible_child_name(stack);
    if (tab && strcmp(tab, "eda") == 0) {
        populate_insert_menu_eda(insert_menu);
        /* Ensure library is loaded and set on editors */
        DC_ELibrary *lib = ensure_library_loaded();
        DC_EdaView *ev = dc_app_window_get_eda_view(window);
        if (lib && ev) {
            dc_sch_editor_set_library(dc_eda_view_get_sch_editor(ev), lib);
            dc_pcb_editor_set_library(dc_eda_view_get_pcb_editor(ev), lib);
        }
    } else {
        populate_insert_menu_3d(insert_menu);
    }
}

/* -------------------------------------------------------------------------
 * EDA Insert action handlers
 * ---------------------------------------------------------------------- */
static DC_ELibrary *s_eda_lib = NULL; /* global library cache — lazy loaded */

static DC_ELibrary *
ensure_library_loaded(void)
{
    if (s_eda_lib) return s_eda_lib;

    s_eda_lib = dc_elibrary_new();
    if (!s_eda_lib) return NULL;

    /* Register ALL KiCad symbol libraries (lazy — no parsing yet).
     * Libraries are parsed on demand when accessed in the browser. */
    const char *lib_dir = "/usr/share/kicad/symbols/";
    GDir *dir = g_dir_open(lib_dir, 0, NULL);
    if (dir) {
        const gchar *name;
        int count = 0;
        while ((name = g_dir_read_name(dir)) != NULL) {
            if (!g_str_has_suffix(name, ".kicad_sym")) continue;
            char path[512];
            snprintf(path, sizeof(path), "%s%s", lib_dir, name);
            if (dc_elibrary_register_symbols(s_eda_lib, path) == 0)
                count++;
        }
        g_dir_close(dir);
        dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA,
               "Registered %d symbol libraries (lazy loading)",
               count);
    } else {
        dc_log(DC_LOG_WARN, DC_LOG_EVENT_EDA,
               "Could not open KiCad symbols directory: %s", lib_dir);
    }

    return s_eda_lib;
}

/* Auto-generate next reference designator like R1, R2, C1, C2... */
static int s_ref_counters[256]; /* indexed by first char */
static char *
next_reference(const char *lib_id)
{
    char prefix = 'U';
    if (lib_id) {
        if (strstr(lib_id, ":R") || strstr(lib_id, "Resistor"))  prefix = 'R';
        else if (strstr(lib_id, ":C") || strstr(lib_id, "Capacitor")) prefix = 'C';
        else if (strstr(lib_id, ":L") || strstr(lib_id, "Inductor")) prefix = 'L';
        else if (strstr(lib_id, ":D") || strstr(lib_id, "Diode") || strstr(lib_id, "LED")) prefix = 'D';
        else if (strstr(lib_id, ":Q") || strstr(lib_id, "Transistor")) prefix = 'Q';
        else if (strstr(lib_id, "Connector")) prefix = 'J';
    }
    int idx = (unsigned char)prefix;
    s_ref_counters[idx]++;
    char buf[32];
    snprintf(buf, sizeof(buf), "%c%d", prefix, s_ref_counters[idx]);
    return strdup(buf);
}

static void on_eda_add_symbol(GSimpleAction *action, GVariant *param, gpointer userdata);

/* Called when the Sym toolbar button is clicked in the schematic editor */
static void
on_sch_place_request(DC_SchEditMode mode, void *userdata)
{
    GtkWidget *window = userdata;
    if (mode == DC_SCH_MODE_PLACE_SYMBOL) {
        on_eda_add_symbol(NULL, NULL, window);
    }
}

static void
on_eda_add_symbol(GSimpleAction *action, GVariant *param, gpointer userdata)
{
    (void)action; (void)param;
    GtkWidget *window = userdata;
    DC_EdaView *ev = dc_app_window_get_eda_view(window);
    if (!ev) return;

    DC_ELibrary *lib = ensure_library_loaded();
    if (!lib || dc_elibrary_symbol_count(lib) == 0) {
        dc_log(DC_LOG_WARN, DC_LOG_EVENT_EDA, "No symbols loaded in library");
        return;
    }

    /* Set library on editor for rendering */
    dc_sch_editor_set_library(dc_eda_view_get_sch_editor(ev), lib);

    char *lib_id = dc_eda_library_browser_run(GTK_WINDOW(window), lib, "symbol");
    if (!lib_id) return;

    DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
    DC_ESchematic *sch = dc_sch_editor_get_schematic(ed);
    char *ref = next_reference(lib_id);

    /* Place at center of current view */
    DC_SchCanvas *canvas = dc_sch_editor_get_canvas(ed);
    double px, py;
    dc_sch_canvas_get_pan(canvas, &px, &py);
    dc_eschematic_add_symbol(sch, lib_id, ref, px, py);
    dc_sch_canvas_queue_redraw(canvas);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA,
           "Added symbol %s as %s", lib_id, ref);
    free(ref);
    free(lib_id);
}

static void
on_eda_add_wire(GSimpleAction *action, GVariant *param, gpointer userdata)
{
    (void)action; (void)param;
    GtkWidget *window = userdata;
    DC_EdaView *ev = dc_app_window_get_eda_view(window);
    if (!ev) return;
    dc_sch_editor_set_mode(dc_eda_view_get_sch_editor(ev), DC_SCH_MODE_WIRE);
}

static void
on_eda_add_label(GSimpleAction *action, GVariant *param, gpointer userdata)
{
    (void)action; (void)param;
    GtkWidget *window = userdata;
    DC_EdaView *ev = dc_app_window_get_eda_view(window);
    if (!ev) return;
    dc_sch_editor_set_mode(dc_eda_view_get_sch_editor(ev), DC_SCH_MODE_PLACE_LABEL);
}

static void
on_eda_add_power(GSimpleAction *action, GVariant *param, gpointer userdata)
{
    (void)action; (void)param;
    GtkWidget *window = userdata;
    DC_EdaView *ev = dc_app_window_get_eda_view(window);
    if (!ev) return;
    /* For now, place a VCC power port at view center */
    DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
    DC_ESchematic *sch = dc_sch_editor_get_schematic(ed);
    DC_SchCanvas *canvas = dc_sch_editor_get_canvas(ed);
    double px, py;
    dc_sch_canvas_get_pan(canvas, &px, &py);
    dc_eschematic_add_power_port(sch, "VCC", px, py);
    dc_sch_canvas_queue_redraw(canvas);
}

static void
on_eda_route_track(GSimpleAction *action, GVariant *param, gpointer userdata)
{
    (void)action; (void)param;
    GtkWidget *window = userdata;
    DC_EdaView *ev = dc_app_window_get_eda_view(window);
    if (!ev) return;
    dc_pcb_editor_set_mode(dc_eda_view_get_pcb_editor(ev), DC_PCB_MODE_ROUTE);
}

static void
on_eda_add_via(GSimpleAction *action, GVariant *param, gpointer userdata)
{
    (void)action; (void)param;
    GtkWidget *window = userdata;
    DC_EdaView *ev = dc_app_window_get_eda_view(window);
    if (!ev) return;
    dc_pcb_editor_set_mode(dc_eda_view_get_pcb_editor(ev), DC_PCB_MODE_PLACE_VIA);
}

static void
on_eda_add_footprint(GSimpleAction *action, GVariant *param, gpointer userdata)
{
    (void)action; (void)param;
    GtkWidget *window = userdata;
    DC_EdaView *ev = dc_app_window_get_eda_view(window);
    if (!ev) return;
    dc_pcb_editor_set_mode(dc_eda_view_get_pcb_editor(ev), DC_PCB_MODE_PLACE_FOOTPRINT);
}

static void
install_eda_actions(GtkWidget *window)
{
    GSimpleActionGroup *group = g_simple_action_group_new();

    struct { const char *name; GCallback cb; } actions[] = {
        { "add-symbol",    G_CALLBACK(on_eda_add_symbol) },
        { "add-wire",      G_CALLBACK(on_eda_add_wire) },
        { "add-label",     G_CALLBACK(on_eda_add_label) },
        { "add-power",     G_CALLBACK(on_eda_add_power) },
        { "route-track",   G_CALLBACK(on_eda_route_track) },
        { "add-via",       G_CALLBACK(on_eda_add_via) },
        { "add-footprint", G_CALLBACK(on_eda_add_footprint) },
    };
    for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); i++) {
        GSimpleAction *a = g_simple_action_new(actions[i].name, NULL);
        g_signal_connect(a, "activate", actions[i].cb, window);
        g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));
        g_object_unref(a);
    }

    gtk_widget_insert_action_group(window, "eda", G_ACTION_GROUP(group));
    g_object_unref(group);
}


/* -------------------------------------------------------------------------
 * Pick callback — viewport object click → code editor + transform panel
 * ---------------------------------------------------------------------- */
typedef struct {
    DC_CodeEditor     *code_ed;
    DC_TransformPanel *transform;
    DC_GlViewport     *gl_vp;       /* for live translate during drag */
    DC_ScadPreview    *preview;     /* for auto-render on drag end */
    double             move_init[3]; /* initial translate at drag start */
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
 * Code cursor → GL selection (reverse of pick → select_lines)
 * ---------------------------------------------------------------------- */
static void
on_cursor_changed(GtkTextBuffer *buffer, GtkTextIter *location,
                  GtkTextMark *mark, gpointer data)
{
    PickCtx *ctx = data;
    if (!ctx->gl_vp) return;

    /* Only react to the insert mark (cursor), not selection-bound etc. */
    if (mark != gtk_text_buffer_get_insert(buffer)) return;

    /* Don't fight with GL-initiated selections (check if GL area has focus).
     * dc_gl_viewport_widget() returns an overlay; check child focus. */
    GtkWidget *gl_widget = dc_gl_viewport_widget(ctx->gl_vp);
    if (gl_widget) {
        GtkWidget *focus_child = gtk_widget_get_focus_child(gl_widget);
        if (focus_child && gtk_widget_has_focus(focus_child)) return;
    }

    int cursor_line = gtk_text_iter_get_line(location) + 1; /* 1-based */

    /* Find which object covers this line */
    int count = dc_gl_viewport_get_object_count(ctx->gl_vp);
    int best = -1;
    for (int i = 0; i < count; i++) {
        int ls, le;
        if (dc_gl_viewport_get_object_lines(ctx->gl_vp, i, &ls, &le) == 0) {
            if (cursor_line >= ls && cursor_line <= le) {
                best = i;
                break;
            }
        }
    }

    /* Select the object (or deselect if cursor is outside all objects).
     * Use select_object_quiet to avoid feedback loop (no pick callback). */
    int current = dc_gl_viewport_get_selected(ctx->gl_vp);
    if (best != current) {
        dc_gl_viewport_select_object_quiet(ctx->gl_vp, best);

        /* Update transform panel */
        if (best >= 0 && ctx->transform) {
            int ls, le;
            dc_gl_viewport_get_object_lines(ctx->gl_vp, best, &ls, &le);
            char *full = dc_code_editor_get_text(ctx->code_ed);
            if (full) {
                int line = 1;
                const char *p = full;
                const char *sp = NULL, *ep = NULL;
                while (*p) {
                    if (line == ls && !sp) sp = p;
                    if (*p == '\n') {
                        if (line == le) { ep = p; break; }
                        line++;
                    }
                    p++;
                }
                if (!sp) sp = full;
                if (!ep) ep = full + strlen(full);
                size_t len = (size_t)(ep - sp);
                char *stmt = malloc(len + 1);
                if (stmt) {
                    memcpy(stmt, sp, len);
                    stmt[len] = '\0';
                    dc_transform_panel_show(ctx->transform, stmt, ls, le);
                    free(stmt);
                }
                free(full);
            }
        } else if (best < 0 && ctx->transform) {
            dc_transform_panel_hide(ctx->transform);
        }
    }
}

/* -------------------------------------------------------------------------
 * Move callback — viewport object drag → live translate update
 * ---------------------------------------------------------------------- */
static void
on_object_moved(int obj_idx, int phase, float dx, float dy, float dz,
                void *userdata)
{
    PickCtx *ctx = userdata;

    if (phase == 0) {
        /* Drag start: save initial translate values */
        if (ctx->transform)
            dc_transform_panel_get_translate(ctx->transform,
                &ctx->move_init[0], &ctx->move_init[1], &ctx->move_init[2]);
        fprintf(stderr, "MOVE phase 0: init=[%.2f, %.2f, %.2f]\n",
                ctx->move_init[0], ctx->move_init[1], ctx->move_init[2]);
        return;
    }

    if (phase == 1) {
        /* Moving: visual-only GL translate offset (no code changes mid-drag).
         * Delta only — mesh already has original translate baked in. */
        if (ctx->gl_vp)
            dc_gl_viewport_set_object_translate(ctx->gl_vp, obj_idx,
                                                 dx, dy, dz);

        /* Live preview: convert GL delta to SCAD-space and show in panel+terminal */
        float scad_dx =  dx;
        float scad_dy = -dz;
        float scad_dz =  dy;
        double fx = ctx->move_init[0] + (double)scad_dx;
        double fy = ctx->move_init[1] + (double)scad_dy;
        double fz = ctx->move_init[2] + (double)scad_dz;

        if (ctx->transform)
            dc_transform_panel_set_translate_preview(ctx->transform, fx, fy, fz);

        fprintf(stderr, "MOVE live: translate([%.2f, %.2f, %.2f])\n", fx, fy, fz);
        return;
    }

    if (phase == 2) {
        /* Convert GL-space delta (Y-up) to SCAD-space (Z-up).
         * STL loader does: GL_x=SCAD_x, GL_y=SCAD_z, GL_z=-SCAD_y
         * Inverse:         SCAD_x=GL_x, SCAD_y=-GL_z, SCAD_z=GL_y */
        float scad_dx =  dx;
        float scad_dy = -dz;
        float scad_dz =  dy;

        fprintf(stderr, "MOVE phase 2: gl_delta=[%.2f, %.2f, %.2f] scad_delta=[%.2f, %.2f, %.2f] init=[%.2f, %.2f, %.2f]\n",
                (double)dx, (double)dy, (double)dz,
                (double)scad_dx, (double)scad_dy, (double)scad_dz,
                ctx->move_init[0], ctx->move_init[1], ctx->move_init[2]);
        fprintf(stderr, "MOVE phase 2: final=[%.2f, %.2f, %.2f]\n",
                ctx->move_init[0] + (double)scad_dx,
                ctx->move_init[1] + (double)scad_dy,
                ctx->move_init[2] + (double)scad_dz);

        /* Drag end: snap back if dropped close to origin */
        float mag = sqrtf(dx * dx + dy * dy + dz * dz);
        float snap_thresh = 1.0f; /* world units */

        if (mag < snap_thresh) {
            DC_LOG_INFO_APP("move phase 2: snap back (mag=%.2f)", (double)mag);
            /* Snap back — reset GL offset, no code change, no re-render */
            if (ctx->gl_vp)
                dc_gl_viewport_set_object_translate(ctx->gl_vp, obj_idx,
                                                     0.0f, 0.0f, 0.0f);
            return;
        }

        /* Commit final position to code (SCAD-space), then re-render */
        if (ctx->transform)
            dc_transform_panel_set_translate(ctx->transform,
                ctx->move_init[0] + (double)scad_dx,
                ctx->move_init[1] + (double)scad_dy,
                ctx->move_init[2] + (double)scad_dz);
        /* Keep GL offset until re-render replaces objects */
        if (ctx->preview)
            dc_scad_preview_render(ctx->preview);
    }
}

/* -------------------------------------------------------------------------
 * Transform panel Enter callback — triggers preview render
 * ---------------------------------------------------------------------- */
static void
on_transform_enter(void *userdata)
{
    DC_ScadPreview *pv = userdata;
    if (pv) dc_scad_preview_render(pv);
}

/* -------------------------------------------------------------------------
 * Terminal command routing: /cmd → inspect, else → AI chat
 * ---------------------------------------------------------------------- */
typedef struct {
    DC_TerminalPanel *term;
    DC_AiChat        *ai;
    DC_GlViewport    *gl_vp;
    DC_CodeEditor    *code_ed;
    GtkWidget        *status_label;
} TermCtx;

static void
ai_lock_ui(TermCtx *ctx, int lock)
{
    if (ctx->gl_vp)
        dc_gl_viewport_set_locked(ctx->gl_vp, lock);
    if (ctx->status_label) {
        gtk_label_set_text(GTK_LABEL(ctx->status_label),
                           lock ? "AI working — interaction locked"
                                : "Ready");
    }
}

static void
on_ai_done(void *userdata)
{
    TermCtx *ctx = userdata;
    ai_lock_ui(ctx, 0);
}

static void
on_ai_response(const char *text, void *userdata)
{
    DC_TerminalPanel *term = userdata;
    dc_terminal_panel_append(term, text);
}

static void
on_ai_tool(const char *tool, const char *input, const char *result,
           void *userdata)
{
    (void)result;
    DC_TerminalPanel *term = userdata;
    char buf[256];
    /* Show tool name and abbreviated input */
    size_t ilen = strlen(input);
    if (ilen > 80) {
        snprintf(buf, sizeof(buf), "[%s] %.77s...\n", tool, input);
    } else {
        snprintf(buf, sizeof(buf), "[%s] %s\n", tool, input);
    }
    dc_terminal_panel_append(term, buf);
}

/* ---- Lang mode changed: update AI chat ---- */
static void
on_lang_mode_changed(int cubeiform, void *userdata)
{
    DC_AiChat *ai = userdata;
    if (ai) dc_ai_chat_set_cubeiform(ai, cubeiform);
}

static void
on_terminal_command(const char *command, void *userdata)
{
    TermCtx *ctx = userdata;

    if (command[0] == '/') {
        /* Direct inspect command (strip leading /) */
        char *response = dc_inspect_dispatch(command + 1);
        if (response) {
            dc_terminal_panel_append(ctx->term, response);
            dc_terminal_panel_append(ctx->term, "\n");
            free(response);
        }
    } else if (ctx->ai) {
        if (dc_ai_chat_busy(ctx->ai)) {
            dc_terminal_panel_append(ctx->term, "(busy, please wait...)\n");
            return;
        }
        dc_terminal_panel_append(ctx->term, "...\n");
        ai_lock_ui(ctx, 1);
        dc_ai_chat_send(ctx->ai, command);
    } else {
        /* No AI available — fall back to inspect */
        dc_terminal_panel_append(ctx->term,
            "AI not available (claude CLI not found). "
            "Use /command for inspect commands.\n");
    }
}

/* -------------------------------------------------------------------------
 * Undo/Redo action callbacks
 * ---------------------------------------------------------------------- */
static void
on_undo_activate(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    DC_CodeEditor *ed = data;
    dc_code_editor_undo(ed);
}

static void
on_redo_activate(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    DC_CodeEditor *ed = data;
    dc_code_editor_redo(ed);
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
        if (pv) dc_scad_preview_render_refit(pv);
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

    /* Attach the GMenuModel to the application — Insert menu is mutable */
    GMenu *insert_menu = NULL;
    GMenuModel *menu_model = build_menu_model(&insert_menu);
    gtk_application_set_menubar(app, menu_model);
    g_object_unref(menu_model);

    /* Store the Insert menu for tab-aware swapping */
    g_object_set_data_full(G_OBJECT(window), "dc-insert-menu",
                           insert_menu, g_object_unref);

    /* Install EDA actions on window */
    install_eda_actions(window);

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

    DC_TerminalPanel *terminal = dc_terminal_panel_new();
    GtkWidget *terminal_widget = dc_terminal_panel_widget(terminal);

    /* Right pane: vertical split — bezier (top) + terminal (bottom) */
    GtkWidget *right_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_start_child(GTK_PANED(right_paned), bezier_widget);
    gtk_paned_set_end_child(GTK_PANED(right_paned), terminal_widget);
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

    /* --- Tab system: GtkStack wrapping 3D CAD, EDA, Assembly --- */
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_widget_set_hexpand(stack, TRUE);

    /* Tab 1: 3D CAD (existing layout) */
    gtk_stack_add_titled(GTK_STACK(stack), outer_paned, "3d_cad", "3D CAD");

    /* Tab 2: EDA */
    DC_EdaView *eda_view = dc_eda_view_new();
    if (eda_view) {
        GtkWidget *eda_widget = dc_eda_view_widget(eda_view);
        gtk_stack_add_titled(GTK_STACK(stack), eda_widget, "eda", "EDA");
        g_object_set_data_full(G_OBJECT(window), "dc-eda-view", eda_view,
                               (GDestroyNotify)dc_eda_view_free);

        /* Wire Sym button to open library browser via place callback */
        dc_sch_editor_set_place_callback(
            dc_eda_view_get_sch_editor(eda_view),
            on_sch_place_request, window);
    }

    /* Tab 3: Assembly (placeholder) */
    GtkWidget *asm_placeholder = gtk_label_new("Assembly — coming soon");
    gtk_stack_add_titled(GTK_STACK(stack), asm_placeholder, "assembly", "Assembly");

    /* Stack switcher in header bar */
    GtkWidget *stack_switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(stack_switcher), GTK_STACK(stack));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), stack_switcher);

    g_object_set_data(G_OBJECT(window), "dc-stack", stack);

    /* Connect tab change signal to swap Insert menu */
    g_signal_connect(stack, "notify::visible-child",
                     G_CALLBACK(on_tab_changed), window);

    gtk_box_append(GTK_BOX(outer_box), stack);

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

    /* Wire the terminal panel with AI chat */
    DC_AiChat *ai_chat = dc_ai_chat_new(); /* NULL if no API key */
    if (ai_chat) {
        dc_ai_chat_set_response_callback(ai_chat, on_ai_response, terminal);
        dc_ai_chat_set_tool_callback(ai_chat, on_ai_tool, terminal);
        /* Set initial lang mode from code editor */
        dc_ai_chat_set_cubeiform(ai_chat, dc_code_editor_is_cubeiform(code_ed));
        dc_terminal_panel_append(terminal,
            "AI connected. Type to chat, /command for inspect.\n\n");
        /* Update AI lang mode when editor switches files */
        dc_code_editor_set_lang_changed_callback(code_ed,
                                                  on_lang_mode_changed, ai_chat);
    } else {
        dc_terminal_panel_append(terminal,
            "claude CLI not found. /command for inspect.\n\n");
    }
    TermCtx *term_ctx = malloc(sizeof(TermCtx));
    memset(term_ctx, 0, sizeof(*term_ctx));
    term_ctx->term = terminal;
    term_ctx->ai = ai_chat;
    term_ctx->code_ed = code_ed;
    term_ctx->status_label = status_label;
    dc_terminal_panel_set_command_callback(terminal, on_terminal_command, term_ctx);
    g_object_set_data_full(G_OBJECT(window), "dc-terminal", terminal,
                           (GDestroyNotify)dc_terminal_panel_free);
    g_object_set_data_full(G_OBJECT(window), "dc-ai-chat", ai_chat,
                           (GDestroyNotify)dc_ai_chat_free);
    g_object_set_data_full(G_OBJECT(window), "dc-term-ctx", term_ctx, free);

    /* Wire pick callback: viewport click → code editor + transform panel */
    DC_GlViewport *gl_vp = dc_scad_preview_get_viewport(preview);
    term_ctx->gl_vp = gl_vp;
    if (ai_chat) {
        dc_ai_chat_set_done_callback(ai_chat, on_ai_done, term_ctx);
    }
    if (gl_vp) {
        /* PickCtx lives as long as the window (freed via destroy-notify) */
        PickCtx *pick_ctx = malloc(sizeof(PickCtx));
        memset(pick_ctx, 0, sizeof(*pick_ctx));
        pick_ctx->code_ed = code_ed;
        pick_ctx->transform = dc_scad_preview_get_transform(preview);
        pick_ctx->gl_vp = gl_vp;
        pick_ctx->preview = preview;
        dc_gl_viewport_set_pick_callback(gl_vp, on_object_picked, pick_ctx);
        dc_gl_viewport_set_move_callback(gl_vp, on_object_moved, pick_ctx);
        g_object_set_data_full(G_OBJECT(window), "dc-pick-ctx", pick_ctx, free);

        /* Code cursor → GL selection (reverse direction) */
        GtkTextBuffer *buf = dc_code_editor_get_buffer(code_ed);
        if (buf) {
            g_signal_connect(buf, "mark-set",
                             G_CALLBACK(on_cursor_changed), pick_ctx);
        }

        /* Enter in transform panel entries → trigger render */
        dc_transform_panel_set_enter_callback(
            dc_scad_preview_get_transform(preview),
            on_transform_enter, preview);
    }

    /* Shape context menu: right-click on GL viewport + menu bar "Insert" */
    if (gl_vp) {
        GtkWidget *gl_widget = dc_gl_viewport_widget(gl_vp);
        dc_shape_menu_attach(gl_widget, code_ed, gl_vp,
                             dc_scad_preview_get_transform(preview),
                             preview, editor);

        /* Install shape actions on window so menu bar "Insert" can find them */
        GActionGroup *sg = g_object_get_data(G_OBJECT(gl_widget),
                                              "dc-shape-action-group");
        if (sg) gtk_widget_insert_action_group(window, "shape", sg);
    }

    /* --- Undo/Redo actions --- */
    {
        GSimpleAction *undo_action = g_simple_action_new("undo", NULL);
        g_signal_connect(undo_action, "activate",
                         G_CALLBACK(on_undo_activate), code_ed);
        g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(undo_action));
        g_object_unref(undo_action);

        GSimpleAction *redo_action = g_simple_action_new("redo", NULL);
        g_signal_connect(redo_action, "activate",
                         G_CALLBACK(on_redo_activate), code_ed);
        g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(redo_action));
        g_object_unref(redo_action);
    }

    /* Keyboard shortcuts: Ctrl+Z = undo, Ctrl+Shift+Z = redo */
    gtk_application_set_accels_for_action(app, "win.undo",
        (const char *[]){ "<Control>z", NULL });
    gtk_application_set_accels_for_action(app, "win.redo",
        (const char *[]){ "<Control><Shift>z", NULL });

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

struct DC_EdaView *
dc_app_window_get_eda_view(GtkWidget *window)
{
    if (!window) return NULL;
    return g_object_get_data(G_OBJECT(window), "dc-eda-view");
}

void
dc_app_window_set_tab(GtkWidget *window, const char *tab_name)
{
    if (!window || !tab_name) return;
    GtkWidget *stack = g_object_get_data(G_OBJECT(window), "dc-stack");
    if (stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(stack), tab_name);
    }
}
