#define _POSIX_C_SOURCE 200809L
#include "ui/transform_panel.h"
#include "ui/code_editor.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* -------------------------------------------------------------------------
 * Transform panel — overlay for editing translate/rotate in the viewport.
 *
 * Parses SCAD text for translate([x,y,z]) and rotate([x,y,z]),
 * shows editable entries, and live-updates the code editor on changes.
 * Only shows fields for transforms that exist in the selected statement.
 * ---------------------------------------------------------------------- */

struct DC_TransformPanel {
    GtkWidget     *container;      /* outer frame */
    GtkWidget     *grid;           /* layout grid */
    GtkWidget     *trans_header;   /* "Translate" label */
    GtkWidget     *rot_header;     /* "Rotate" label */
    GtkWidget     *trans_labels[3]; /* X/Y/Z labels for translate */
    GtkWidget     *rot_labels[3];   /* X/Y/Z labels for rotate */
    GtkWidget     *trans_entries[3]; /* tx, ty, tz entries */
    GtkWidget     *rot_entries[3];   /* rx, ry, rz entries */
    DC_CodeEditor *code_ed;        /* borrowed */
    int            line_start;     /* 1-based */
    int            line_end;       /* 1-based, inclusive */
    double         values[6];      /* tx,ty,tz,rx,ry,rz */
    int            has_translate;
    int            has_rotate;
    int            updating;       /* suppress re-entry during code update */
    char          *orig_stmt;      /* original statement text (owned) */
    DC_TransformEnterCb enter_cb;  /* called on Enter in entry */
    void          *enter_cb_data;
};

/* ---- SCAD transform parsing ---- */

static int
parse_transform(const char *text, const char *keyword,
                double *x, double *y, double *z)
{
    *x = *y = *z = 0.0;
    size_t klen = strlen(keyword);
    const char *p = text;
    while ((p = strstr(p, keyword)) != NULL) {
        const char *after = p + klen;
        while (*after && isspace((unsigned char)*after)) after++;
        if (*after == '(') {
            after++;
            while (*after && isspace((unsigned char)*after)) after++;
            if (*after == '[') {
                after++;
                if (sscanf(after, "%lf , %lf , %lf", x, y, z) == 3)
                    return 1;
            }
        }
        p += klen;
    }
    return 0;
}

/* ---- Line range extraction helper ---- */

/* Extract text for lines [line_start..line_end] (1-based, inclusive)
 * from full text. Returns malloc'd string, caller frees. */
static char *
extract_lines(const char *full, int line_start, int line_end)
{
    int line = 1;
    const char *p = full;
    const char *start_ptr = NULL;
    const char *end_ptr = NULL;

    while (*p) {
        if (line == line_start && !start_ptr)
            start_ptr = p;
        if (*p == '\n') {
            if (line == line_end) {
                end_ptr = p;
                break;
            }
            line++;
        }
        p++;
    }
    if (!start_ptr) start_ptr = full;
    if (!end_ptr) end_ptr = full + strlen(full);

    size_t len = (size_t)(end_ptr - start_ptr);
    char *result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, start_ptr, len);
    result[len] = '\0';
    return result;
}

/* Replace lines [line_start..line_end] in code editor with new_text. */
static void
replace_lines(DC_TransformPanel *tp, const char *new_text)
{
    if (!tp->code_ed) return;

    char *full = dc_code_editor_get_text(tp->code_ed);
    if (!full) return;

    int line = 1;
    const char *p = full;
    const char *start_ptr = NULL;
    const char *end_ptr = NULL;

    while (*p) {
        if (line == tp->line_start && !start_ptr)
            start_ptr = p;
        if (*p == '\n') {
            if (line == tp->line_end) {
                end_ptr = p + 1; /* include the newline */
                break;
            }
            line++;
        }
        p++;
    }
    if (!start_ptr) start_ptr = full;
    if (!end_ptr) end_ptr = full + strlen(full);

    size_t prefix_len = (size_t)(start_ptr - full);
    size_t suffix_len = strlen(end_ptr);
    size_t new_len = strlen(new_text);
    size_t total = prefix_len + new_len + suffix_len + 1;

    char *result = malloc(total);
    if (!result) { free(full); return; }

    memcpy(result, full, prefix_len);
    memcpy(result + prefix_len, new_text, new_len);
    memcpy(result + prefix_len + new_len, end_ptr, suffix_len);
    result[prefix_len + new_len + suffix_len] = '\0';

    tp->updating = 1;
    dc_code_editor_set_text(tp->code_ed, result);
    tp->updating = 0;

    free(result);
    free(full);
}

