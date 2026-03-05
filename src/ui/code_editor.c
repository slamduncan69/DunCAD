#define _POSIX_C_SOURCE 200809L
#include "ui/code_editor.h"
#include "core/log.h"

#include <gtksourceview/gtksource.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */
struct DC_CodeEditor {
    GtkWidget        *container;    /* GtkBox(V): toolbar + scrolled view */
    GtkSourceView    *view;
    GtkSourceBuffer  *buffer;
    GtkWidget        *window;       /* borrowed, for file dialogs */
    char             *file_path;    /* owned, NULL if untitled */
    GtkWidget        *path_label;   /* shows filename in toolbar */
};

/* -------------------------------------------------------------------------
 * Language setup — register custom OpenSCAD .lang from data dir
 * ---------------------------------------------------------------------- */
static GtkSourceLanguage *
find_openscad_language(void)
{
    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();

    /* Try built-in first */
    GtkSourceLanguage *lang = gtk_source_language_manager_get_language(lm, "openscad");
    if (lang) return lang;

    /* Add our data/language-specs directory */
    const char * const *old_dirs = gtk_source_language_manager_get_search_path(lm);
    GPtrArray *dirs = g_ptr_array_new();
    if (old_dirs) {
        for (int i = 0; old_dirs[i]; i++)
            g_ptr_array_add(dirs, (gpointer)old_dirs[i]);
    }

    /* Find our data dir relative to the executable.
     * In development: <build>/bin/duncad → <source>/data/language-specs
     * We use a compile-time definition for the source dir. */
#ifdef DC_SOURCE_DIR
    char custom_dir[1024];
    snprintf(custom_dir, sizeof(custom_dir), "%s/data/language-specs", DC_SOURCE_DIR);
    g_ptr_array_add(dirs, custom_dir);
#endif

    g_ptr_array_add(dirs, NULL);
    gtk_source_language_manager_set_search_path(
        lm, (const char * const *)dirs->pdata);
    g_ptr_array_free(dirs, TRUE);

    lang = gtk_source_language_manager_get_language(lm, "openscad");
    if (lang)
        dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "code_editor: loaded openscad.lang");
    else
        dc_log(DC_LOG_WARN, DC_LOG_EVENT_APP, "code_editor: openscad.lang not found");

    return lang;
}

/* -------------------------------------------------------------------------
 * Apply dark color scheme
 * ---------------------------------------------------------------------- */
static void
apply_dark_scheme(GtkSourceBuffer *buffer)
{
    GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
    /* Try Adwaita-dark first, then others */
    const char *schemes[] = {
        "Adwaita-dark", "classic-dark", "cobalt", "solarized-dark", NULL
    };
    for (int i = 0; schemes[i]; i++) {
        GtkSourceStyleScheme *scheme =
            gtk_source_style_scheme_manager_get_scheme(sm, schemes[i]);
        if (scheme) {
            gtk_source_buffer_set_style_scheme(buffer, scheme);
            return;
        }
    }
}

/* -------------------------------------------------------------------------
 * Toolbar button callbacks
 * ---------------------------------------------------------------------- */
static void
on_open_response(GObject *source, GAsyncResult *result, gpointer data)
{
    DC_CodeEditor *ed = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);

    GError *err = NULL;
    GFile *file = gtk_file_dialog_open_finish(dialog, result, &err);
    if (!file) {
        if (err) g_error_free(err);
        return;
    }

    char *path = g_file_get_path(file);
    g_object_unref(file);
    if (path) {
        dc_code_editor_open_file(ed, path);
        g_free(path);
    }
}

static void
on_open_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    DC_CodeEditor *ed = data;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open SCAD File");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "OpenSCAD files (*.scad)");
    gtk_file_filter_add_pattern(filter, "*.scad");
    g_list_store_append(filters, filter);
    g_object_unref(filter);

    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    g_list_store_append(filters, all);
    g_object_unref(all);

    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filters);

    GtkWindow *win = ed->window ? GTK_WINDOW(ed->window) : NULL;
    gtk_file_dialog_open(dialog, win, NULL, on_open_response, ed);
    g_object_unref(dialog);
}

