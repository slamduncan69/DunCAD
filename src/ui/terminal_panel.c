#define _POSIX_C_SOURCE 200809L
#include "terminal_panel.h"
#include "core/log.h"

#include <string.h>
#include <stdlib.h>

struct DC_TerminalPanel {
    GtkWidget       *box;        /* top-level container */
    GtkWidget       *scroll;     /* scrolled window */
    GtkWidget       *text_view;  /* output display */
    GtkTextBuffer   *buffer;
    GtkWidget       *entry;      /* command input */
    DC_TerminalCmdCb cmd_cb;
    void            *cmd_cb_data;
    /* Command history */
    char           **history;
    int              hist_count;
    int              hist_cap;
    int              hist_pos;   /* -1 = current input, 0..n = history */
    char            *saved_input; /* saved current input when browsing history */
};

/* ---- Scroll to bottom ---- */
static void
scroll_to_bottom(DC_TerminalPanel *tp)
{
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(tp->buffer, &end);
    GtkTextMark *mark = gtk_text_buffer_get_mark(tp->buffer, "end-mark");
    if (!mark)
        mark = gtk_text_buffer_create_mark(tp->buffer, "end-mark", &end, FALSE);
    else
        gtk_text_buffer_move_mark(tp->buffer, mark, &end);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(tp->text_view), mark);
}

/* ---- History management ---- */
static void
history_push(DC_TerminalPanel *tp, const char *cmd)
{
    if (tp->hist_count >= tp->hist_cap) {
        tp->hist_cap = tp->hist_cap ? tp->hist_cap * 2 : 64;
        tp->history = realloc(tp->history, (size_t)tp->hist_cap * sizeof(char *));
    }
    tp->history[tp->hist_count++] = strdup(cmd);
}

/* ---- Entry activate (Enter pressed) ---- */
static void
on_entry_activate(GtkEntry *entry, gpointer data)
{
    DC_TerminalPanel *tp = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!text || !*text) return;

    /* Echo command to output */
    char *echo = malloc(strlen(text) + 4);
    if (echo) {
        sprintf(echo, "> %s\n", text);
        dc_terminal_panel_append(tp, echo);
        free(echo);
    }

    /* Save to history */
    history_push(tp, text);
    tp->hist_pos = -1;
    free(tp->saved_input);
    tp->saved_input = NULL;

    /* Fire callback */
    if (tp->cmd_cb)
        tp->cmd_cb(text, tp->cmd_cb_data);

    /* Clear input */
    gtk_editable_set_text(GTK_EDITABLE(entry), "");
}

/* ---- Key handler for Up/Down history ---- */
static gboolean
on_entry_key(GtkEventControllerKey *ctrl, guint keyval,
             guint keycode, GdkModifierType state, gpointer data)
{
    (void)ctrl; (void)keycode; (void)state;
    DC_TerminalPanel *tp = data;

    if (keyval == GDK_KEY_Up) {
        if (tp->hist_count == 0) return TRUE;
        if (tp->hist_pos == -1) {
            /* Save current input */
            free(tp->saved_input);
            tp->saved_input = strdup(
                gtk_editable_get_text(GTK_EDITABLE(tp->entry)));
            tp->hist_pos = tp->hist_count - 1;
        } else if (tp->hist_pos > 0) {
            tp->hist_pos--;
        }
        gtk_editable_set_text(GTK_EDITABLE(tp->entry),
                              tp->history[tp->hist_pos]);
        gtk_editable_set_position(GTK_EDITABLE(tp->entry), -1);
        return TRUE;
    }

    if (keyval == GDK_KEY_Down) {
        if (tp->hist_pos == -1) return TRUE;
        if (tp->hist_pos < tp->hist_count - 1) {
            tp->hist_pos++;
            gtk_editable_set_text(GTK_EDITABLE(tp->entry),
                                  tp->history[tp->hist_pos]);
        } else {
            /* Restore saved input */
            tp->hist_pos = -1;
            gtk_editable_set_text(GTK_EDITABLE(tp->entry),
                                  tp->saved_input ? tp->saved_input : "");
            free(tp->saved_input);
            tp->saved_input = NULL;
        }
        gtk_editable_set_position(GTK_EDITABLE(tp->entry), -1);
        return TRUE;
    }

    return FALSE;
}

