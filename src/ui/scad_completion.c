#define _POSIX_C_SOURCE 200809L
#include "ui/scad_completion.h"
#include "core/log.h"

#include <gtksourceview/gtksource.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* =========================================================================
 * Snippet-based autocomplete with tab-stop navigation.
 *
 * Bypasses GtkSourceView's completion (Wayland/NVIDIA popup bug, see s007).
 * Uses GtkPopover + GtkListBox for the dropdown, GtkTextMarks for tab stops.
 *
 * Flow:
 *   1. User types → prefix-match against snippet triggers
 *   2. Arrow/Tab to select → snippet template inserted
 *   3. Tab navigates through ${1:placeholder} stops
 *   4. After last required stop, optional param picker appears
 *   5. Enter exits snippet mode at any point
 * ========================================================================= */

/* ---- Optional parameter definition ---- */
typedef struct {
    const char *name;       /* e.g. "center" */
    const char *scad_val;   /* e.g. "true" */
    const char *dcad_val;   /* e.g. "true" */
} DC_SnipParam;

/* ---- Snippet definition ---- */
typedef struct {
    const char      *trigger;       /* match trigger: "cube" */
    const char      *tmpl_scad;     /* "cube([${1:x}, ${2:y}, ${3:z}]);" */
    const char      *tmpl_dcad;     /* "cube(${1:x}, ${2:y}, ${3:z});" */
    const DC_SnipParam *opt_params; /* NULL-terminated, or NULL */
} DC_Snippet;

/* ---- Optional param arrays ---- */
static const DC_SnipParam cube_opts[] = {
    { "center", "true", "true" }, {0}
};
static const DC_SnipParam sphere_opts[] = {
    { "$fn", "64", "64" }, {0}
};
static const DC_SnipParam cylinder_opts[] = {
    { "center", "true", "true" },
    { "$fn", "64", "64" }, {0}
};
static const DC_SnipParam circle_opts[] = {
    { "$fn", "64", "64" }, {0}
};
static const DC_SnipParam square_opts[] = {
    { "center", "true", "true" }, {0}
};
static const DC_SnipParam linext_opts[] = {
    { "center", "true", "true" },
    { "twist", "0", "0" },
    { "slices", "20", "20" },
    { "scale", "1.0", "1.0" },
    { "$fn", "64", "64" }, {0}
};
static const DC_SnipParam rotext_opts[] = {
    { "angle", "360", "360" },
    { "$fn", "64", "64" }, {0}
};

