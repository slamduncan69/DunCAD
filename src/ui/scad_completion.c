#define _POSIX_C_SOURCE 200809L
#include "ui/scad_completion.h"
#include "core/log.h"

#include <gtksourceview/gtksource.h>
#include <string.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * Custom inline autocompletion for OpenSCAD.
 *
 * Bypasses GtkSourceView's completion system entirely because its popup
 * uses GdkPopup which is broken on Wayland/NVIDIA (gdk_popup_present
 * assertion 'width > 0' after first dismissal — see session s007).
 *
 * Instead, uses a GtkPopover (which is a regular widget, not a Wayland
 * popup) containing a GtkListBox. The popover is anchored to the cursor
 * position in the GtkSourceView and managed by our own key handler.
 *
 * Flow:
 *   1. User types → key-released handler extracts word at cursor
 *   2. Word is matched against keyword database (prefix match)
 *   3. Matches shown in popover with arrow key navigation
 *   4. Tab inserts selected keyword, then shows syntax template
 *   5. In syntax phase, Tab cycles through parameter placeholders
 * ---------------------------------------------------------------------- */

/* ---- OpenSCAD keyword + syntax database ---- */

typedef struct {
    const char *keyword;
    const char *syntax;    /* full syntax template, NULL if none */
} ScadEntry;

static const ScadEntry SCAD_DB[] = {
    /* 3D Primitives */
    { "cube",           "cube([x, y, z], center=true);" },
    { "sphere",         "sphere(r=radius, $fn=segments);" },
    { "cylinder",       "cylinder(h=height, r=radius, $fn=segments, center=true);" },
    { "polyhedron",     "polyhedron(points=[[...]], faces=[[...]]);" },

    /* 2D Primitives */
    { "circle",         "circle(r=radius, $fn=segments);" },
    { "square",         "square([x, y], center=true);" },
    { "polygon",        "polygon(points=[[x,y], ...]);" },
    { "text",           "text(\"string\", size=10, font=\"Liberation Sans\");" },

    /* Transforms */
    { "translate",      "translate([x, y, z])" },
    { "rotate",         "rotate([x, y, z])" },
    { "scale",          "scale([x, y, z])" },
    { "mirror",         "mirror([x, y, z])" },
    { "multmatrix",     "multmatrix(m)" },
    { "resize",         "resize([x, y, z], auto=true)" },
    { "offset",         "offset(r=radius)" },
    { "color",          "color(\"colorname\", alpha)" },

    /* Boolean Operations */
    { "difference",     "difference() {\n\t\n}" },
    { "union",          "union() {\n\t\n}" },
    { "intersection",   "intersection() {\n\t\n}" },

    /* Extrusion */
    { "linear_extrude", "linear_extrude(height=h, center=true, twist=0, slices=1)" },
    { "rotate_extrude", "rotate_extrude(angle=360, $fn=segments)" },

    /* CSG Operations */
    { "hull",           "hull() {\n\t\n}" },
    { "minkowski",      "minkowski() {\n\t\n}" },

    /* Import/Render */
    { "import",         "import(\"filename\");" },
    { "surface",        "surface(\"filename\", center=true);" },
    { "projection",     "projection(cut=false)" },
    { "render",         "render(convexity=10)" },

    /* Control Flow */
    { "module",         "module name(params) {\n\t\n}" },
    { "function",       "function name(params) = expr;" },
    { "for",            "for (i = [start:step:end]) {\n\t\n}" },
    { "if",             "if (condition) {\n\t\n}" },
    { "else",           NULL },
    { "let",            "let (x = value)" },
    { "each",           NULL },
    { "assert",         "assert(condition, \"message\");" },
    { "echo",           "echo(value);" },
    { "include",        "include <filename.scad>" },
    { "use",            "use <filename.scad>" },
    { "children",       "children(index);" },

    /* Math Functions */
    { "abs",            "abs(x)" },
    { "sign",           "sign(x)" },
    { "sin",            "sin(degrees)" },
    { "cos",            "cos(degrees)" },
    { "tan",            "tan(degrees)" },
    { "asin",           "asin(x)" },
    { "acos",           "acos(x)" },
    { "atan",           "atan(x)" },
    { "atan2",          "atan2(y, x)" },
    { "floor",          "floor(x)" },
    { "ceil",           "ceil(x)" },
    { "round",          "round(x)" },
    { "sqrt",           "sqrt(x)" },
    { "pow",            "pow(base, exponent)" },
    { "exp",            "exp(x)" },
    { "log",            "log(x)" },
    { "ln",             "ln(x)" },
    { "min",            "min(a, b)" },
    { "max",            "max(a, b)" },
    { "norm",           "norm(vector)" },
    { "cross",          "cross(a, b)" },

    /* List/String Functions */
    { "len",            "len(value)" },
    { "concat",         "concat(a, b)" },
    { "lookup",         "lookup(key, table)" },
    { "str",            "str(value)" },
    { "chr",            "chr(code)" },
    { "ord",            "ord(char)" },
    { "search",         "search(needle, haystack)" },
    { "is_undef",       "is_undef(x)" },
    { "is_bool",        "is_bool(x)" },
    { "is_num",         "is_num(x)" },
    { "is_string",      "is_string(x)" },
    { "is_list",        "is_list(x)" },
    { "is_function",    "is_function(x)" },

    /* Special Variables */
    { "$fn",            NULL },
    { "$fa",            NULL },
    { "$fs",            NULL },
    { "$t",             NULL },
    { "$vpr",           NULL },
    { "$vpt",           NULL },
    { "$vpd",           NULL },
    { "$vpf",           NULL },
    { "$children",      NULL },
    { "$preview",       NULL },

    /* Constants / Common params */
    { "true",           NULL },
    { "false",          NULL },
    { "undef",          NULL },
    { "PI",             NULL },
    { "center",         NULL },
    { "convexity",      NULL },
    { "twist",          NULL },
    { "slices",         NULL },
};

