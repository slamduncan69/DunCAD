#define _POSIX_C_SOURCE 200809L

#include "eda_footprint_browser.h"
#include "pcb_footprint_render.h"
#include "eda/eda_library.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * Three-pane footprint browser — same pattern as symbol browser.
 * ========================================================================= */

typedef struct {
    GtkWidget    *dialog;
    GtkWidget    *search;
    GtkWidget    *lib_list;
    GtkWidget    *fp_list;
    GtkWidget    *preview_area;
    GtkWidget    *info_label;
    DC_ELibrary  *lib;
    char         *selected_lib;
    char         *result;
    int           done;
    int           searching;
    GMainLoop    *loop;
    const DC_Sexpr *preview_fp;
} FPBrowserCtx;

/* Case-insensitive substring match */
static int
fp_str_contains_ci(const char *haystack, const char *needle)
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
 * Populate library list — distinct footprint library names
 * ========================================================================= */
static void
populate_fp_lib_list(FPBrowserCtx *ctx)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ctx->lib_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(ctx->lib_list), child);

    /* Collect distinct library names from footprints */
    size_t count = dc_elibrary_footprint_count(ctx->lib);
    /* Simple O(n^2) distinct — footprint libs are typically small */
    for (size_t i = 0; i < count; i++) {
        const char *lname = dc_elibrary_footprint_lib_name(ctx->lib, i);
        if (!lname) continue;

        /* Check if already added */
        int dup = 0;
        for (size_t j = 0; j < i; j++) {
            const char *prev = dc_elibrary_footprint_lib_name(ctx->lib, j);
            if (prev && strcmp(lname, prev) == 0) { dup = 1; break; }
        }
        if (dup) continue;

        GtkWidget *label = gtk_label_new(lname);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_start(label, 6);
        gtk_widget_set_margin_end(label, 6);
        gtk_widget_set_margin_top(label, 2);
        gtk_widget_set_margin_bottom(label, 2);
        gtk_list_box_append(GTK_LIST_BOX(ctx->lib_list), label);
    }
}

/* Populate footprint list for a given library */
static void
populate_fp_list_for_lib(FPBrowserCtx *ctx, const char *lib_name)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ctx->fp_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(ctx->fp_list), child);

    if (!lib_name) return;

    size_t count = dc_elibrary_footprint_count(ctx->lib);
    int added = 0;
    for (size_t i = 0; i < count && added < 2000; i++) {
        const char *lname = dc_elibrary_footprint_lib_name(ctx->lib, i);
        const char *name = dc_elibrary_footprint_name(ctx->lib, i);
        if (!lname || !name) continue;
        if (strcmp(lname, lib_name) != 0) continue;

        GtkWidget *label = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_start(label, 6);
        gtk_widget_set_margin_end(label, 6);
        gtk_widget_set_margin_top(label, 2);
        gtk_widget_set_margin_bottom(label, 2);
        gtk_list_box_append(GTK_LIST_BOX(ctx->fp_list), label);
        added++;
    }
}

/* Flat search across all footprint libs */
static void
populate_fp_list_search(FPBrowserCtx *ctx, const char *filter)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ctx->fp_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(ctx->fp_list), child);

    size_t count = dc_elibrary_footprint_count(ctx->lib);
    int added = 0;
    for (size_t i = 0; i < count && added < 500; i++) {
        const char *name = dc_elibrary_footprint_name(ctx->lib, i);
        const char *lname = dc_elibrary_footprint_lib_name(ctx->lib, i);
        if (!name || !lname) continue;

        size_t llen = strlen(lname);
        size_t nlen = strlen(name);
        char *lib_id = malloc(llen + 1 + nlen + 1);
        if (!lib_id) continue;
        memcpy(lib_id, lname, llen);
        lib_id[llen] = ':';
        memcpy(lib_id + llen + 1, name, nlen + 1);

        if (!fp_str_contains_ci(lib_id, filter)) { free(lib_id); continue; }

        GtkWidget *label = gtk_label_new(lib_id);
        free(lib_id);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_start(label, 6);
        gtk_widget_set_margin_end(label, 6);
        gtk_widget_set_margin_top(label, 2);
        gtk_widget_set_margin_bottom(label, 2);
        gtk_list_box_append(GTK_LIST_BOX(ctx->fp_list), label);
        added++;
    }
}

