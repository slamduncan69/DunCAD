#define _POSIX_C_SOURCE 200809L

#include "eda_library_browser.h"
#include "eda/eda_library.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * Library browser dialog
 *
 * Simple approach: GtkWindow with GtkSearchEntry + GtkListBox.
 * GtkListBox rows are GtkLabels with lib_id strings.
 * Filtering via search text. Selection returns lib_id.
 * ========================================================================= */

typedef struct {
    GtkWidget    *dialog;
    GtkWidget    *search;
    GtkWidget    *list_box;
    DC_ELibrary  *lib;
    char         *result;       /* owned — selected lib_id, or NULL */
    int           done;
    GMainLoop    *loop;
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

static void
populate_list(BrowserCtx *ctx, const char *filter)
{
    /* Remove all existing children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ctx->list_box)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(ctx->list_box), child);

    size_t count = dc_elibrary_symbol_count(ctx->lib);
    int added = 0;
    for (size_t i = 0; i < count && added < 500; i++) {
        const char *name = dc_elibrary_symbol_name(ctx->lib, i);
        if (!name) continue;
        if (filter && *filter && !str_contains_ci(name, filter)) continue;

        GtkWidget *label = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_start(label, 8);
        gtk_widget_set_margin_end(label, 8);
        gtk_widget_set_margin_top(label, 2);
        gtk_widget_set_margin_bottom(label, 2);
        gtk_list_box_append(GTK_LIST_BOX(ctx->list_box), label);
        added++;
    }
}

static void
on_search_changed(GtkSearchEntry *entry, gpointer userdata)
{
    BrowserCtx *ctx = userdata;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    populate_list(ctx, text);
}

static void
on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
    (void)box;
    BrowserCtx *ctx = userdata;
    if (!row) return;

    GtkWidget *label = gtk_list_box_row_get_child(row);
    if (!label) return;
    const char *text = gtk_label_get_text(GTK_LABEL(label));
    if (text) {
        free(ctx->result);
        ctx->result = strdup(text);
    }
    ctx->done = 1;
    g_main_loop_quit(ctx->loop);
}

static void
on_ok_clicked(GtkButton *btn, gpointer userdata)
{
    (void)btn;
    BrowserCtx *ctx = userdata;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(ctx->list_box));
    if (row) {
        GtkWidget *label = gtk_list_box_row_get_child(row);
        if (label) {
            const char *text = gtk_label_get_text(GTK_LABEL(label));
            if (text) {
                free(ctx->result);
                ctx->result = strdup(text);
            }
        }
    }
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
    return TRUE; /* prevent default close — we destroy manually */
}

char *
dc_eda_library_browser_run(GtkWindow *parent,
                            DC_ELibrary *lib,
                            const char *kind)
{
    if (!lib) return NULL;
    (void)kind; /* TODO: separate footprint browsing */

    BrowserCtx ctx = {0};
    ctx.lib = lib;
    ctx.loop = g_main_loop_new(NULL, FALSE);

    /* Build dialog window */
    ctx.dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(ctx.dialog), "Library Browser — Select Symbol");
    gtk_window_set_default_size(GTK_WINDOW(ctx.dialog), 500, 600);
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

    /* Search entry */
    ctx.search = gtk_search_entry_new();
    gtk_widget_set_hexpand(ctx.search, TRUE);
    g_signal_connect(ctx.search, "search-changed",
                     G_CALLBACK(on_search_changed), &ctx);
    gtk_box_append(GTK_BOX(vbox), ctx.search);

    /* Scrolled list */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);

    ctx.list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx.list_box),
                                     GTK_SELECTION_SINGLE);
    g_signal_connect(ctx.list_box, "row-activated",
                     G_CALLBACK(on_row_activated), &ctx);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), ctx.list_box);
    gtk_box_append(GTK_BOX(vbox), scrolled);

    /* Button bar */
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

    /* Populate with all symbols */
    populate_list(&ctx, NULL);

    /* Show and run a nested main loop (modal behavior) */
    gtk_window_present(GTK_WINDOW(ctx.dialog));

    g_main_loop_run(ctx.loop);
    g_main_loop_unref(ctx.loop);

    gtk_window_destroy(GTK_WINDOW(ctx.dialog));

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Library browser: selected %s",
           ctx.result ? ctx.result : "(cancelled)");
    return ctx.result;
}
