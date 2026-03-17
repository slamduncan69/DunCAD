#define _POSIX_C_SOURCE 200809L

#include "eda_library_browser.h"
#include "sch_symbol_render.h"
#include "eda/eda_library.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* qsort comparator for C strings (via pointer-to-pointer) */
static int
cmp_strings(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

/* =========================================================================
 * Three-pane library browser dialog
 *
 * +----------------+------------------+--------------------+
 * | Libraries      | Symbols          | Preview + Info     |
 * | (GtkListBox)   | (GtkListBox)     | (GtkDrawingArea)   |
 * +----------------+------------------+--------------------+
 * | [Search: ____________________________________________] |
 * | [OK] [Cancel]                                         |
 * ========================================================================= */

typedef struct {
    GtkWidget    *dialog;
    GtkWidget    *search;
    GtkWidget    *lib_list;      /* left pane: library names */
    GtkWidget    *sym_list;      /* center pane: symbols in selected lib */
    GtkWidget    *preview_area;  /* right pane: symbol preview */
    GtkWidget    *info_label;    /* right pane: info text below preview */
    DC_ELibrary  *lib;
    char         *selected_lib;  /* currently selected library name — owned */
    char         *result;        /* owned — selected lib_id, or NULL */
    int           done;
    int           searching;     /* 1 if search is active (flat results mode) */
    GMainLoop    *loop;

    /* Currently previewed symbol definition (borrowed) */
    const DC_Sexpr *preview_sym;
} BrowserCtx;

/* Case-insensitive substring match */
static int
str_contains_ci(const char *haystack, const char *needle)
{
    if (!needle || !*needle) return 1;
    if (!haystack) return 0;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return 0;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

/* =========================================================================
 * Populate library list (left pane)
 * ========================================================================= */
static void
populate_lib_list(BrowserCtx *ctx)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ctx->lib_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(ctx->lib_list), child);

    size_t nlibs = dc_elibrary_lib_count(ctx->lib);
    if (nlibs == 0) return;

    /* Collect names and sort alphabetically */
    const char **names = malloc(nlibs * sizeof(const char *));
    if (!names) return;
    for (size_t i = 0; i < nlibs; i++)
        names[i] = dc_elibrary_lib_name(ctx->lib, i);
    qsort(names, nlibs, sizeof(const char *), cmp_strings);

    for (size_t i = 0; i < nlibs; i++) {
        if (!names[i]) continue;
        GtkWidget *label = gtk_label_new(names[i]);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_start(label, 6);
        gtk_widget_set_margin_end(label, 6);
        gtk_widget_set_margin_top(label, 2);
        gtk_widget_set_margin_bottom(label, 2);
        gtk_list_box_append(GTK_LIST_BOX(ctx->lib_list), label);
    }
    free(names);
}

/* =========================================================================
 * Populate symbol list for a given library (center pane)
 * ========================================================================= */
static void
populate_sym_list_for_lib(BrowserCtx *ctx, const char *lib_name)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ctx->sym_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(ctx->sym_list), child);

    if (!lib_name) return;

    size_t count = dc_elibrary_lib_symbol_count(ctx->lib, lib_name);
    if (count == 0) return;
    if (count > 2000) count = 2000;

    /* Collect and sort symbol names */
    const char **names = malloc(count * sizeof(const char *));
    if (!names) return;
    for (size_t i = 0; i < count; i++)
        names[i] = dc_elibrary_lib_symbol_name(ctx->lib, lib_name, i);
    qsort(names, count, sizeof(const char *), cmp_strings);

    for (size_t i = 0; i < count; i++) {
        if (!names[i]) continue;
        GtkWidget *label = gtk_label_new(names[i]);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_start(label, 6);
        gtk_widget_set_margin_end(label, 6);
        gtk_widget_set_margin_top(label, 2);
        gtk_widget_set_margin_bottom(label, 2);
        gtk_list_box_append(GTK_LIST_BOX(ctx->sym_list), label);
    }
    free(names);
}

/* =========================================================================
 * Populate sym list with flat search results across all libs
 * ========================================================================= */
