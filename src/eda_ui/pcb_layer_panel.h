#ifndef DC_PCB_LAYER_PANEL_H
#define DC_PCB_LAYER_PANEL_H

/*
 * pcb_layer_panel.h — Layer visibility sidebar for PCB editor.
 *
 * Shows checkboxes with color swatches for each PCB layer.
 * Active layer selector highlights the currently-active routing layer.
 */

#include <gtk/gtk.h>

typedef struct DC_PcbLayerPanel DC_PcbLayerPanel;

/* Callbacks */
typedef void (*DC_LayerVisibilityCb)(int layer_id, int visible, void *userdata);
typedef void (*DC_ActiveLayerCb)(int layer_id, void *userdata);

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_PcbLayerPanel *dc_pcb_layer_panel_new(void);
void dc_pcb_layer_panel_free(DC_PcbLayerPanel *panel);

/* =========================================================================
 * Widget access
 * ========================================================================= */

GtkWidget *dc_pcb_layer_panel_widget(DC_PcbLayerPanel *panel);

/* =========================================================================
 * Callbacks
 * ========================================================================= */

void dc_pcb_layer_panel_set_visibility_callback(DC_PcbLayerPanel *panel,
                                                  DC_LayerVisibilityCb cb,
                                                  void *userdata);
void dc_pcb_layer_panel_set_active_callback(DC_PcbLayerPanel *panel,
                                              DC_ActiveLayerCb cb,
                                              void *userdata);

#endif /* DC_PCB_LAYER_PANEL_H */