/* ---- Transform stripping and rebuilding ---- */

/* Strip existing translate(...) and rotate(...) prefixes from text. */
static const char *
strip_transforms(const char *text)
{
    const char *p = text;
    while (*p && isspace((unsigned char)*p)) p++;

    for (;;) {
        int found = 0;
        const char *keywords[] = {"translate", "rotate", NULL};
        for (int k = 0; keywords[k]; k++) {
            size_t klen = strlen(keywords[k]);
            if (strncmp(p, keywords[k], klen) == 0) {
                const char *q = p + klen;
                while (*q && isspace((unsigned char)*q)) q++;
                if (*q == '(') {
                    int depth = 1;
                    q++;
                    while (*q && depth > 0) {
                        if (*q == '(') depth++;
                        else if (*q == ')') depth--;
                        q++;
                    }
                    while (*q && isspace((unsigned char)*q)) q++;
                    p = q;
                    found = 1;
                    break;
                }
            }
        }
        if (!found) break;
    }
    return p;
}

static char *
build_transformed_text(DC_TransformPanel *tp, const char *orig_text)
{
    const char *core = strip_transforms(orig_text);

    int has_trans = (tp->values[0] != 0.0 || tp->values[1] != 0.0 ||
                     tp->values[2] != 0.0);
    int has_rot   = (tp->values[3] != 0.0 || tp->values[4] != 0.0 ||
                     tp->values[5] != 0.0);

    char trans_buf[128] = "";
    char rot_buf[128] = "";

    if (has_trans) {
        snprintf(trans_buf, sizeof(trans_buf),
                 "translate([%g, %g, %g]) ",
                 tp->values[0], tp->values[1], tp->values[2]);
    }
    if (has_rot) {
        snprintf(rot_buf, sizeof(rot_buf),
                 "rotate([%g, %g, %g]) ",
                 tp->values[3], tp->values[4], tp->values[5]);
    }

    size_t len = strlen(trans_buf) + strlen(rot_buf) + strlen(core) + 2;
    char *result = malloc(len);
    if (!result) return NULL;
    snprintf(result, len, "%s%s%s", trans_buf, rot_buf, core);
    return result;
}

/* ---- Entry activate handler (Enter key) ---- */

static void
on_entry_activate(GtkEntry *entry, gpointer data)
{
    (void)entry;
    DC_TransformPanel *tp = data;
    if (tp->enter_cb)
        tp->enter_cb(tp->enter_cb_data);
}

/* ---- Entry change handler ---- */

static void
on_entry_changed(GtkEditable *editable, gpointer data)
{
    (void)editable;
    DC_TransformPanel *tp = data;
    if (tp->updating || !tp->code_ed) return;

    /* Read values from visible entries only */
    if (tp->has_translate) {
        for (int i = 0; i < 3; i++) {
            const char *text = gtk_editable_get_text(
                GTK_EDITABLE(tp->trans_entries[i]));
            tp->values[i] = atof(text);
        }
    }
    if (tp->has_rotate) {
        for (int i = 0; i < 3; i++) {
            const char *text = gtk_editable_get_text(
                GTK_EDITABLE(tp->rot_entries[i]));
            tp->values[3 + i] = atof(text);
        }
    }

    /* Extract current statement from editor */
    char *full = dc_code_editor_get_text(tp->code_ed);
    if (!full) return;

    char *old_stmt = extract_lines(full, tp->line_start, tp->line_end);
    if (!old_stmt) { free(full); return; }

    /* Build new statement */
    char *new_stmt = build_transformed_text(tp, old_stmt);
    free(old_stmt);

    if (new_stmt) {
        /* Check if original had trailing newline */
        int line = 1;
        const char *p = full;
        while (*p) {
            if (*p == '\n' && line == tp->line_end) {
                /* Append newline to replacement */
                size_t nlen = strlen(new_stmt);
                char *with_nl = malloc(nlen + 2);
                if (with_nl) {
                    memcpy(with_nl, new_stmt, nlen);
                    with_nl[nlen] = '\n';
                    with_nl[nlen + 1] = '\0';
                    free(new_stmt);
                    new_stmt = with_nl;
                }
                break;
            }
            if (*p == '\n') line++;
            p++;
        }

        replace_lines(tp, new_stmt);

        /* Update stored original statement for next edit */
        free(tp->orig_stmt);
        tp->orig_stmt = new_stmt; /* transfer ownership */
    }

    free(full);
}