static void
populate_sym_list_search(BrowserCtx *ctx, const char *filter)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ctx->sym_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(ctx->sym_list), child);

    size_t total = dc_elibrary_symbol_count(ctx->lib);
    int added = 0;
    for (size_t i = 0; i < total && added < 500; i++) {
        const char *name = dc_elibrary_symbol_name(ctx->lib, i);
        const char *lname = dc_elibrary_symbol_lib_name(ctx->lib, i);
        if (!name || !lname) continue;

        /* Build "lib:name" for display and filtering */
        size_t llen = strlen(lname);
        size_t nlen = strlen(name);
        char *lib_id = malloc(llen + 1 + nlen + 1);
        if (!lib_id) continue;
        memcpy(lib_id, lname, llen);
        lib_id[llen] = ':';
        memcpy(lib_id + llen + 1, name, nlen + 1);

        if (!str_contains_ci(lib_id, filter)) { free(lib_id); continue; }

        GtkWidget *label = gtk_label_new(lib_id);
        free(lib_id);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_start(label, 6);
        gtk_widget_set_margin_end(label, 6);
        gtk_widget_set_margin_top(label, 2);
        gtk_widget_set_margin_bottom(label, 2);
        gtk_list_box_append(GTK_LIST_BOX(ctx->sym_list), label);
        added++;
    }
}

/* =========================================================================
 * Update preview + info for selected symbol
 * ========================================================================= */
static void
update_preview(BrowserCtx *ctx, const char *lib_id)
{
    ctx->preview_sym = NULL;

    if (lib_id) {
        ctx->preview_sym = dc_elibrary_find_symbol(ctx->lib, lib_id);
        if (!ctx->preview_sym) {
            /* Try name-only lookup */
            const char *colon = strchr(lib_id, ':');
            const char *name = colon ? colon + 1 : lib_id;
            ctx->preview_sym = dc_elibrary_find_symbol_by_name(ctx->lib, name);
        }
    }

    /* Update info label */
    if (ctx->preview_sym) {
        const char *desc = dc_elibrary_symbol_property(ctx->preview_sym, "Description");
        const char *fp = dc_elibrary_symbol_property(ctx->preview_sym, "Footprint");
        size_t pins = dc_elibrary_symbol_pin_count(ctx->preview_sym);

        char info[512];
        snprintf(info, sizeof(info),
                 "Pins: %zu\nFP: %s\nDesc: %s",
                 pins,
                 fp ? fp : "(none)",
                 desc ? desc : "(none)");
        gtk_label_set_text(GTK_LABEL(ctx->info_label), info);
    } else {
        gtk_label_set_text(GTK_LABEL(ctx->info_label), "Select a symbol to preview");
    }

    gtk_widget_queue_draw(ctx->preview_area);
}

/* =========================================================================
 * Callbacks
 * ========================================================================= */
static void
on_lib_selected(GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
    (void)box;
    BrowserCtx *ctx = userdata;
    if (!row || ctx->searching) return;

    GtkWidget *label = gtk_list_box_row_get_child(row);
    if (!label) return;
    const char *lname = gtk_label_get_text(GTK_LABEL(label));

    free(ctx->selected_lib);
    ctx->selected_lib = lname ? strdup(lname) : NULL;

    populate_sym_list_for_lib(ctx, ctx->selected_lib);
    update_preview(ctx, NULL);
}

static void
on_sym_selected(GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
    (void)box;
    BrowserCtx *ctx = userdata;
    if (!row) return;

    GtkWidget *label = gtk_list_box_row_get_child(row);
    if (!label) return;
    const char *text = gtk_label_get_text(GTK_LABEL(label));
    if (!text) return;

    if (ctx->searching) {
        /* text is already "lib:name" */
        update_preview(ctx, text);
    } else if (ctx->selected_lib) {
        /* Build lib:name */
        size_t llen = strlen(ctx->selected_lib);
        size_t nlen = strlen(text);
        char *lib_id = malloc(llen + 1 + nlen + 1);
        if (lib_id) {
            memcpy(lib_id, ctx->selected_lib, llen);
            lib_id[llen] = ':';
            memcpy(lib_id + llen + 1, text, nlen + 1);
            update_preview(ctx, lib_id);
            free(lib_id);
        }
    }
}