/* ---- Snippet database ---- */
static const DC_Snippet SNIPPET_DB[] = {
    /* 3D Primitives */
    { "cube",
      "cube([${1:x}, ${2:y}, ${3:z}]);",
      "cube(${1:x}, ${2:y}, ${3:z})",
      cube_opts },
    { "sphere",
      "sphere(r=${1:radius});",
      "sphere(r=${1:radius})",
      sphere_opts },
    { "cylinder",
      "cylinder(h=${1:height}, r=${2:radius});",
      "cylinder(h=${1:height}, r=${2:radius})",
      cylinder_opts },
    { "polyhedron",
      "polyhedron(points=${1:[[...]]}, faces=${2:[[...]]});",
      "polyhedron(points=${1:[[...]]}, faces=${2:[[...]]}})",
      NULL },

    /* 2D Primitives */
    { "circle",
      "circle(r=${1:radius});",
      "circle(r=${1:radius})",
      circle_opts },
    { "square",
      "square([${1:x}, ${2:y}]);",
      "square(${1:x}, ${2:y})",
      square_opts },
    { "polygon",
      "polygon(points=${1:[[x,y], ...]});",
      "polygon(points=${1:[[x,y], ...]})",
      NULL },
    { "text",
      "text(\"${1:string}\", size=${2:10});",
      "text(\"${1:string}\", size=${2:10})",
      NULL },

    /* Transforms */
    { "translate",
      "translate([${1:x}, ${2:y}, ${3:z}])",
      "move(${1:x}, ${2:y}, ${3:z})",
      NULL },
    { "move",
      "translate([${1:x}, ${2:y}, ${3:z}])",
      "move(${1:x}, ${2:y}, ${3:z})",
      NULL },
    { "rotate",
      "rotate([${1:x}, ${2:y}, ${3:z}])",
      "rotate(${1:x}, ${2:y}, ${3:z})",
      NULL },
    { "scale",
      "scale([${1:x}, ${2:y}, ${3:z}])",
      "scale(${1:x}, ${2:y}, ${3:z})",
      NULL },
    { "mirror",
      "mirror([${1:x}, ${2:y}, ${3:z}])",
      "mirror(${1:x}, ${2:y}, ${3:z})",
      NULL },
    { "color",
      "color(\"${1:colorname}\")",
      "color(\"${1:colorname}\")",
      NULL },
    { "multmatrix",
      "multmatrix(${1:m})",
      "matrix(${1:m})",
      NULL },

    /* Boolean Operations */
    { "difference",
      "difference() {\n\t${1:body};\n\t${2:cut};\n}",
      "${1:body} - ${2:cut}",
      NULL },
    { "union",
      "union() {\n\t${1:a};\n\t${2:b};\n}",
      "${1:a} + ${2:b}",
      NULL },
    { "intersection",
      "intersection() {\n\t${1:a};\n\t${2:b};\n}",
      "${1:a} & ${2:b}",
      NULL },
    { "hull",
      "hull() {\n\t${1:a};\n\t${2:b};\n}",
      "hull(${1:a}, ${2:b})",
      NULL },
    { "minkowski",
      "minkowski() {\n\t${1:a};\n\t${2:b};\n}",
      "minkowski(${1:a}, ${2:b})",
      NULL },

    /* Extrusion */
    { "linear_extrude",
      "linear_extrude(height=${1:h})",
      "sweep(h=${1:h})",
      linext_opts },
    { "sweep",
      "linear_extrude(height=${1:h})",
      "sweep(h=${1:h})",
      linext_opts },
    { "rotate_extrude",
      "rotate_extrude()",
      "revolve()",
      rotext_opts },
    { "revolve",
      "rotate_extrude()",
      "revolve()",
      rotext_opts },

    /* Control Flow */
    { "module",
      "module ${1:name}(${2:params}) {\n\t${3}\n}",
      "shape ${1:name}(${2:params}) {\n\t${3}\n}",
      NULL },
    { "shape",
      "module ${1:name}(${2:params}) {\n\t${3}\n}",
      "shape ${1:name}(${2:params}) {\n\t${3}\n}",
      NULL },
    { "function",
      "function ${1:name}(${2:params}) = ${3:expr};",
      "fn ${1:name}(${2:params}) = ${3:expr}",
      NULL },
    { "for",
      "for (${1:i} = [${2:0}:${3:n}]) {\n\t${4}\n}",
      "for ${1:i} in [${2:0}:${3:n}] {\n\t${4}\n}",
      NULL },
    { "if",
      "if (${1:condition}) {\n\t${2}\n}",
      "if (${1:condition}) {\n\t${2}\n}",
      NULL },
    { "let",
      "let (${1:x} = ${2:value})",
      "let ${1:x} = ${2:value}",
      NULL },
    { "echo",      "echo(${1:value});", "echo(${1:value})", NULL },
    { "assert",    "assert(${1:condition}, \"${2:msg}\");",
                   "assert(${1:condition}, \"${2:msg}\")", NULL },
    { "include",   "include <${1:file}.scad>", "include <${1:file}.dcad>", NULL },
    { "use",       "use <${1:file}.scad>", "use <${1:file}.dcad>", NULL },
    { "children",  "children(${1:index});", "children(${1:index})", NULL },

    /* Math (simple — no tab stops, just insert) */
    { "abs",    "abs(${1:x})",          "abs(${1:x})",          NULL },
    { "sin",    "sin(${1:degrees})",    "sin(${1:degrees})",    NULL },
    { "cos",    "cos(${1:degrees})",    "cos(${1:degrees})",    NULL },
    { "tan",    "tan(${1:degrees})",    "tan(${1:degrees})",    NULL },
    { "asin",   "asin(${1:x})",        "asin(${1:x})",         NULL },
    { "acos",   "acos(${1:x})",        "acos(${1:x})",         NULL },
    { "atan",   "atan(${1:x})",        "atan(${1:x})",         NULL },
    { "atan2",  "atan2(${1:y}, ${2:x})", "atan2(${1:y}, ${2:x})", NULL },
    { "sqrt",   "sqrt(${1:x})",        "sqrt(${1:x})",         NULL },
    { "pow",    "pow(${1:base}, ${2:exp})", "pow(${1:base}, ${2:exp})", NULL },
    { "floor",  "floor(${1:x})",       "floor(${1:x})",        NULL },
    { "ceil",   "ceil(${1:x})",        "ceil(${1:x})",         NULL },
    { "round",  "round(${1:x})",       "round(${1:x})",        NULL },
    { "min",    "min(${1:a}, ${2:b})", "min(${1:a}, ${2:b})",  NULL },
    { "max",    "max(${1:a}, ${2:b})", "max(${1:a}, ${2:b})",  NULL },
    { "len",    "len(${1:v})",         "len(${1:v})",          NULL },
    { "norm",   "norm(${1:v})",        "norm(${1:v})",         NULL },
    { "cross",  "cross(${1:a}, ${2:b})", "cross(${1:a}, ${2:b})", NULL },
    { "concat", "concat(${1:a}, ${2:b})", "concat(${1:a}, ${2:b})", NULL },
    { "str",    "str(${1:value})",     "str(${1:value})",      NULL },
    { "lookup", "lookup(${1:key}, ${2:table})", "lookup(${1:key}, ${2:table})", NULL },
    { "search", "search(${1:needle}, ${2:haystack})", "search(${1:needle}, ${2:haystack})", NULL },
    { "sign",   "sign(${1:x})",        "sign(${1:x})",         NULL },
    { "exp",    "exp(${1:x})",         "exp(${1:x})",          NULL },
    { "ln",     "ln(${1:x})",         "ln(${1:x})",           NULL },
    { "log",    "log(${1:x})",        "log(${1:x})",          NULL },

    /* Platonic solids */
    { "tetrahedron",  "tetrahedron(${1:r});",  "tetrahedron(${1:r})",  NULL },
    { "octahedron",   "octahedron(${1:r});",   "octahedron(${1:r})",   NULL },
    { "dodecahedron", "dodecahedron(${1:r});", "dodecahedron(${1:r})", NULL },
    { "icosahedron",  "icosahedron(${1:r});",  "icosahedron(${1:r})",  NULL },

    /* Special variables (no template) */
    { "$fn", NULL, NULL, NULL },
    { "$fa", NULL, NULL, NULL },
    { "$fs", NULL, NULL, NULL },
    { "$t",  NULL, NULL, NULL },

    /* Constants */
    { "true",  NULL, NULL, NULL },
    { "false", NULL, NULL, NULL },
    { "undef", NULL, NULL, NULL },
    { "PI",    NULL, NULL, NULL },

    /* Projection */
    { "projection", "projection(cut=${1:false})", "project(cut=${1:false})", NULL },
    { "import",     "import(\"${1:filename}\");", "import(\"${1:filename}\");", NULL },
    { "render",     "render(convexity=${1:10})",   "render(convexity=${1:10})", NULL },
};

#define SNIPPET_DB_COUNT (sizeof(SNIPPET_DB) / sizeof(SNIPPET_DB[0]))

/* ---- Tab stop tracking ---- */
#define MAX_TAB_STOPS 16

typedef struct {
    GtkTextMark *start;  /* left-gravity */
    GtkTextMark *end;    /* right-gravity */
} DC_StopMark;