/* ---- Visibility helpers ---- */

static void
show_translate_fields(DC_TransformPanel *tp, int show)
{
    gtk_widget_set_visible(tp->trans_header, show);
    for (int i = 0; i < 3; i++) {
        gtk_widget_set_visible(tp->trans_labels[i], show);
        gtk_widget_set_visible(tp->trans_entries[i], show);
    }
}

static void
show_rotate_fields(DC_TransformPanel *tp, int show)
{
    gtk_widget_set_visible(tp->rot_header, show);
    for (int i = 0; i < 3; i++) {
        gtk_widget_set_visible(tp->rot_labels[i], show);
        gtk_widget_set_visible(tp->rot_entries[i], show);
    }
}

/* ---- Public API ---- */

DC_TransformPanel *
dc_transform_panel_new(void)
{
    DC_TransformPanel *tp = calloc(1, sizeof(*tp));
    if (!tp) return NULL;

    tp->container = gtk_frame_new(NULL);
    gtk_widget_set_halign(tp->container, GTK_ALIGN_START);
    gtk_widget_set_valign(tp->container, GTK_ALIGN_END);
    gtk_widget_set_margin_start(tp->container, 8);
    gtk_widget_set_margin_bottom(tp->container, 8);
    gtk_widget_set_opacity(tp->container, 0.92);
    gtk_widget_add_css_class(tp->container, "transform-panel");
    gtk_widget_set_visible(tp->container, FALSE);

    tp->grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(tp->grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(tp->grid), 2);
    gtk_widget_set_margin_start(tp->grid, 6);
    gtk_widget_set_margin_end(tp->grid, 6);
    gtk_widget_set_margin_top(tp->grid, 4);
    gtk_widget_set_margin_bottom(tp->grid, 4);

    const char *axis_labels[] = {"X", "Y", "Z"};

    /* Translate column (col 0-1) */
    tp->trans_header = gtk_label_new("Translate");
    {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
        gtk_label_set_attributes(GTK_LABEL(tp->trans_header), attrs);
        pango_attr_list_unref(attrs);
    }
    gtk_grid_attach(GTK_GRID(tp->grid), tp->trans_header, 0, 0, 2, 1);

    for (int i = 0; i < 3; i++) {
        tp->trans_labels[i] = gtk_label_new(axis_labels[i]);
        gtk_widget_set_opacity(tp->trans_labels[i], 0.6);
        gtk_grid_attach(GTK_GRID(tp->grid), tp->trans_labels[i], 0, i + 1, 1, 1);

        tp->trans_entries[i] = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(tp->trans_entries[i]), "0");
        gtk_editable_set_width_chars(GTK_EDITABLE(tp->trans_entries[i]), 8);
        gtk_editable_set_text(GTK_EDITABLE(tp->trans_entries[i]), "0");
        g_signal_connect(tp->trans_entries[i], "changed",
                         G_CALLBACK(on_entry_changed), tp);
        g_signal_connect(tp->trans_entries[i], "activate",
                         G_CALLBACK(on_entry_activate), tp);
        gtk_grid_attach(GTK_GRID(tp->grid), tp->trans_entries[i], 1, i + 1, 1, 1);
    }

    /* Rotate column (col 2-3) */
    tp->rot_header = gtk_label_new("Rotate");
    {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
        gtk_label_set_attributes(GTK_LABEL(tp->rot_header), attrs);
        pango_attr_list_unref(attrs);
    }
    gtk_grid_attach(GTK_GRID(tp->grid), tp->rot_header, 2, 0, 2, 1);

    for (int i = 0; i < 3; i++) {
        tp->rot_labels[i] = gtk_label_new(axis_labels[i]);
        gtk_widget_set_opacity(tp->rot_labels[i], 0.6);
        gtk_grid_attach(GTK_GRID(tp->grid), tp->rot_labels[i], 2, i + 1, 1, 1);

        tp->rot_entries[i] = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(tp->rot_entries[i]), "0");
        gtk_editable_set_width_chars(GTK_EDITABLE(tp->rot_entries[i]), 8);
        gtk_editable_set_text(GTK_EDITABLE(tp->rot_entries[i]), "0");
        g_signal_connect(tp->rot_entries[i], "changed",
                         G_CALLBACK(on_entry_changed), tp);
        g_signal_connect(tp->rot_entries[i], "activate",
                         G_CALLBACK(on_entry_activate), tp);
        gtk_grid_attach(GTK_GRID(tp->grid), tp->rot_entries[i], 3, i + 1, 1, 1);
    }

    gtk_frame_set_child(GTK_FRAME(tp->container), tp->grid);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "transform panel created");
    return tp;
}