#define SCAD_DB_COUNT (sizeof(SCAD_DB) / sizeof(SCAD_DB[0]))

/* ---- Completion state ---- */

struct DC_ScadCompletion {
    GtkSourceView  *view;       /* borrowed */
    GtkSourceBuffer *buffer;    /* borrowed */
    GtkWidget      *popover;    /* owned GtkPopover */
    GtkWidget      *listbox;    /* inside popover */
    GtkWidget      *syntax_label; /* syntax hint label below listbox */

    /* Current matches */
    int             match_idx[128]; /* indices into SCAD_DB */
    int             match_count;
    int             selected;       /* currently highlighted row */

    /* Word extraction */
    char            prefix[128];    /* word being typed */
    int             prefix_start_offset; /* char offset of word start */

    /* State */
    int             active;         /* popover is showing */
    int             suppressed;     /* user pressed Escape */
    int             syntax_ready;   /* syntax template available for Tab */
    int             syntax_db_idx;  /* SCAD_DB index of last completed entry */
};

/* ---- Forward declarations ---- */
static void update_completions(DC_ScadCompletion *comp);
static void show_popover(DC_ScadCompletion *comp);
static void hide_popover(DC_ScadCompletion *comp);
static void accept_completion(DC_ScadCompletion *comp);

/* ---- Word extraction ---- */

static int
extract_word_at_cursor(DC_ScadCompletion *comp)
{
    GtkTextIter cursor;
    GtkTextMark *mark = gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(comp->buffer));
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(comp->buffer), &cursor, mark);

    GtkTextIter start = cursor;

    /* Walk backward over word characters (alnum, underscore, $) */
    while (gtk_text_iter_backward_char(&start)) {
        gunichar ch = gtk_text_iter_get_char(&start);
        if (!g_unichar_isalnum(ch) && ch != '_' && ch != '$') {
            gtk_text_iter_forward_char(&start);
            break;
        }
    }

    /* Handle start-of-buffer */
    if (!gtk_text_iter_equal(&start, &cursor)) {
        gunichar first = gtk_text_iter_get_char(&start);
        if (!g_unichar_isalnum(first) && first != '_' && first != '$') {
            gtk_text_iter_forward_char(&start);
        }
    }

    char *word = gtk_text_iter_get_text(&start, &cursor);
    if (!word) {
        comp->prefix[0] = '\0';
        return 0;
    }

    size_t wlen = strlen(word);
    if (wlen >= sizeof(comp->prefix)) wlen = sizeof(comp->prefix) - 1;
    memcpy(comp->prefix, word, wlen);
    comp->prefix[wlen] = '\0';
    comp->prefix_start_offset = gtk_text_iter_get_offset(&start);

    g_free(word);
    return (int)wlen;
}

/* ---- Matching ---- */