/* ---- Constructor ---- */
DC_TerminalPanel *
dc_terminal_panel_new(void)
{
    DC_TerminalPanel *tp = calloc(1, sizeof(*tp));
    if (!tp) return NULL;

    tp->hist_pos = -1;

    /* Container */
    tp->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Header label */
    GtkWidget *header = gtk_label_new("Terminal");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_widget_set_margin_start(header, 6);
    gtk_widget_set_margin_top(header, 2);
    gtk_widget_set_margin_bottom(header, 2);
    {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
        gtk_label_set_attributes(GTK_LABEL(header), attrs);
        pango_attr_list_unref(attrs);
    }
    gtk_widget_set_opacity(header, 0.6);
    gtk_box_append(GTK_BOX(tp->box), header);

    /* Output text view (read-only, monospace, dark) */
    tp->text_view = gtk_text_view_new();
    tp->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tp->text_view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tp->text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tp->text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tp->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tp->text_view), 6);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tp->text_view), 6);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tp->text_view), 4);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(tp->text_view), 4);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tp->text_view), TRUE);

    /* Scrolled window for output */
    tp->scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tp->scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tp->scroll), tp->text_view);
    gtk_widget_set_vexpand(tp->scroll, TRUE);
    gtk_widget_set_hexpand(tp->scroll, TRUE);
    gtk_box_append(GTK_BOX(tp->box), tp->scroll);

    /* Input entry with prompt */
    GtkWidget *input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(input_box, 4);
    gtk_widget_set_margin_end(input_box, 4);
    gtk_widget_set_margin_top(input_box, 2);
    gtk_widget_set_margin_bottom(input_box, 4);

    GtkWidget *prompt = gtk_label_new(">");
    {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_family_new("monospace"));
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes(GTK_LABEL(prompt), attrs);
        pango_attr_list_unref(attrs);
    }
    gtk_box_append(GTK_BOX(input_box), prompt);

    tp->entry = gtk_entry_new();
    gtk_widget_set_hexpand(tp->entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(tp->entry), "Type a command...");
    g_signal_connect(tp->entry, "activate", G_CALLBACK(on_entry_activate), tp);

    /* Key controller for history navigation */
    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_entry_key), tp);
    gtk_widget_add_controller(tp->entry, key_ctrl);

    gtk_box_append(GTK_BOX(input_box), tp->entry);
    gtk_box_append(GTK_BOX(tp->box), input_box);

    /* Welcome message */
    dc_terminal_panel_append(tp, "DunCAD Terminal\nType 'help' for commands.\n\n");

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "terminal panel created");

    return tp;
}

void
dc_terminal_panel_free(DC_TerminalPanel *tp)
{
    if (!tp) return;
    for (int i = 0; i < tp->hist_count; i++)
        free(tp->history[i]);
    free(tp->history);
    free(tp->saved_input);
    free(tp);
}

GtkWidget *
dc_terminal_panel_widget(DC_TerminalPanel *tp)
{
    return tp ? tp->box : NULL;
}

void
dc_terminal_panel_append(DC_TerminalPanel *tp, const char *text)
{
    if (!tp || !text) return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(tp->buffer, &end);
    gtk_text_buffer_insert(tp->buffer, &end, text, -1);
    scroll_to_bottom(tp);
}

void
dc_terminal_panel_set_command_callback(DC_TerminalPanel *tp,
                                        DC_TerminalCmdCb cb,
                                        void *userdata)
{
    if (!tp) return;
    tp->cmd_cb = cb;
    tp->cmd_cb_data = userdata;
}