static void
on_preview_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                gpointer userdata)
{
    (void)area;
    BrowserCtx *ctx = userdata;

    /* Dark background */
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.14);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    if (ctx->preview_sym) {
        dc_sch_symbol_render_preview_ex(cr, ctx->preview_sym, ctx->lib,
                                         0, 0, (double)width, (double)height);
    } else {
        cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12.0);
        cairo_move_to(cr, 10, height / 2.0);
        cairo_show_text(cr, "No preview");
    }
}

static void
on_search_changed(GtkSearchEntry *entry, gpointer userdata)
{
    BrowserCtx *ctx = userdata;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));

    if (text && *text) {
        ctx->searching = 1;
        populate_sym_list_search(ctx, text);
    } else {
        ctx->searching = 0;
        if (ctx->selected_lib)
            populate_sym_list_for_lib(ctx, ctx->selected_lib);
        else {
            GtkWidget *child;
            while ((child = gtk_widget_get_first_child(ctx->sym_list)) != NULL)
                gtk_list_box_remove(GTK_LIST_BOX(ctx->sym_list), child);
        }
    }
    update_preview(ctx, NULL);
}

/* Build a lib_id result from the currently selected symbol row */
static char *
build_result_from_row(BrowserCtx *ctx, GtkListBoxRow *row)
{
    if (!row) return NULL;
    GtkWidget *label = gtk_list_box_row_get_child(row);
    if (!label) return NULL;
    const char *text = gtk_label_get_text(GTK_LABEL(label));
    if (!text) return NULL;

    if (ctx->searching) {
        return strdup(text);
    } else if (ctx->selected_lib) {
        size_t llen = strlen(ctx->selected_lib);
        size_t nlen = strlen(text);
        char *result = malloc(llen + 1 + nlen + 1);
        if (result) {
            memcpy(result, ctx->selected_lib, llen);
            result[llen] = ':';
            memcpy(result + llen + 1, text, nlen + 1);
        }
        return result;
    }
    return strdup(text);
}

static void
on_ok_clicked(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    BrowserCtx *ctx = userdata;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(ctx->sym_list));
    if (!row) return; /* nothing selected — do nothing */

    free(ctx->result);
    ctx->result = build_result_from_row(ctx, row);
    ctx->done = 1;
    g_main_loop_quit(ctx->loop);
}

static void
on_cancel_clicked(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    BrowserCtx *ctx = userdata;
    free(ctx->result);
    ctx->result = NULL;
    ctx->done = 1;
    g_main_loop_quit(ctx->loop);
}

static gboolean
on_close_request(GtkWindow *win, gpointer userdata)
{
    (void)win;
    BrowserCtx *ctx = userdata;
    free(ctx->result);
    ctx->result = NULL;
    ctx->done = 1;
    g_main_loop_quit(ctx->loop);
    return TRUE;
}

/* =========================================================================
 * Public API
 * ========================================================================= */