static void
find_matches(DC_ScadCompletion *comp)
{
    comp->match_count = 0;
    int plen = (int)strlen(comp->prefix);
    if (plen < 1) return;

    /* Case-insensitive prefix match */
    for (int i = 0; i < (int)SCAD_DB_COUNT && comp->match_count < 128; i++) {
        const char *kw = SCAD_DB[i].keyword;
        int match = 1;
        for (int j = 0; j < plen; j++) {
            if (tolower((unsigned char)kw[j]) != tolower((unsigned char)comp->prefix[j])) {
                match = 0;
                break;
            }
            if (kw[j] == '\0') { match = 0; break; }
        }
        /* Don't show exact match as sole completion */
        if (match && !(plen == (int)strlen(kw))) {
            comp->match_idx[comp->match_count++] = i;
        }
    }
}

/* ---- Popover UI ---- */

static void
rebuild_listbox(DC_ScadCompletion *comp)
{
    /* Remove all children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(comp->listbox)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(comp->listbox), child);

    for (int i = 0; i < comp->match_count; i++) {
        const ScadEntry *e = &SCAD_DB[comp->match_idx[i]];

        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row_box, 4);
        gtk_widget_set_margin_end(row_box, 4);
        gtk_widget_set_margin_top(row_box, 1);
        gtk_widget_set_margin_bottom(row_box, 1);

        /* Keyword label — bold */
        GtkWidget *kw_label = gtk_label_new(e->keyword);
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes(GTK_LABEL(kw_label), attrs);
        pango_attr_list_unref(attrs);
        gtk_box_append(GTK_BOX(row_box), kw_label);

        /* Syntax hint — dim */
        if (e->syntax) {
            /* Show just the first line of syntax */
            char synbuf[80];
            const char *nl = strchr(e->syntax, '\n');
            size_t slen = nl ? (size_t)(nl - e->syntax) : strlen(e->syntax);
            if (slen >= sizeof(synbuf)) slen = sizeof(synbuf) - 1;
            memcpy(synbuf, e->syntax, slen);
            synbuf[slen] = '\0';

            GtkWidget *syn_label = gtk_label_new(synbuf);
            gtk_widget_set_opacity(syn_label, 0.5);
            gtk_widget_set_hexpand(syn_label, TRUE);
            gtk_label_set_xalign(GTK_LABEL(syn_label), 0.0f);
            gtk_box_append(GTK_BOX(row_box), syn_label);
        }

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        gtk_list_box_append(GTK_LIST_BOX(comp->listbox), row);
    }

    /* Select first row */
    comp->selected = 0;
    if (comp->match_count > 0) {
        GtkListBoxRow *first = gtk_list_box_get_row_at_index(
            GTK_LIST_BOX(comp->listbox), 0);
        if (first)
            gtk_list_box_select_row(GTK_LIST_BOX(comp->listbox), first);
    }
}

static void
position_popover(DC_ScadCompletion *comp)
{
    GtkTextIter cursor;
    GtkTextMark *mark = gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(comp->buffer));
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(comp->buffer), &cursor, mark);

    GdkRectangle rect;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(comp->view), &cursor, &rect);

    /* Convert buffer coords to widget coords */
    int wx, wy;
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(comp->view),
        GTK_TEXT_WINDOW_WIDGET, rect.x, rect.y + rect.height, &wx, &wy);

    GdkRectangle pointing = { wx, wy, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(comp->popover), &pointing);
}

static void
show_popover(DC_ScadCompletion *comp)
{
    if (comp->match_count == 0) {
        hide_popover(comp);
        return;
    }

    rebuild_listbox(comp);
    position_popover(comp);
    gtk_popover_popup(GTK_POPOVER(comp->popover));
    comp->active = 1;
    comp->suppressed = 0;

    /* Keep focus on the text view */
    gtk_widget_grab_focus(GTK_WIDGET(comp->view));
}

static void
hide_popover(DC_ScadCompletion *comp)
{
    gtk_popover_popdown(GTK_POPOVER(comp->popover));
    comp->active = 0;
}

/* ---- Completion acceptance ---- */