typedef struct {
    int          active;
    int          snippet_idx;
    DC_StopMark  marks[MAX_TAB_STOPS];
    int          stop_count;
    int          current_stop;
    int          stop_dirty;   /* 1 after first keystroke in current stop */
    GtkTextMark *end_mark;     /* end of entire snippet */
} DC_SnippetSession;

/* ---- Completion state ---- */
struct DC_ScadCompletion {
    GtkSourceView   *view;
    GtkSourceBuffer *buffer;

    /* Completion popup */
    GtkWidget       *popover;
    GtkWidget       *listbox;
    GtkWidget       *syntax_label;  /* kept for layout compat */

    /* Optional param picker */
    GtkWidget       *opt_popover;
    GtkWidget       *opt_listbox;
    int              opt_active;
    int              opt_selected;

    /* Text tags for tab-stop highlighting */
    GtkTextTag      *tag_active;
    GtkTextTag      *tag_pending;

    /* Matching state */
    int              match_idx[128];
    int              match_count;
    int              selected;
    char             prefix[128];
    int              prefix_start_offset;
    int              active;
    int              suppressed;

    /* Snippet session */
    DC_SnippetSession session;

    /* Language mode */
    DC_LangMode      lang_mode;
};

/* ---- Forward declarations ---- */
static void update_completions(DC_ScadCompletion *comp);
static void show_popover(DC_ScadCompletion *comp);
static void hide_popover(DC_ScadCompletion *comp);
static void accept_completion(DC_ScadCompletion *comp);
static void activate_tab_stop(DC_ScadCompletion *comp, int index);
static void advance_tab_stop(DC_ScadCompletion *comp);
static void exit_snippet_mode(DC_ScadCompletion *comp);
static void show_opt_params(DC_ScadCompletion *comp);
static void insert_opt_param(DC_ScadCompletion *comp, int param_idx);
static void hide_opt_popover(DC_ScadCompletion *comp);

/* =========================================================================
 * Template parser: "${1:placeholder}" → flat text + tab stop positions
 * ========================================================================= */

typedef struct {
    int offset;
    int length;
} DC_ParsedStop;

/* Parse template, write flat text to `out`, record stops.
 * Returns length of flat text written. */
static int
parse_template(const char *tmpl, char *out, int out_size,
               DC_ParsedStop *stops, int *stop_count)
{
    int oi = 0;  /* output index */
    int ns = 0;  /* stop count */
    const char *p = tmpl;

    while (*p && oi < out_size - 1) {
        if (p[0] == '$' && p[1] == '{') {
            /* ${N:placeholder} */
            p += 2;
            /* skip N */
            while (*p && *p != ':' && *p != '}') p++;
            if (*p == ':') p++;  /* skip : */

            /* Extract placeholder */
            const char *ph_start = p;
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '{') depth++;
                else if (*p == '}') depth--;
                if (depth > 0) p++;
            }
            int ph_len = (int)(p - ph_start);
            if (*p == '}') p++;

            /* Record stop */
            if (ns < MAX_TAB_STOPS) {
                stops[ns].offset = oi;
                stops[ns].length = ph_len;
                ns++;
            }

            /* Copy placeholder to output */
            for (int i = 0; i < ph_len && oi < out_size - 1; i++)
                out[oi++] = ph_start[i];
        } else {
            out[oi++] = *p++;
        }
    }
    out[oi] = '\0';
    *stop_count = ns;
    return oi;
}

/* =========================================================================
 * Word extraction
 * ========================================================================= */

static int
extract_word_at_cursor(DC_ScadCompletion *comp)
{
    GtkTextIter cursor;
    GtkTextMark *mark = gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(comp->buffer));
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(comp->buffer), &cursor, mark);

    GtkTextIter start = cursor;

    while (gtk_text_iter_backward_char(&start)) {
        gunichar ch = gtk_text_iter_get_char(&start);
        if (!g_unichar_isalnum(ch) && ch != '_' && ch != '$') {
            gtk_text_iter_forward_char(&start);
            break;
        }
    }

    if (!gtk_text_iter_equal(&start, &cursor)) {
        gunichar first = gtk_text_iter_get_char(&start);
        if (!g_unichar_isalnum(first) && first != '_' && first != '$')
            gtk_text_iter_forward_char(&start);
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

/* =========================================================================
 * Matching
 * ========================================================================= */

static void
find_matches(DC_ScadCompletion *comp)
{
    comp->match_count = 0;
    int plen = (int)strlen(comp->prefix);
    if (plen < 1) return;

    for (int i = 0; i < (int)SNIPPET_DB_COUNT && comp->match_count < 128; i++) {
        const char *trigger = SNIPPET_DB[i].trigger;
        int match = 1;
        for (int j = 0; j < plen; j++) {
            if (tolower((unsigned char)trigger[j]) !=
                tolower((unsigned char)comp->prefix[j])) {
                match = 0; break;
            }
            if (trigger[j] == '\0') { match = 0; break; }
        }
        if (match && plen != (int)strlen(trigger))
            comp->match_idx[comp->match_count++] = i;
    }
}

/* =========================================================================
 * Popover UI
 * ========================================================================= */

static void
rebuild_listbox(DC_ScadCompletion *comp)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(comp->listbox)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(comp->listbox), child);

    for (int i = 0; i < comp->match_count; i++) {
        const DC_Snippet *s = &SNIPPET_DB[comp->match_idx[i]];

        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row_box, 4);
        gtk_widget_set_margin_end(row_box, 4);
        gtk_widget_set_margin_top(row_box, 1);
        gtk_widget_set_margin_bottom(row_box, 1);

        /* Trigger label — bold */
        GtkWidget *trigger_label = gtk_label_new(s->trigger);
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes(GTK_LABEL(trigger_label), attrs);
        pango_attr_list_unref(attrs);
        gtk_box_append(GTK_BOX(row_box), trigger_label);

        /* Template preview — dim, first line only */
        const char *tmpl = (comp->lang_mode == DC_LANG_CUBEIFORM)
            ? s->tmpl_dcad : s->tmpl_scad;
        if (tmpl) {
            /* Show flattened first line (strip ${N:} markers) */
            char flat[128];
            DC_ParsedStop dummy[MAX_TAB_STOPS];
            int dummy_count;
            parse_template(tmpl, flat, (int)sizeof(flat), dummy, &dummy_count);
            char *nl = strchr(flat, '\n');
            if (nl) *nl = '\0';

            GtkWidget *syn_label = gtk_label_new(flat);
            gtk_widget_set_opacity(syn_label, 0.45);
            gtk_widget_set_hexpand(syn_label, TRUE);
            gtk_label_set_xalign(GTK_LABEL(syn_label), 0.0f);
            PangoAttrList *sa = pango_attr_list_new();
            pango_attr_list_insert(sa, pango_attr_family_new("Monospace"));
            pango_attr_list_insert(sa, pango_attr_scale_new(0.85));
            gtk_label_set_attributes(GTK_LABEL(syn_label), sa);
            pango_attr_list_unref(sa);
            gtk_box_append(GTK_BOX(row_box), syn_label);
        }

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        gtk_list_box_append(GTK_LIST_BOX(comp->listbox), row);
    }

    comp->selected = 0;
    if (comp->match_count > 0) {
        GtkListBoxRow *first = gtk_list_box_get_row_at_index(
            GTK_LIST_BOX(comp->listbox), 0);
        if (first)
            gtk_list_box_select_row(GTK_LIST_BOX(comp->listbox), first);
    }
}