static void
on_save_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    DC_CodeEditor *ed = data;

    if (ed->file_path) {
        dc_code_editor_save(ed);
    } else {
        /* No path yet — trigger Save As */
        GtkFileDialog *dialog = gtk_file_dialog_new();
        gtk_file_dialog_set_title(dialog, "Save SCAD File");
        gtk_file_dialog_set_initial_name(dialog, "untitled.scad");

        GtkWindow *win = ed->window ? GTK_WINDOW(ed->window) : NULL;
        gtk_file_dialog_save(dialog, win, NULL,
            (GAsyncReadyCallback)on_open_response, ed);
        g_object_unref(dialog);
    }
}

static void
on_save_as_response(GObject *source, GAsyncResult *result, gpointer data)
{
    DC_CodeEditor *ed = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);

    GError *err = NULL;
    GFile *file = gtk_file_dialog_save_finish(dialog, result, &err);
    if (!file) {
        if (err) g_error_free(err);
        return;
    }

    char *path = g_file_get_path(file);
    g_object_unref(file);
    if (path) {
        dc_code_editor_save_as(ed, path);
        g_free(path);
    }
}

static void
on_save_as_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    DC_CodeEditor *ed = data;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save SCAD File As");
    if (ed->file_path) {
        const char *base = strrchr(ed->file_path, '/');
        base = base ? base + 1 : ed->file_path;
        gtk_file_dialog_set_initial_name(dialog, base);
    } else {
        gtk_file_dialog_set_initial_name(dialog, "untitled.scad");
    }

    GtkWindow *win = ed->window ? GTK_WINDOW(ed->window) : NULL;
    gtk_file_dialog_save(dialog, win, NULL, on_save_as_response, ed);
    g_object_unref(dialog);
}

/* -------------------------------------------------------------------------
 * Update path label
 * ---------------------------------------------------------------------- */
static void
update_path_label(DC_CodeEditor *ed)
{
    if (!ed->path_label) return;
    if (ed->file_path) {
        const char *base = strrchr(ed->file_path, '/');
        base = base ? base + 1 : ed->file_path;
        gtk_label_set_text(GTK_LABEL(ed->path_label), base);
    } else {
        gtk_label_set_text(GTK_LABEL(ed->path_label), "untitled.scad");
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

DC_CodeEditor *
dc_code_editor_new(void)
{
    DC_CodeEditor *ed = calloc(1, sizeof(*ed));
    if (!ed) return NULL;

    /* Create source buffer with OpenSCAD language */
    GtkSourceLanguage *lang = find_openscad_language();
    ed->buffer = gtk_source_buffer_new(NULL);
    if (lang)
        gtk_source_buffer_set_language(ed->buffer, lang);
    gtk_source_buffer_set_highlight_syntax(ed->buffer, TRUE);

    apply_dark_scheme(ed->buffer);

    /* Create source view */
    ed->view = GTK_SOURCE_VIEW(
        gtk_source_view_new_with_buffer(ed->buffer));
    gtk_source_view_set_show_line_numbers(ed->view, TRUE);
    gtk_source_view_set_tab_width(ed->view, 4);
    gtk_source_view_set_insert_spaces_instead_of_tabs(ed->view, TRUE);
    gtk_source_view_set_auto_indent(ed->view, TRUE);
    gtk_source_view_set_highlight_current_line(ed->view, TRUE);

    /* Monospace font */
    PangoFontDescription *font = pango_font_description_from_string("Monospace 11");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_font_desc_new(font));
    gtk_widget_set_css_classes(GTK_WIDGET(ed->view), (const char *[]){"monospace", NULL});
    pango_attr_list_unref(attrs);
    pango_font_description_free(font);

    /* Scrolled window for the source view */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_WIDGET(ed->view));
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);
    gtk_widget_set_margin_top(toolbar, 2);
    gtk_widget_set_margin_bottom(toolbar, 2);

    GtkWidget *open_btn = gtk_button_new_with_label("Open");
    gtk_widget_set_focusable(open_btn, FALSE);
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_clicked), ed);
    gtk_box_append(GTK_BOX(toolbar), open_btn);

    GtkWidget *save_btn = gtk_button_new_with_label("Save");
    gtk_widget_set_focusable(save_btn, FALSE);
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), ed);
    gtk_box_append(GTK_BOX(toolbar), save_btn);

    GtkWidget *save_as_btn = gtk_button_new_with_label("Save As");
    gtk_widget_set_focusable(save_as_btn, FALSE);
    g_signal_connect(save_as_btn, "clicked", G_CALLBACK(on_save_as_clicked), ed);
    gtk_box_append(GTK_BOX(toolbar), save_as_btn);

    /* Separator + filename label */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(toolbar), sep);

    ed->path_label = gtk_label_new("untitled.scad");
    gtk_label_set_xalign(GTK_LABEL(ed->path_label), 0.0f);
    gtk_widget_set_hexpand(ed->path_label, TRUE);
    gtk_widget_set_opacity(ed->path_label, 0.7);
    gtk_box_append(GTK_BOX(toolbar), ed->path_label);

    /* Container */
    ed->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(ed->container), toolbar);
    gtk_box_append(GTK_BOX(ed->container), scroll);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "code editor created");
    return ed;
}

