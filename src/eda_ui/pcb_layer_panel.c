#define _POSIX_C_SOURCE 200809L

#include "pcb_layer_panel.h"
#include "eda/eda_pcb.h"

#include <stdlib.h>
#include <string.h>

/* Layer display entries */
typedef struct {
    int         layer_id;
    const char *name;
    const char *color_css;  /* CSS color for swatch */
} LayerEntry;

static const LayerEntry LAYERS[] = {
    { DC_PCB_LAYER_F_CU,      "F.Cu",       "#cc0000" },
    { DC_PCB_LAYER_B_CU,      "B.Cu",       "#0000cc" },
    { DC_PCB_LAYER_F_SILKS,   "F.SilkS",    "#ffffff" },
    { DC_PCB_LAYER_B_SILKS,   "B.SilkS",    "#00cccc" },
    { DC_PCB_LAYER_F_MASK,    "F.Mask",      "#800080" },
    { DC_PCB_LAYER_B_MASK,    "B.Mask",      "#008080" },
    { DC_PCB_LAYER_EDGE_CUTS, "Edge.Cuts",   "#cccc00" },
};
#define N_LAYERS ((int)(sizeof(LAYERS) / sizeof(LAYERS[0])))

/* =========================================================================
 * Internal state
 * ========================================================================= */
struct DC_PcbLayerPanel {
    GtkWidget            *box;
    GtkWidget            *checks[7];
    DC_LayerVisibilityCb  vis_cb;
    void                 *vis_data;
    DC_ActiveLayerCb      act_cb;
    void                 *act_data;
};

/* =========================================================================
 * Callbacks
 * ========================================================================= */
static void
on_check_toggled(GtkCheckButton *btn, gpointer userdata)
{
    DC_PcbLayerPanel *p = userdata;
    for (int i = 0; i < N_LAYERS; i++) {
        if (GTK_WIDGET(btn) == p->checks[i]) {
            int active = gtk_check_button_get_active(btn);
            if (p->vis_cb) p->vis_cb(LAYERS[i].layer_id, active, p->vis_data);

            /* Double-click to set active layer (use single click for now) */
            if (active && p->act_cb) p->act_cb(LAYERS[i].layer_id, p->act_data);
            break;
        }
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
DC_PcbLayerPanel *dc_pcb_layer_panel_new(void)
{
    DC_PcbLayerPanel *p = calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(p->box, 4);
    gtk_widget_set_margin_end(p->box, 4);
    gtk_widget_set_margin_top(p->box, 4);

    /* Title */
    GtkWidget *title = gtk_label_new("Layers");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_append(GTK_BOX(p->box), title);
    gtk_box_append(GTK_BOX(p->box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Layer checkboxes */
    for (int i = 0; i < N_LAYERS; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

        /* Color swatch via CSS on a label widget */
        GtkWidget *swatch = gtk_label_new(" ");
        gtk_widget_set_size_request(swatch, 12, 12);
        char css_name[64];
        snprintf(css_name, sizeof(css_name), "layer-swatch-%d", i);
        gtk_widget_set_name(swatch, css_name);

        GtkCssProvider *css = gtk_css_provider_new();
        char css_str[256];
        snprintf(css_str, sizeof(css_str),
                 "#%s { background-color: %s; min-width: 12px; min-height: 12px; }",
                 css_name, LAYERS[i].color_css);
        gtk_css_provider_load_from_string(css, css_str);
        gtk_style_context_add_provider_for_display(
            gtk_widget_get_display(swatch),
            GTK_STYLE_PROVIDER(css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);

        gtk_box_append(GTK_BOX(row), swatch);

        GtkWidget *check = gtk_check_button_new_with_label(LAYERS[i].name);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(check), TRUE);
        g_signal_connect(check, "toggled", G_CALLBACK(on_check_toggled), p);
        gtk_box_append(GTK_BOX(row), check);
        p->checks[i] = check;

        gtk_box_append(GTK_BOX(p->box), row);
    }

    return p;
}

void dc_pcb_layer_panel_free(DC_PcbLayerPanel *p)
{
    if (p) free(p);
}

GtkWidget *dc_pcb_layer_panel_widget(DC_PcbLayerPanel *p)
{
    return p ? p->box : NULL;
}

void dc_pcb_layer_panel_set_visibility_callback(DC_PcbLayerPanel *p,
                                                  DC_LayerVisibilityCb cb,
                                                  void *userdata)
{
    if (!p) return;
    p->vis_cb = cb;
    p->vis_data = userdata;
}

void dc_pcb_layer_panel_set_active_callback(DC_PcbLayerPanel *p,
                                              DC_ActiveLayerCb cb,
                                              void *userdata)
{
    if (!p) return;
    p->act_cb = cb;
    p->act_data = userdata;
}