static void
position_popover_at_cursor(DC_ScadCompletion *comp, GtkWidget *popover)
{
    GtkTextIter cursor;
    GtkTextMark *mark = gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(comp->buffer));
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(comp->buffer), &cursor, mark);

    GdkRectangle rect;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(comp->view), &cursor, &rect);

    int wx, wy;
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(comp->view),
        GTK_TEXT_WINDOW_WIDGET, rect.x, rect.y + rect.height, &wx, &wy);

    GdkRectangle pointing = { wx, wy, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &pointing);
}

static void
show_popover(DC_ScadCompletion *comp)
{
    if (comp->match_count == 0) { hide_popover(comp); return; }

    rebuild_listbox(comp);
    position_popover_at_cursor(comp, comp->popover);
    gtk_popover_popup(GTK_POPOVER(comp->popover));
    comp->active = 1;
    comp->suppressed = 0;
    gtk_widget_grab_focus(GTK_WIDGET(comp->view));
}

static void
hide_popover(DC_ScadCompletion *comp)
{
    gtk_popover_popdown(GTK_POPOVER(comp->popover));
    comp->active = 0;
}

/* =========================================================================
 * Snippet insertion and tab-stop engine
 * ========================================================================= */

static void
insert_snippet(DC_ScadCompletion *comp, int snippet_idx)
{
    const DC_Snippet *snip = &SNIPPET_DB[snippet_idx];
    const char *tmpl = (comp->lang_mode == DC_LANG_CUBEIFORM)
        ? snip->tmpl_dcad : snip->tmpl_scad;

    if (!tmpl) {
        /* No template — just insert the trigger word */
        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(comp->buffer),
            &start, comp->prefix_start_offset);
        GtkTextMark *m = gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(comp->buffer));
        gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(comp->buffer), &end, m);

        gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(comp->buffer));
        gtk_text_buffer_delete(GTK_TEXT_BUFFER(comp->buffer), &start, &end);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(comp->buffer), &start,
                               snip->trigger, -1);
        gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(comp->buffer));
        return;
    }

    /* Parse template */
    char flat[2048];
    DC_ParsedStop stops[MAX_TAB_STOPS];
    int stop_count = 0;
    parse_template(tmpl, flat, (int)sizeof(flat), stops, &stop_count);

    /* Delete prefix and insert flat text */
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(comp->buffer),
        &start, comp->prefix_start_offset);
    GtkTextMark *m = gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(comp->buffer));
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(comp->buffer), &end, m);

    int insert_offset = comp->prefix_start_offset;

    gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(comp->buffer));
    gtk_text_buffer_delete(GTK_TEXT_BUFFER(comp->buffer), &start, &end);
    gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(comp->buffer),
        &start, insert_offset);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(comp->buffer), &start, flat, -1);
    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(comp->buffer));

    /* Set up snippet session with marks */
    DC_SnippetSession *ss = &comp->session;
    ss->active = 1;
    ss->snippet_idx = snippet_idx;
    ss->stop_count = stop_count;
    ss->current_stop = -1;

    for (int i = 0; i < stop_count; i++) {
        GtkTextIter si, ei;
        int abs_offset = insert_offset + stops[i].offset;
        gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(comp->buffer),
            &si, abs_offset);
        gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(comp->buffer),
            &ei, abs_offset + stops[i].length);

        char sname[32], ename[32];
        snprintf(sname, sizeof(sname), "snip-s%d", i);
        snprintf(ename, sizeof(ename), "snip-e%d", i);

        ss->marks[i].start = gtk_text_buffer_create_mark(
            GTK_TEXT_BUFFER(comp->buffer), sname, &si, TRUE);  /* left gravity */
        ss->marks[i].end = gtk_text_buffer_create_mark(
            GTK_TEXT_BUFFER(comp->buffer), ename, &ei, FALSE); /* right gravity */
    }

    /* End mark for the whole snippet */
    {
        GtkTextIter se;
        gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(comp->buffer),
            &se, insert_offset + (int)strlen(flat));
        ss->end_mark = gtk_text_buffer_create_mark(
            GTK_TEXT_BUFFER(comp->buffer), "snip-end", &se, FALSE);
    }

    /* Apply pending tag to all stops */
    for (int i = 0; i < stop_count; i++) {
        GtkTextIter si, ei;
        gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(comp->buffer),
            &si, ss->marks[i].start);
        gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(comp->buffer),
            &ei, ss->marks[i].end);
        gtk_text_buffer_apply_tag(GTK_TEXT_BUFFER(comp->buffer),
            comp->tag_pending, &si, &ei);
    }

    /* Activate first stop */
    if (stop_count > 0)
        activate_tab_stop(comp, 0);
}

