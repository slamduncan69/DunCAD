#ifndef DC_TERMINAL_PANEL_H
#define DC_TERMINAL_PANEL_H

#include <gtk/gtk.h>

typedef struct DC_TerminalPanel DC_TerminalPanel;

/* Create a new terminal panel. */
DC_TerminalPanel *dc_terminal_panel_new(void);

/* Free the terminal panel. */
void dc_terminal_panel_free(DC_TerminalPanel *tp);

/* Get the top-level GTK widget for embedding in the layout. */
GtkWidget *dc_terminal_panel_widget(DC_TerminalPanel *tp);

/* Append a line of text to the terminal output. */
void dc_terminal_panel_append(DC_TerminalPanel *tp, const char *text);

/* Set the command handler: called when user presses Enter.
 * handler(command, userdata) — command is the text from the input entry. */
typedef void (*DC_TerminalCmdCb)(const char *command, void *userdata);
void dc_terminal_panel_set_command_callback(DC_TerminalPanel *tp,
                                             DC_TerminalCmdCb cb,
                                             void *userdata);

#endif /* DC_TERMINAL_PANEL_H */