/* Update preview + info */
static void
update_fp_preview(FPBrowserCtx *ctx, const char *lib_id)
{
    ctx->preview_fp = NULL;

    if (lib_id) {
        ctx->preview_fp = dc_elibrary_find_footprint(ctx->lib, lib_id);
    }

    if (ctx->preview_fp) {
        /* Count pads */
        size_t pad_count = 0;
        DC_Sexpr **pads = dc_sexpr_find_all(ctx->preview_fp, "pad", &pad_count);
        free(pads);

        const char *desc = NULL;
        DC_Sexpr *desc_node = dc_sexpr_find(ctx->preview_fp, "descr");
        if (desc_node) desc = dc_sexpr_value(desc_node);

        const char *tags = NULL;
        DC_Sexpr *tags_node = dc_sexpr_find(ctx->preview_fp, "tags");
        if (tags_node) tags = dc_sexpr_value(tags_node);

        char info[512];
        snprintf(info, sizeof(info),
                 "Pads: %zu\nTags: %s\nDesc: %s",
                 pad_count,
                 tags ? tags : "(none)",
                 desc ? desc : "(none)");
        gtk_label_set_text(GTK_LABEL(ctx->info_label), info);
    } else {
        gtk_label_set_text(GTK_LABEL(ctx->info_label), "Select a footprint to preview");
    }

    gtk_widget_queue_draw(ctx->preview_area);
}

/* =========================================================================
 * Callbacks
 * ========================================================================= */
static void
on_fp_lib_selected(GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
    (void)box;
    FPBrowserCtx *ctx = userdata;
    if (!row || ctx->searching) return;

    GtkWidget *label = gtk_list_box_row_get_child(row);
    if (!label) return;
    const char *lname = gtk_label_get_text(GTK_LABEL(label));

    free(ctx->selected_lib);
    ctx->selected_lib = lname ? strdup(lname) : NULL;

    populate_fp_list_for_lib(ctx, ctx->selected_lib);
    update_fp_preview(ctx, NULL);
}

static void
on_fp_selected(GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
    (void)box;
    FPBrowserCtx *ctx = userdata;
    if (!row) return;

    GtkWidget *label = gtk_list_box_row_get_child(row);
    if (!label) return;
    const char *text = gtk_label_get_text(GTK_LABEL(label));
    if (!text) return;

    if (ctx->searching) {
        update_fp_preview(ctx, text);
    } else if (ctx->selected_lib) {
        size_t llen = strlen(ctx->selected_lib);
        size_t nlen = strlen(text);
        char *lib_id = malloc(llen + 1 + nlen + 1);
        if (lib_id) {
            memcpy(lib_id, ctx->selected_lib, llen);
            lib_id[llen] = ':';
            memcpy(lib_id + llen + 1, text, nlen + 1);
            update_fp_preview(ctx, lib_id);
            free(lib_id);
        }
    }
}

static void
on_fp_preview_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                   gpointer userdata)
{
    (void)area;
    FPBrowserCtx *ctx = userdata;

    cairo_set_source_rgb(cr, 0.1, 0.15, 0.1);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    if (ctx->preview_fp) {
        dc_pcb_footprint_render_preview(cr, ctx->preview_fp,
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
on_fp_search_changed(GtkSearchEntry *entry, gpointer userdata)
{
    FPBrowserCtx *ctx = userdata;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));

    if (text && *text) {
        ctx->searching = 1;
        populate_fp_list_search(ctx, text);
    } else {
        ctx->searching = 0;
        if (ctx->selected_lib)
            populate_fp_list_for_lib(ctx, ctx->selected_lib);
        else {
            GtkWidget *child;
            while ((child = gtk_widget_get_first_child(ctx->fp_list)) != NULL)
                gtk_list_box_remove(GTK_LIST_BOX(ctx->fp_list), child);
        }
    }
    update_fp_preview(ctx, NULL);
}

static void
on_fp_activated(GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
    (void)box;
    FPBrowserCtx *ctx = userdata;
    if (!row) return;

    GtkWidget *label = gtk_list_box_row_get_child(row);
    if (!label) return;
    const char *text = gtk_label_get_text(GTK_LABEL(label));
    if (!text) return;

    free(ctx->result);
    if (ctx->searching) {
        ctx->result = strdup(text);
    } else if (ctx->selected_lib) {
        size_t llen = strlen(ctx->selected_lib);
        size_t nlen = strlen(text);
        ctx->result = malloc(llen + 1 + nlen + 1);
        if (ctx->result) {
            memcpy(ctx->result, ctx->selected_lib, llen);
            ctx->result[llen] = ':';
            memcpy(ctx->result + llen + 1, text, nlen + 1);
        }
    } else {
        ctx->result = strdup(text);
    }

    ctx->done = 1;
    g_main_loop_quit(ctx->loop);
}

static void
on_fp_ok_clicked(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    FPBrowserCtx *ctx = userdata;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(ctx->fp_list));
    if (row)
        on_fp_activated(GTK_LIST_BOX(ctx->fp_list), row, ctx);
}