static void
activate_tab_stop(DC_ScadCompletion *comp, int index)
{
    DC_SnippetSession *ss = &comp->session;
    if (index < 0 || index >= ss->stop_count) return;

    GtkTextBuffer *buf = GTK_TEXT_BUFFER(comp->buffer);

    /* Remove active tag from previous stop */
    if (ss->current_stop >= 0 && ss->current_stop < ss->stop_count) {
        GtkTextIter ps, pe;
        gtk_text_buffer_get_iter_at_mark(buf, &ps,
            ss->marks[ss->current_stop].start);
        gtk_text_buffer_get_iter_at_mark(buf, &pe,
            ss->marks[ss->current_stop].end);
        gtk_text_buffer_remove_tag(buf, comp->tag_active, &ps, &pe);
        gtk_text_buffer_apply_tag(buf, comp->tag_pending, &ps, &pe);
    }

    ss->current_stop = index;
    ss->stop_dirty = 0;

    /* Apply active tag and select the placeholder text */
    GtkTextIter si, ei;
    gtk_text_buffer_get_iter_at_mark(buf, &si, ss->marks[index].start);
    gtk_text_buffer_get_iter_at_mark(buf, &ei, ss->marks[index].end);

    /* Remove pending, apply active */
    gtk_text_buffer_remove_tag(buf, comp->tag_pending, &si, &ei);
    gtk_text_buffer_apply_tag(buf, comp->tag_active, &si, &ei);

    /* Place cursor at start of placeholder */
    gtk_text_buffer_place_cursor(buf, &si);
}

static void
advance_tab_stop(DC_ScadCompletion *comp)
{
    DC_SnippetSession *ss = &comp->session;
    if (!ss->active) return;

    /* Remove active tag from current stop */
    if (ss->current_stop >= 0 && ss->current_stop < ss->stop_count) {
        GtkTextBuffer *buf = GTK_TEXT_BUFFER(comp->buffer);
        GtkTextIter ps, pe;
        gtk_text_buffer_get_iter_at_mark(buf, &ps,
            ss->marks[ss->current_stop].start);
        gtk_text_buffer_get_iter_at_mark(buf, &pe,
            ss->marks[ss->current_stop].end);
        gtk_text_buffer_remove_tag(buf, comp->tag_active, &ps, &pe);
        gtk_text_buffer_remove_tag(buf, comp->tag_pending, &ps, &pe);
    }

    int next = ss->current_stop + 1;
    if (next < ss->stop_count) {
        activate_tab_stop(comp, next);
    } else {
        /* Past last required stop — check for optional params */
        const DC_Snippet *snip = &SNIPPET_DB[ss->snippet_idx];
        if (snip->opt_params && snip->opt_params[0].name) {
            show_opt_params(comp);
        } else {
            exit_snippet_mode(comp);
        }
    }
}

static void
exit_snippet_mode(DC_ScadCompletion *comp)
{
    DC_SnippetSession *ss = &comp->session;
    if (!ss->active) return;

    GtkTextBuffer *buf = GTK_TEXT_BUFFER(comp->buffer);

    /* Remove all tags and marks */
    for (int i = 0; i < ss->stop_count; i++) {
        GtkTextIter si, ei;
        gtk_text_buffer_get_iter_at_mark(buf, &si, ss->marks[i].start);
        gtk_text_buffer_get_iter_at_mark(buf, &ei, ss->marks[i].end);
        gtk_text_buffer_remove_tag(buf, comp->tag_active, &si, &ei);
        gtk_text_buffer_remove_tag(buf, comp->tag_pending, &si, &ei);
        gtk_text_buffer_delete_mark(buf, ss->marks[i].start);
        gtk_text_buffer_delete_mark(buf, ss->marks[i].end);
    }

    /* Place cursor at end of snippet */
    if (ss->end_mark) {
        GtkTextIter end_iter;
        gtk_text_buffer_get_iter_at_mark(buf, &end_iter, ss->end_mark);
        gtk_text_buffer_place_cursor(buf, &end_iter);
        gtk_text_buffer_delete_mark(buf, ss->end_mark);
        ss->end_mark = NULL;
    }

    ss->active = 0;
    ss->stop_count = 0;
    ss->current_stop = -1;

    hide_opt_popover(comp);
}

/* =========================================================================
 * Optional parameter picker
 * ========================================================================= */

static void
show_opt_params(DC_ScadCompletion *comp)
{
    DC_SnippetSession *ss = &comp->session;
    const DC_Snippet *snip = &SNIPPET_DB[ss->snippet_idx];
    if (!snip->opt_params) return;

    /* Count optional params */
    int count = 0;
    while (snip->opt_params[count].name) count++;
    if (count == 0) { exit_snippet_mode(comp); return; }

    /* Clear and rebuild opt listbox */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(comp->opt_listbox)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(comp->opt_listbox), child);

    /* Add "Done (Enter)" row at top */
    {
        GtkWidget *done_label = gtk_label_new("  Done (Enter)");
        gtk_widget_set_opacity(done_label, 0.5);
        gtk_label_set_xalign(GTK_LABEL(done_label), 0.0f);
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), done_label);
        gtk_list_box_append(GTK_LIST_BOX(comp->opt_listbox), row);
    }

    for (int i = 0; i < count; i++) {
        const char *val = (comp->lang_mode == DC_LANG_CUBEIFORM)
            ? snip->opt_params[i].dcad_val : snip->opt_params[i].scad_val;
        char label_text[128];
        /* For OpenSCAD, $fn stays as $fn; for Cubeiform, strip the $ */
        const char *pname = snip->opt_params[i].name;
        if (comp->lang_mode == DC_LANG_CUBEIFORM && pname[0] == '$')
            pname++;
        snprintf(label_text, sizeof(label_text), "  %s = %s", pname, val);

        GtkWidget *label = gtk_label_new(label_text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_family_new("Monospace"));
        gtk_label_set_attributes(GTK_LABEL(label), attrs);
        pango_attr_list_unref(attrs);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_list_box_append(GTK_LIST_BOX(comp->opt_listbox), row);
    }

    comp->opt_selected = 0;
    GtkListBoxRow *first = gtk_list_box_get_row_at_index(
        GTK_LIST_BOX(comp->opt_listbox), 0);
    if (first)
        gtk_list_box_select_row(GTK_LIST_BOX(comp->opt_listbox), first);

    position_popover_at_cursor(comp, comp->opt_popover);
    gtk_popover_popup(GTK_POPOVER(comp->opt_popover));
    comp->opt_active = 1;
    gtk_widget_grab_focus(GTK_WIDGET(comp->view));
}