void
dc_transform_panel_free(DC_TransformPanel *tp)
{
    if (!tp) return;
    free(tp->orig_stmt);
    free(tp);
}

GtkWidget *
dc_transform_panel_widget(DC_TransformPanel *tp)
{
    return tp ? tp->container : NULL;
}

void
dc_transform_panel_set_code_editor(DC_TransformPanel *tp, DC_CodeEditor *ed)
{
    if (tp) tp->code_ed = ed;
}

void
dc_transform_panel_show(DC_TransformPanel *tp, const char *stmt_text,
                         int line_start, int line_end)
{
    if (!tp) return;

    tp->line_start = line_start;
    tp->line_end = line_end;

    /* Store original statement */
    free(tp->orig_stmt);
    tp->orig_stmt = stmt_text ? strdup(stmt_text) : NULL;

    /* Parse existing translate/rotate from the statement */
    double tx = 0, ty = 0, tz = 0, rx = 0, ry = 0, rz = 0;
    tp->has_translate = 0;
    tp->has_rotate = 0;

    if (stmt_text) {
        tp->has_translate = parse_transform(stmt_text, "translate", &tx, &ty, &tz);
        tp->has_rotate = parse_transform(stmt_text, "rotate", &rx, &ry, &rz);
    }

    /* If neither detected, don't show panel */
    if (!tp->has_translate && !tp->has_rotate) {
        gtk_widget_set_visible(tp->container, FALSE);
        return;
    }

    tp->values[0] = tx; tp->values[1] = ty; tp->values[2] = tz;
    tp->values[3] = rx; tp->values[4] = ry; tp->values[5] = rz;

    /* Show/hide field groups */
    show_translate_fields(tp, tp->has_translate);
    show_rotate_fields(tp, tp->has_rotate);

    /* Update entries without triggering change handlers */
    tp->updating = 1;
    if (tp->has_translate) {
        for (int i = 0; i < 3; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%g", tp->values[i]);
            gtk_editable_set_text(GTK_EDITABLE(tp->trans_entries[i]), buf);
        }
    }
    if (tp->has_rotate) {
        for (int i = 0; i < 3; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%g", tp->values[3 + i]);
            gtk_editable_set_text(GTK_EDITABLE(tp->rot_entries[i]), buf);
        }
    }
    tp->updating = 0;

    gtk_widget_set_visible(tp->container, TRUE);
}

void
dc_transform_panel_hide(DC_TransformPanel *tp)
{
    if (!tp) return;
    gtk_widget_set_visible(tp->container, FALSE);
}

void
dc_transform_panel_set_enter_callback(DC_TransformPanel *tp,
                                       DC_TransformEnterCb cb, void *userdata)
{
    if (!tp) return;
    tp->enter_cb = cb;
    tp->enter_cb_data = userdata;
}