static void
on_fp_cancel_clicked(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    FPBrowserCtx *ctx = userdata;
    free(ctx->result);
    ctx->result = NULL;
    ctx->done = 1;
    g_main_loop_quit(ctx->loop);
}

static gboolean
on_fp_close_request(GtkWindow *win, gpointer userdata)
{
    (void)win;
    FPBrowserCtx *ctx = userdata;
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
dc_eda_footprint_browser_run(GtkWindow *parent, DC_ELibrary *lib)
{
    if (!lib) return NULL;

    FPBrowserCtx ctx = {0};
    ctx.lib = lib;
    ctx.loop = g_main_loop_new(NULL, FALSE);

    ctx.dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(ctx.dialog), "Footprint Library Browser");
    gtk_window_set_default_size(GTK_WINDOW(ctx.dialog), 900, 600);
    gtk_window_set_modal(GTK_WINDOW(ctx.dialog), TRUE);
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(ctx.dialog), parent);

    g_signal_connect(ctx.dialog, "close-request",
                     G_CALLBACK(on_fp_close_request), &ctx);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);

    /* Three-pane layout */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_vexpand(hbox, TRUE);

    /* Left: library list */
    GtkWidget *lib_frame = gtk_frame_new("Libraries");
    GtkWidget *lib_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(lib_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(lib_scroll, 200, -1);
    ctx.lib_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx.lib_list), GTK_SELECTION_SINGLE);
    g_signal_connect(ctx.lib_list, "row-selected", G_CALLBACK(on_fp_lib_selected), &ctx);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(lib_scroll), ctx.lib_list);
    gtk_frame_set_child(GTK_FRAME(lib_frame), lib_scroll);
    gtk_box_append(GTK_BOX(hbox), lib_frame);

    /* Center: footprint list */
    GtkWidget *fp_frame = gtk_frame_new("Footprints");
    GtkWidget *fp_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(fp_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(fp_scroll, 220, -1);
    gtk_widget_set_hexpand(fp_scroll, TRUE);
    ctx.fp_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx.fp_list), GTK_SELECTION_SINGLE);
    g_signal_connect(ctx.fp_list, "row-selected", G_CALLBACK(on_fp_selected), &ctx);
    g_signal_connect(ctx.fp_list, "row-activated", G_CALLBACK(on_fp_activated), &ctx);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(fp_scroll), ctx.fp_list);
    gtk_frame_set_child(GTK_FRAME(fp_frame), fp_scroll);
    gtk_box_append(GTK_BOX(hbox), fp_frame);

    /* Right: preview + info */
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(right_box, 250, -1);

    GtkWidget *preview_frame = gtk_frame_new("Preview");
    ctx.preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(ctx.preview_area, 240, 200);
    gtk_widget_set_vexpand(ctx.preview_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ctx.preview_area),
                                    on_fp_preview_draw, &ctx, NULL);
    gtk_frame_set_child(GTK_FRAME(preview_frame), ctx.preview_area);
    gtk_box_append(GTK_BOX(right_box), preview_frame);

    GtkWidget *info_frame = gtk_frame_new("Info");
    ctx.info_label = gtk_label_new("Select a footprint to preview");
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

    /* Search entry */
    ctx.search = gtk_search_entry_new();
    gtk_widget_set_hexpand(ctx.search, TRUE);
    g_signal_connect(ctx.search, "search-changed",
                     G_CALLBACK(on_fp_search_changed), &ctx);
    gtk_box_append(GTK_BOX(vbox), ctx.search);

    /* Button bar */
    GtkWidget *btn_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_bar, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btn_bar, 4);

    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *btn_ok = gtk_button_new_with_label("OK");
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_fp_cancel_clicked), &ctx);
    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_fp_ok_clicked), &ctx);
    gtk_box_append(GTK_BOX(btn_bar), btn_cancel);
    gtk_box_append(GTK_BOX(btn_bar), btn_ok);
    gtk_box_append(GTK_BOX(vbox), btn_bar);

    gtk_window_set_child(GTK_WINDOW(ctx.dialog), vbox);

    populate_fp_lib_list(&ctx);

    gtk_window_present(GTK_WINDOW(ctx.dialog));
    g_main_loop_run(ctx.loop);
    g_main_loop_unref(ctx.loop);

    gtk_window_destroy(GTK_WINDOW(ctx.dialog));
    free(ctx.selected_lib);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Footprint browser: selected %s",
           ctx.result ? ctx.result : "(cancelled)");
    return ctx.result;
}