static void
insert_opt_param(DC_ScadCompletion *comp, int param_idx)
{
    DC_SnippetSession *ss = &comp->session;
    const DC_Snippet *snip = &SNIPPET_DB[ss->snippet_idx];

    const char *pname = snip->opt_params[param_idx].name;
    const char *val = (comp->lang_mode == DC_LANG_CUBEIFORM)
        ? snip->opt_params[param_idx].dcad_val
        : snip->opt_params[param_idx].scad_val;

    /* For Cubeiform, strip $ prefix from special vars */
    if (comp->lang_mode == DC_LANG_CUBEIFORM && pname[0] == '$')
        pname++;

    /* Build ", name=value" string */
    char insert_text[128];
    snprintf(insert_text, sizeof(insert_text), ", %s=%s", pname, val);

    GtkTextBuffer *buf = GTK_TEXT_BUFFER(comp->buffer);

    /* Find where to insert — just before the closing paren/bracket/semicolon.
     * Use the cursor position (should be at end of last param). */
    GtkTextIter cursor;
    GtkTextMark *m = gtk_text_buffer_get_insert(buf);
    gtk_text_buffer_get_iter_at_mark(buf, &cursor, m);

    /* Scan forward to find ) or ; */
    GtkTextIter search = cursor;
    while (!gtk_text_iter_is_end(&search)) {
        gunichar ch = gtk_text_iter_get_char(&search);
        if (ch == ')' || ch == ';') break;
        gtk_text_iter_forward_char(&search);
    }

    /* Insert before the closing char */
    int insert_pos = gtk_text_iter_get_offset(&search);
    GtkTextIter ins_iter;
    gtk_text_buffer_get_iter_at_offset(buf, &ins_iter, insert_pos);

    gtk_text_buffer_begin_user_action(buf);
    gtk_text_buffer_insert(buf, &ins_iter, insert_text, -1);
    gtk_text_buffer_end_user_action(buf);

    /* Select just the value part for editing */
    int val_start = insert_pos + (int)strlen(insert_text) - (int)strlen(val);
    int val_end = insert_pos + (int)strlen(insert_text);
    GtkTextIter vs, ve;
    gtk_text_buffer_get_iter_at_offset(buf, &vs, val_start);
    gtk_text_buffer_get_iter_at_offset(buf, &ve, val_end);
    gtk_text_buffer_apply_tag(buf, comp->tag_active, &vs, &ve);
    gtk_text_buffer_select_range(buf, &ve, &vs);

    /* Hide opt popover — will re-show on next tab */
    hide_opt_popover(comp);
}

static void
hide_opt_popover(DC_ScadCompletion *comp)
{
    if (comp->opt_popover)
        gtk_popover_popdown(GTK_POPOVER(comp->opt_popover));
    comp->opt_active = 0;
}

/* =========================================================================
 * Cubeiform pipe continuation helpers
 * ========================================================================= */

/* Insert newline + indent + ">>" for pipe continuation */
static void
insert_pipe_continuation(DC_ScadCompletion *comp)
{
    GtkTextBuffer *buf = GTK_TEXT_BUFFER(comp->buffer);
    gtk_text_buffer_begin_user_action(buf);
    gtk_text_buffer_insert_at_cursor(buf, "\n    >> ", -1);
    gtk_text_buffer_end_user_action(buf);
}

/* =========================================================================
 * Key event handling — three mode priority
 * ========================================================================= */