static void
accept_completion(DC_ScadCompletion *comp)
{
    if (comp->selected < 0 || comp->selected >= comp->match_count)
        return;

    const ScadEntry *e = &SCAD_DB[comp->match_idx[comp->selected]];

    /* Delete the prefix and insert the keyword */
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(comp->buffer),
        &start, comp->prefix_start_offset);
    GtkTextMark *mark = gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(comp->buffer));
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(comp->buffer), &end, mark);

    gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(comp->buffer));
    gtk_text_buffer_delete(GTK_TEXT_BUFFER(comp->buffer), &start, &end);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(comp->buffer), &start,
                           e->keyword, -1);
    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(comp->buffer));

    /* Transition to syntax hint phase — keep popover open with syntax */
    if (e->syntax) {
        comp->syntax_ready = 1;
        comp->syntax_db_idx = comp->match_idx[comp->selected];
        comp->active = 0; /* no longer in match-navigation mode */

        /* Rebuild listbox with single syntax entry, bolded */
        GtkWidget *child;
        while ((child = gtk_widget_get_first_child(comp->listbox)) != NULL)
            gtk_list_box_remove(GTK_LIST_BOX(comp->listbox), child);

        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row_box, 4);
        gtk_widget_set_margin_end(row_box, 4);
        gtk_widget_set_margin_top(row_box, 2);
        gtk_widget_set_margin_bottom(row_box, 2);

        GtkWidget *syn_label = gtk_label_new(e->syntax);
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        pango_attr_list_insert(attrs, pango_attr_family_new("Monospace"));
        gtk_label_set_attributes(GTK_LABEL(syn_label), attrs);
        pango_attr_list_unref(attrs);
        gtk_box_append(GTK_BOX(row_box), syn_label);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_append(GTK_LIST_BOX(comp->listbox), row);

        position_popover(comp);
        gtk_popover_popup(GTK_POPOVER(comp->popover));
        gtk_widget_grab_focus(GTK_WIDGET(comp->view));
    } else {
        hide_popover(comp);
        comp->syntax_ready = 0;
    }
}

/* ---- Key event handling ---- */

static gboolean
on_key_pressed(GtkEventControllerKey *ctrl, guint keyval,
               guint keycode, GdkModifierType mods, gpointer data)
{
    (void)ctrl; (void)keycode; (void)mods;
    DC_ScadCompletion *comp = data;

    /* Syntax hint phase — popover shows bold syntax, not in match mode */
    if (!comp->active && comp->syntax_ready) {
        if (keyval == GDK_KEY_Escape) {
            comp->syntax_ready = 0;
            hide_popover(comp);
            comp->suppressed = 1;
            return TRUE;
        }

        /* Tab inserts the syntax template (part after keyword) */
        if (keyval == GDK_KEY_Tab) {
            comp->syntax_ready = 0;
            const ScadEntry *e = &SCAD_DB[comp->syntax_db_idx];
            if (e->syntax) {
                const char *rest = e->syntax + strlen(e->keyword);

                GtkTextIter cursor;
                GtkTextMark *m = gtk_text_buffer_get_insert(
                    GTK_TEXT_BUFFER(comp->buffer));
                gtk_text_buffer_get_iter_at_mark(
                    GTK_TEXT_BUFFER(comp->buffer), &cursor, m);

                gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(comp->buffer));
                gtk_text_buffer_insert(GTK_TEXT_BUFFER(comp->buffer),
                                       &cursor, rest, -1);
                gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(comp->buffer));
            }
            hide_popover(comp);
            return TRUE;
        }

        /* Semicolon or Enter dismisses syntax hint (let key through) */
        if (keyval == GDK_KEY_semicolon || keyval == GDK_KEY_Return) {
            comp->syntax_ready = 0;
            hide_popover(comp);
            return FALSE; /* let the key be processed normally */
        }

        /* All other keys: keep syntax hint visible, let key through */
        return FALSE;
    }

    if (!comp->active) {
        if (keyval == GDK_KEY_Escape) {
            comp->suppressed = 0;
        }
        return FALSE;
    }

    switch (keyval) {
    case GDK_KEY_Down:
        if (comp->selected < comp->match_count - 1) {
            comp->selected++;
            GtkListBoxRow *row = gtk_list_box_get_row_at_index(
                GTK_LIST_BOX(comp->listbox), comp->selected);
            if (row)
                gtk_list_box_select_row(GTK_LIST_BOX(comp->listbox), row);
        }
        return TRUE;

    case GDK_KEY_Up:
        if (comp->selected > 0) {
            comp->selected--;
            GtkListBoxRow *row = gtk_list_box_get_row_at_index(
                GTK_LIST_BOX(comp->listbox), comp->selected);
            if (row)
                gtk_list_box_select_row(GTK_LIST_BOX(comp->listbox), row);
        }
        return TRUE;

    case GDK_KEY_Tab:
    case GDK_KEY_Return:
        accept_completion(comp);
        return TRUE;

    case GDK_KEY_Escape:
        hide_popover(comp);
        comp->suppressed = 1;
        return TRUE;

    default:
        return FALSE;
    }
}