char *
dc_eda_library_browser_run(GtkWindow *parent,
                            DC_ELibrary *lib,
                            const char *kind)
{
    if (!lib) return NULL;
    (void)kind; /* TODO: separate footprint browsing via eda_footprint_browser */

    BrowserCtx ctx = {0};
    ctx.lib = lib;
    ctx.loop = g_main_loop_new(NULL, FALSE);

    /* Build dialog window */
    ctx.dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(ctx.dialog), "Symbol Library Browser");
    gtk_window_set_default_size(GTK_WINDOW(ctx.dialog), 900, 600);
    gtk_window_set_modal(GTK_WINDOW(ctx.dialog), TRUE);
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(ctx.dialog), parent);

    g_signal_connect(ctx.dialog, "close-request",
                     G_CALLBACK(on_close_request), &ctx);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);

    /* ---- Three-pane horizontal layout ---- */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_vexpand(hbox, TRUE);

    /* Left pane: library list */
    GtkWidget *lib_frame = gtk_frame_new("Libraries");
    GtkWidget *lib_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(lib_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(lib_scroll, 180, -1);
    ctx.lib_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx.lib_list), GTK_SELECTION_SINGLE);
    g_signal_connect(ctx.lib_list, "row-selected", G_CALLBACK(on_lib_selected), &ctx);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(lib_scroll), ctx.lib_list);
    gtk_frame_set_child(GTK_FRAME(lib_frame), lib_scroll);
    gtk_box_append(GTK_BOX(hbox), lib_frame);

    /* Center pane: symbol list */
    GtkWidget *sym_frame = gtk_frame_new("Symbols");
    GtkWidget *sym_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sym_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(sym_scroll, 200, -1);
    gtk_widget_set_hexpand(sym_scroll, TRUE);
    ctx.sym_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx.sym_list), GTK_SELECTION_SINGLE);
    g_signal_connect(ctx.sym_list, "row-selected", G_CALLBACK(on_sym_selected), &ctx);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sym_scroll), ctx.sym_list);
    gtk_frame_set_child(GTK_FRAME(sym_frame), sym_scroll);
    gtk_box_append(GTK_BOX(hbox), sym_frame);

    /* Right pane: preview + info */
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(right_box, 250, -1);

    GtkWidget *preview_frame = gtk_frame_new("Preview");
    ctx.preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(ctx.preview_area, 240, 200);
    gtk_widget_set_vexpand(ctx.preview_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ctx.preview_area),
                                    on_preview_draw, &ctx, NULL);
    gtk_frame_set_child(GTK_FRAME(preview_frame), ctx.preview_area);
    gtk_box_append(GTK_BOX(right_box), preview_frame);

    GtkWidget *info_frame = gtk_frame_new("Info");
    ctx.info_label = gtk_label_new("Select a symbol to preview");
    gtk_label_set_xalign(GTK_LABEL(ctx.info_label), 0.0);
    gtk_label_set_wrap(GTK_LABEL(ctx.info_label), TRUE);
    gtk_widget_set_margin_start(ctx.info_label, 6);
    gtk_widget_set_margin_end(ctx.info_label, 6);
    gtk_widget_set_margin_top(ctx.info_label, 4);
    gtk_widget_set_margin_bottom(ctx.info_label, 4);
    gtk_frame_set_child(GTK_FRAME(info_frame), ctx.info_label);
    gtk_box_append(GTK_BOX(right_box), info_frame);

    gtk_box_append(GTK_BOX(hbox), right_box);
    gtk_box_append(GTK_BOX(vbox), hbox);

    /* ---- Search entry ---- */
    ctx.search = gtk_search_entry_new();
    gtk_widget_set_hexpand(ctx.search, TRUE);
    g_signal_connect(ctx.search, "search-changed",
                     G_CALLBACK(on_search_changed), &ctx);
    gtk_box_append(GTK_BOX(vbox), ctx.search);

    /* ---- Button bar ---- */
    GtkWidget *btn_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_bar, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btn_bar, 4);

    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *btn_ok = gtk_button_new_with_label("OK");
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_cancel_clicked), &ctx);
    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_ok_clicked), &ctx);
    gtk_box_append(GTK_BOX(btn_bar), btn_cancel);
    gtk_box_append(GTK_BOX(btn_bar), btn_ok);
    gtk_box_append(GTK_BOX(vbox), btn_bar);

    gtk_window_set_child(GTK_WINDOW(ctx.dialog), vbox);

    /* Populate library list */
    populate_lib_list(&ctx);

    /* Show and run nested main loop */
    gtk_window_present(GTK_WINDOW(ctx.dialog));
    g_main_loop_run(ctx.loop);
    g_main_loop_unref(ctx.loop);

    gtk_window_destroy(GTK_WINDOW(ctx.dialog));

    free(ctx.selected_lib);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Library browser: selected %s",
           ctx.result ? ctx.result : "(cancelled)");
    return ctx.result;
}