void
dc_code_editor_free(DC_CodeEditor *ed)
{
    if (!ed) return;
    free(ed->file_path);
    /* GtkSourceBuffer and View are owned by GTK widget hierarchy */
    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "code editor freed");
    free(ed);
}

GtkWidget *
dc_code_editor_widget(DC_CodeEditor *ed)
{
    return ed ? ed->container : NULL;
}

char *
dc_code_editor_get_text(DC_CodeEditor *ed)
{
    if (!ed) return NULL;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(ed->buffer), &start, &end);
    char *text = gtk_text_buffer_get_text(
        GTK_TEXT_BUFFER(ed->buffer), &start, &end, FALSE);
    /* gtk_text_buffer_get_text returns g_malloc'd string — copy to stdlib */
    char *result = strdup(text);
    g_free(text);
    return result;
}

void
dc_code_editor_set_text(DC_CodeEditor *ed, const char *text)
{
    if (!ed) return;
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(ed->buffer),
                              text ? text : "", -1);
}

int
dc_code_editor_open_file(DC_CodeEditor *ed, const char *path)
{
    if (!ed || !path) return -1;

    FILE *f = fopen(path, "r");
    if (!f) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "code_editor: cannot open %s", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return -1; }

    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);

    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(ed->buffer), buf, (int)n);
    free(buf);

    free(ed->file_path);
    ed->file_path = strdup(path);
    update_path_label(ed);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "code_editor: opened %s (%ld bytes)", path, size);
    return 0;
}

int
dc_code_editor_save(DC_CodeEditor *ed)
{
    if (!ed || !ed->file_path) return -1;
    return dc_code_editor_save_as(ed, ed->file_path);
}

int
dc_code_editor_save_as(DC_CodeEditor *ed, const char *path)
{
    if (!ed || !path) return -1;

    char *text = dc_code_editor_get_text(ed);
    if (!text) return -1;

    FILE *f = fopen(path, "w");
    if (!f) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "code_editor: cannot write %s", path);
        free(text);
        return -1;
    }

    fputs(text, f);
    fclose(f);
    free(text);

    free(ed->file_path);
    ed->file_path = strdup(path);
    update_path_label(ed);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "code_editor: saved %s", path);
    return 0;
}

const char *
dc_code_editor_get_path(const DC_CodeEditor *ed)
{
    return ed ? ed->file_path : NULL;
}

void
dc_code_editor_set_window(DC_CodeEditor *ed, GtkWidget *window)
{
    if (ed) ed->window = window;
}