static void
on_key_released(GtkEventControllerKey *ctrl, guint keyval,
                guint keycode, GdkModifierType mods, gpointer data)
{
    (void)ctrl; (void)keycode; (void)mods;
    DC_ScadCompletion *comp = data;

    /* Don't re-trigger after Escape until user types a new word */
    if (keyval == GDK_KEY_Escape) return;

    /* Navigation keys don't trigger re-filter */
    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down ||
        keyval == GDK_KEY_Tab || keyval == GDK_KEY_Return ||
        keyval == GDK_KEY_Left || keyval == GDK_KEY_Right)
        return;

    /* During syntax hint phase, don't trigger match completions —
     * the popover is showing the syntax template, not matches.
     * It stays up until ; or Enter (handled in key-pressed). */
    if (comp->syntax_ready)
        return;

    /* Reset suppression on new typing */
    if (g_unichar_isalnum(gdk_keyval_to_unicode(keyval)) ||
        keyval == GDK_KEY_underscore || keyval == GDK_KEY_dollar ||
        keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_Delete) {
        comp->suppressed = 0;
    }

    if (comp->suppressed) return;

    update_completions(comp);
}

static void
update_completions(DC_ScadCompletion *comp)
{
    int wlen = extract_word_at_cursor(comp);
    if (wlen < 2) {
        hide_popover(comp);
        return;
    }

    find_matches(comp);

    if (comp->match_count == 0) {
        hide_popover(comp);
        return;
    }

    show_popover(comp);
}

/* ---- Public API ---- */

DC_ScadCompletion *
dc_scad_completion_new(GtkSourceView *view, GtkSourceBuffer *buffer)
{
    DC_ScadCompletion *comp = calloc(1, sizeof(*comp));
    if (!comp) return NULL;

    comp->view = view;
    comp->buffer = buffer;

    /* Create popover attached to the view */
    comp->popover = gtk_popover_new();
    gtk_widget_set_parent(comp->popover, GTK_WIDGET(view));
    gtk_popover_set_autohide(GTK_POPOVER(comp->popover), FALSE);
    gtk_popover_set_has_arrow(GTK_POPOVER(comp->popover), FALSE);
    gtk_widget_set_can_focus(comp->popover, FALSE);
    gtk_widget_add_css_class(comp->popover, "completion-popover");

    /* Popover content: vertical box with listbox + syntax label */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Scrolled listbox for matches */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 200);
    gtk_scrolled_window_set_propagate_natural_height(
        GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_widget_set_size_request(scroll, 350, -1);

    comp->listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(comp->listbox),
        GTK_SELECTION_SINGLE);
    gtk_widget_set_can_focus(comp->listbox, FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), comp->listbox);
    gtk_box_append(GTK_BOX(vbox), scroll);

    gtk_popover_set_child(GTK_POPOVER(comp->popover), vbox);

    /* Syntax hint label — shown below the editor line after completion */
    comp->syntax_label = gtk_label_new("");
    gtk_widget_set_visible(comp->syntax_label, FALSE);
    gtk_label_set_xalign(GTK_LABEL(comp->syntax_label), 0.0f);
    gtk_widget_set_opacity(comp->syntax_label, 0.5);
    gtk_widget_add_css_class(comp->syntax_label, "completion-syntax");
    {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
        pango_attr_list_insert(attrs,
            pango_attr_family_new("Monospace"));
        gtk_label_set_attributes(GTK_LABEL(comp->syntax_label), attrs);
        pango_attr_list_unref(attrs);
    }

    /* Key controller on the view — pressed for navigation, released for filtering */
    GtkEventController *key = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key, GTK_PHASE_CAPTURE);
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), comp);
    g_signal_connect(key, "key-released", G_CALLBACK(on_key_released), comp);
    gtk_widget_add_controller(GTK_WIDGET(view), key);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "scad_completion: initialized");
    return comp;
}

void
dc_scad_completion_free(DC_ScadCompletion *comp)
{
    if (!comp) return;
    if (comp->popover) {
        gtk_widget_unparent(comp->popover);
    }
    free(comp);
}

GtkWidget *
dc_scad_completion_syntax_label(DC_ScadCompletion *comp)
{
    return comp ? comp->syntax_label : NULL;
}