static gboolean
on_key_pressed(GtkEventControllerKey *ctrl, guint keyval,
               guint keycode, GdkModifierType mods, gpointer data)
{
    (void)ctrl; (void)keycode;
    DC_ScadCompletion *comp = data;
    int shift = (mods & GDK_SHIFT_MASK) != 0;

    /* ---- Mode 1: Optional param picker ---- */
    if (comp->opt_active) {
        const DC_Snippet *snip = &SNIPPET_DB[comp->session.snippet_idx];
        int opt_count = 0;
        if (snip->opt_params)
            while (snip->opt_params[opt_count].name) opt_count++;

        int total_rows = opt_count + 1;  /* +1 for "Done" row */

        switch (keyval) {
        case GDK_KEY_Down:
            if (comp->opt_selected < total_rows - 1) {
                comp->opt_selected++;
                GtkListBoxRow *row = gtk_list_box_get_row_at_index(
                    GTK_LIST_BOX(comp->opt_listbox), comp->opt_selected);
                if (row)
                    gtk_list_box_select_row(GTK_LIST_BOX(comp->opt_listbox), row);
            }
            return TRUE;
        case GDK_KEY_Up:
            if (comp->opt_selected > 0) {
                comp->opt_selected--;
                GtkListBoxRow *row = gtk_list_box_get_row_at_index(
                    GTK_LIST_BOX(comp->opt_listbox), comp->opt_selected);
                if (row)
                    gtk_list_box_select_row(GTK_LIST_BOX(comp->opt_listbox), row);
            }
            return TRUE;
        case GDK_KEY_Tab:
            if (comp->opt_selected == 0) {
                /* "Done" row */
                exit_snippet_mode(comp);
            } else {
                insert_opt_param(comp, comp->opt_selected - 1);
            }
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_Escape:
            exit_snippet_mode(comp);
            return (keyval == GDK_KEY_Escape);
        default:
            exit_snippet_mode(comp);
            return FALSE;
        }
    }

    /* ---- Mode 2: Snippet tab-stop navigation ---- */
    if (comp->session.active) {
        DC_SnippetSession *ss = &comp->session;

        if (keyval == GDK_KEY_Tab && !shift) {
            advance_tab_stop(comp);
            return TRUE;
        }
        if (keyval == GDK_KEY_Escape) {
            exit_snippet_mode(comp);
            return TRUE;
        }
        if (keyval == GDK_KEY_Return) {
            /* Exit snippet mode, cursor goes to end of snippet.
             * Consume the Enter — don't insert newline yet. */
            exit_snippet_mode(comp);
            return TRUE;
        }

        /* First printable keystroke in a stop: delete placeholder, insert char */
        if (!ss->stop_dirty && ss->current_stop >= 0 &&
            ss->current_stop < ss->stop_count) {
            gunichar ch = gdk_keyval_to_unicode(keyval);
            if (ch && g_unichar_isprint(ch)) {
                GtkTextBuffer *buf = GTK_TEXT_BUFFER(comp->buffer);
                GtkTextIter si, ei;
                gtk_text_buffer_get_iter_at_mark(buf, &si,
                    ss->marks[ss->current_stop].start);
                gtk_text_buffer_get_iter_at_mark(buf, &ei,
                    ss->marks[ss->current_stop].end);

                /* Delete placeholder text */
                gtk_text_buffer_begin_user_action(buf);
                gtk_text_buffer_delete(buf, &si, &ei);

                /* Insert the typed character */
                char utf8[6];
                int len = g_unichar_to_utf8(ch, utf8);
                gtk_text_buffer_get_iter_at_mark(buf, &si,
                    ss->marks[ss->current_stop].start);
                gtk_text_buffer_insert(buf, &si, utf8, len);
                gtk_text_buffer_end_user_action(buf);

                ss->stop_dirty = 1;
                return TRUE;
            }
        }

        /* Subsequent keystrokes: let GTK handle normally */
        return FALSE;
    }

    /* ---- Mode 3: Completion popup navigation ---- */
    if (comp->active) {
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
            accept_completion(comp);
            return TRUE;
        case GDK_KEY_Return:
            hide_popover(comp);
            return FALSE;
        case GDK_KEY_Escape:
            hide_popover(comp);
            comp->suppressed = 1;
            return TRUE;
        default:
            return FALSE;
        }
    }

    /* ---- Cubeiform pipe continuation (outside all modes) ---- */
    if (comp->lang_mode == DC_LANG_CUBEIFORM) {
        int ctrl_key = (mods & GDK_CONTROL_MASK) != 0;

        /* Ctrl+Tab: insert pipe continuation line */
        if (keyval == GDK_KEY_Tab && ctrl_key) {
            insert_pipe_continuation(comp);
            return TRUE;
        }

        /* Enter: three-stage behavior
         *   1. Line has content (no semicolon) → insert pipe continuation
         *   2. Line is just "    >>" (empty pipe) → delete it, leave blank line
         *   3. Line is empty/whitespace → normal unindented newline */
        if (keyval == GDK_KEY_Return && !shift && !ctrl_key) {
            GtkTextBuffer *buf = GTK_TEXT_BUFFER(comp->buffer);
            GtkTextIter cursor, line_start;
            GtkTextMark *m = gtk_text_buffer_get_insert(buf);
            gtk_text_buffer_get_iter_at_mark(buf, &cursor, m);
            line_start = cursor;
            gtk_text_iter_set_line_offset(&line_start, 0);
            char *line = gtk_text_iter_get_text(&line_start, &cursor);

            if (line) {
                /* Check what's on this line */
                char *stripped = line;
                while (*stripped == ' ' || *stripped == '\t') stripped++;

                /* Check for empty pipe: just ">>" with optional trailing spaces */
                const char *p = stripped;
                int is_empty_pipe = (p[0] == '>' && p[1] == '>');
                if (is_empty_pipe) {
                    p += 2;
                    while (*p == ' ') p++;
                    is_empty_pipe = (*p == '\0');
                }
                if (is_empty_pipe) {
                    /* Stage 2: empty pipe line "    >>" → delete it, leave cursor on blank line */
                    gtk_text_buffer_begin_user_action(buf);
                    gtk_text_buffer_delete(buf, &line_start, &cursor);
                    gtk_text_buffer_end_user_action(buf);
                    g_free(line);
                    return TRUE;
                }

                if (*stripped == '\0') {
                    /* Stage 3: empty/whitespace line → normal newline (let GTK handle) */
                    g_free(line);
                    return FALSE;
                }

                if (strchr(line, ';') != NULL) {
                    /* Has semicolon — normal newline */
                    g_free(line);
                    return FALSE;
                }

                /* Stage 1: line has content, no semicolon → pipe continuation */
                g_free(line);
                insert_pipe_continuation(comp);
                return TRUE;
            }
        }
    }

    /* Not in any mode */
    if (keyval == GDK_KEY_Escape)
        comp->suppressed = 0;

    return FALSE;
}

static void
on_key_released(GtkEventControllerKey *ctrl, guint keyval,
                guint keycode, GdkModifierType mods, gpointer data)
{
    (void)ctrl; (void)keycode; (void)mods;
    DC_ScadCompletion *comp = data;

    if (keyval == GDK_KEY_Escape) return;

    /* Navigation keys don't trigger re-filter */
    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down ||
        keyval == GDK_KEY_Tab || keyval == GDK_KEY_Return ||
        keyval == GDK_KEY_Left || keyval == GDK_KEY_Right)
        return;

    /* Don't trigger completion during snippet or opt mode */
    if (comp->session.active || comp->opt_active) return;

    /* Reset suppression on typing */
    if (g_unichar_isalnum(gdk_keyval_to_unicode(keyval)) ||
        keyval == GDK_KEY_underscore || keyval == GDK_KEY_dollar ||
        keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_Delete) {
        comp->suppressed = 0;
    }

    if (comp->suppressed) return;

    update_completions(comp);
}

static void
accept_completion(DC_ScadCompletion *comp)
{
    if (comp->selected < 0 || comp->selected >= comp->match_count)
        return;

    int snippet_idx = comp->match_idx[comp->selected];
    hide_popover(comp);
    insert_snippet(comp, snippet_idx);
}

static void
update_completions(DC_ScadCompletion *comp)
{
    int wlen = extract_word_at_cursor(comp);
    if (wlen < 2) { hide_popover(comp); return; }

    find_matches(comp);
    if (comp->match_count == 0) { hide_popover(comp); return; }

    show_popover(comp);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

DC_ScadCompletion *
dc_scad_completion_new(GtkSourceView *view, GtkSourceBuffer *buffer)
{
    DC_ScadCompletion *comp = calloc(1, sizeof(*comp));
    if (!comp) return NULL;

    comp->view = view;
    comp->buffer = buffer;
    comp->lang_mode = DC_LANG_OPENSCAD;  /* default */
    comp->session.current_stop = -1;

    /* ---- Text tags for tab-stop highlighting ---- */
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(
        GTK_TEXT_BUFFER(buffer));

    comp->tag_active = gtk_text_tag_new("snippet-active");
    g_object_set(comp->tag_active,
        "background", "#264f78",
        "background-set", TRUE,
        "foreground", "#ffffff",
        "foreground-set", TRUE,
        NULL);
    gtk_text_tag_table_add(table, comp->tag_active);

    comp->tag_pending = gtk_text_tag_new("snippet-pending");
    g_object_set(comp->tag_pending,
        "underline", PANGO_UNDERLINE_SINGLE,
        "underline-set", TRUE,
        NULL);
    gtk_text_tag_table_add(table, comp->tag_pending);

    /* ---- Completion popover ---- */
    comp->popover = gtk_popover_new();
    gtk_widget_set_parent(comp->popover, GTK_WIDGET(view));
    gtk_popover_set_autohide(GTK_POPOVER(comp->popover), FALSE);
    gtk_popover_set_has_arrow(GTK_POPOVER(comp->popover), FALSE);
    gtk_widget_set_can_focus(comp->popover, FALSE);
    gtk_widget_add_css_class(comp->popover, "completion-popover");

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 200);
    gtk_scrolled_window_set_propagate_natural_height(
        GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_widget_set_size_request(scroll, 400, -1);

    comp->listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(comp->listbox),
        GTK_SELECTION_SINGLE);
    gtk_widget_set_can_focus(comp->listbox, FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), comp->listbox);
    gtk_popover_set_child(GTK_POPOVER(comp->popover), scroll);

    /* ---- Optional param popover ---- */
    comp->opt_popover = gtk_popover_new();
    gtk_widget_set_parent(comp->opt_popover, GTK_WIDGET(view));
    gtk_popover_set_autohide(GTK_POPOVER(comp->opt_popover), FALSE);
    gtk_popover_set_has_arrow(GTK_POPOVER(comp->opt_popover), FALSE);
    gtk_widget_set_can_focus(comp->opt_popover, FALSE);
    gtk_widget_add_css_class(comp->opt_popover, "completion-popover");

    comp->opt_listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(comp->opt_listbox),
        GTK_SELECTION_SINGLE);
    gtk_widget_set_can_focus(comp->opt_listbox, FALSE);

    GtkWidget *opt_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(opt_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(
        GTK_SCROLLED_WINDOW(opt_scroll), 150);
    gtk_scrolled_window_set_propagate_natural_height(
        GTK_SCROLLED_WINDOW(opt_scroll), TRUE);
    gtk_widget_set_size_request(opt_scroll, 250, -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(opt_scroll),
        comp->opt_listbox);
    gtk_popover_set_child(GTK_POPOVER(comp->opt_popover), opt_scroll);

    /* ---- Syntax label (kept for layout compat) ---- */
    comp->syntax_label = gtk_label_new("");
    gtk_widget_set_visible(comp->syntax_label, FALSE);

    /* ---- Key controller ---- */
    GtkEventController *key = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key, GTK_PHASE_CAPTURE);
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), comp);
    g_signal_connect(key, "key-released", G_CALLBACK(on_key_released), comp);
    gtk_widget_add_controller(GTK_WIDGET(view), key);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "snippet_completion: initialized (lang=%s)",
           comp->lang_mode == DC_LANG_CUBEIFORM ? "cubeiform" : "openscad");

    return comp;
}

void
dc_scad_completion_free(DC_ScadCompletion *comp)
{
    if (!comp) return;

    /* Clean up any active snippet session */
    if (comp->session.active)
        exit_snippet_mode(comp);

    if (comp->popover)
        gtk_widget_unparent(comp->popover);
    if (comp->opt_popover)
        gtk_widget_unparent(comp->opt_popover);

    free(comp);
}

void
dc_scad_completion_set_lang_mode(DC_ScadCompletion *comp, DC_LangMode mode)
{
    if (comp) comp->lang_mode = mode;
}

DC_LangMode
dc_scad_completion_get_lang_mode(DC_ScadCompletion *comp)
{
    return comp ? comp->lang_mode : DC_LANG_OPENSCAD;
}

GtkWidget *
dc_scad_completion_syntax_label(DC_ScadCompletion *comp)
{
    return comp ? comp->syntax_label : NULL;
}
