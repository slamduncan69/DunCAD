#define _POSIX_C_SOURCE 200809L
#include "ui/scad_completion.h"
#include <string.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * OpenSCAD keyword/function completion provider for GtkSourceView 5.
 *
 * Implements GtkSourceCompletionProvider using a static word list of all
 * OpenSCAD builtins. Uses fuzzy matching from GtkSourceCompletion.
 * ---------------------------------------------------------------------- */

/* ---- Proposal object (wraps a single keyword string) ---- */

#define DC_TYPE_SCAD_PROPOSAL (dc_scad_proposal_get_type())
G_DECLARE_FINAL_TYPE(DcScadProposal, dc_scad_proposal,
                     DC, SCAD_PROPOSAL, GObject)

struct _DcScadProposal {
    GObject parent;
    const char *word;       /* static string, not owned */
    const char *detail;     /* short description, static */
};

static void dc_scad_proposal_iface_init(GtkSourceCompletionProposalInterface *iface);

G_DEFINE_TYPE_WITH_CODE(DcScadProposal, dc_scad_proposal, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(GTK_SOURCE_TYPE_COMPLETION_PROPOSAL,
                          dc_scad_proposal_iface_init))

static char *
proposal_get_typed_text(GtkSourceCompletionProposal *proposal)
{
    DcScadProposal *self = DC_SCAD_PROPOSAL(proposal);
    return g_strdup(self->word);
}

static void
dc_scad_proposal_iface_init(GtkSourceCompletionProposalInterface *iface)
{
    iface->get_typed_text = proposal_get_typed_text;
}

static void dc_scad_proposal_class_init(DcScadProposalClass *klass) { (void)klass; }
static void dc_scad_proposal_init(DcScadProposal *self) { (void)self; }

static DcScadProposal *
dc_scad_proposal_new(const char *word, const char *detail)
{
    DcScadProposal *p = g_object_new(DC_TYPE_SCAD_PROPOSAL, NULL);
    p->word = word;
    p->detail = detail;
    return p;
}

/* ---- OpenSCAD keyword database ---- */

typedef struct {
    const char *word;
    const char *detail;
} ScadKeyword;

static const ScadKeyword SCAD_KEYWORDS[] = {
    /* 3D Primitives */
    { "cube",               "3D box: cube([x,y,z], center)" },
    { "sphere",             "3D sphere: sphere(r, $fn)" },
    { "cylinder",           "3D cylinder: cylinder(h, r, $fn)" },
    { "polyhedron",         "3D polyhedron: polyhedron(points, faces)" },

    /* 2D Primitives */
    { "circle",             "2D circle: circle(r, $fn)" },
    { "square",             "2D rect: square([x,y], center)" },
    { "polygon",            "2D polygon: polygon(points, paths)" },
    { "text",               "2D text: text(t, size, font)" },

    /* Transforms */
    { "translate",          "Move: translate([x,y,z])" },
    { "rotate",             "Rotate: rotate([x,y,z]) or rotate(a, v)" },
    { "scale",              "Scale: scale([x,y,z])" },
    { "mirror",             "Mirror: mirror([x,y,z])" },
    { "multmatrix",         "4x4 transform: multmatrix(m)" },
    { "resize",             "Resize to size: resize([x,y,z])" },
    { "offset",             "2D offset: offset(r) or offset(delta)" },

    /* Boolean Operations */
    { "union",              "Boolean union of children" },
    { "difference",         "Subtract children from first child" },
    { "intersection",       "Boolean intersection of children" },

    /* Extrusion */
    { "linear_extrude",     "Extrude 2D to 3D: linear_extrude(height)" },
    { "rotate_extrude",     "Revolve 2D to 3D: rotate_extrude(angle)" },

    /* CSG Operations */
    { "hull",               "Convex hull of children" },
    { "minkowski",          "Minkowski sum of children" },

    /* Import/Export */
    { "import",             "Import file: import(\"file.stl\")" },
    { "surface",            "Height map: surface(\"file.dat\")" },
    { "projection",         "3D to 2D: projection(cut)" },
    { "render",             "Force CGAL render: render(convexity)" },

    /* Control Flow */
    { "module",             "Define module: module name() { }" },
    { "function",           "Define function: function f(x) = expr" },
    { "for",                "Loop: for (i = [start:step:end])" },
    { "if",                 "Conditional: if (cond) { }" },
    { "else",               "Else branch" },
    { "let",                "Local binding: let (x = expr)" },
    { "each",               "Flatten in list comprehension" },
    { "assert",             "Runtime assertion: assert(cond, msg)" },
    { "echo",               "Debug output: echo(value)" },
    { "include",            "Include file: include <file.scad>" },
    { "use",                "Use file: use <file.scad>" },

    /* Math Functions */
    { "abs",                "Absolute value" },
    { "sign",               "Sign of value (-1, 0, 1)" },
    { "sin",                "Sine (degrees)" },
    { "cos",                "Cosine (degrees)" },
    { "tan",                "Tangent (degrees)" },
    { "asin",               "Arc sine (degrees)" },
    { "acos",               "Arc cosine (degrees)" },
    { "atan",               "Arc tangent (degrees)" },
    { "atan2",              "Arc tangent of y/x (degrees)" },
    { "floor",              "Round down to integer" },
    { "ceil",               "Round up to integer" },
    { "round",              "Round to nearest integer" },
    { "sqrt",               "Square root" },
    { "pow",                "Power: pow(base, exp)" },
    { "exp",                "e^x" },
    { "log",                "Natural logarithm" },
    { "ln",                 "Natural logarithm (alias)" },
    { "min",                "Minimum value" },
    { "max",                "Maximum value" },
    { "norm",               "Vector length: norm(v)" },
    { "cross",              "Cross product: cross(a, b)" },

    /* List/String Functions */
    { "len",                "Length of list or string" },
    { "concat",             "Concatenate lists" },
    { "lookup",             "Table lookup: lookup(key, table)" },
    { "str",                "Convert to string" },
    { "chr",                "Character from code point" },
    { "ord",                "Code point from character" },
    { "search",             "Search in list/string" },
    { "is_undef",           "Check if undefined" },
    { "is_bool",            "Check if boolean" },
    { "is_num",             "Check if number" },
    { "is_string",          "Check if string" },
    { "is_list",            "Check if list" },
    { "is_function",        "Check if function" },

    /* Special Variables */
    { "$fn",                "Fragment count (resolution)" },
    { "$fa",                "Fragment angle minimum" },
    { "$fs",                "Fragment size minimum" },
    { "$t",                 "Animation time (0..1)" },
    { "$vpr",               "Viewport rotation" },
    { "$vpt",               "Viewport translation" },
    { "$vpd",               "Viewport distance" },
    { "$vpf",               "Viewport FOV" },
    { "$children",          "Number of child modules" },
    { "$preview",           "true in preview, false in render" },

    /* Modifier Characters */
    { "children",           "Access child modules: children(i)" },
    { "color",              "Color: color(\"name\") or color([r,g,b,a])" },

    /* Constants */
    { "true",               "Boolean true" },
    { "false",              "Boolean false" },
    { "undef",              "Undefined value" },
    { "PI",                 "Pi (3.14159...)" },

    /* Common Parameters */
    { "center",             "Center the shape (true/false)" },
    { "convexity",          "Convexity hint for rendering" },
    { "twist",              "Twist angle for linear_extrude" },
    { "slices",             "Slices for extrusion" },
};

#define N_KEYWORDS (sizeof(SCAD_KEYWORDS) / sizeof(SCAD_KEYWORDS[0]))

/* ---- Provider implementation ---- */

struct _DcScadCompletion {
    GObject parent;
    GListStore *all_proposals;   /* full list, built once */
};

static void dc_scad_completion_provider_init(GtkSourceCompletionProviderInterface *iface);

G_DEFINE_TYPE_WITH_CODE(DcScadCompletion, dc_scad_completion, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(GTK_SOURCE_TYPE_COMPLETION_PROVIDER,
                          dc_scad_completion_provider_init))

static char *
provider_get_title(GtkSourceCompletionProvider *self)
{
    (void)self;
    return g_strdup("OpenSCAD");
}

static int
provider_get_priority(GtkSourceCompletionProvider *self,
                      GtkSourceCompletionContext  *context)
{
    (void)self; (void)context;
    return 100;  /* higher than words provider */
}

static gboolean
provider_is_trigger(GtkSourceCompletionProvider *self,
                    const GtkTextIter *iter, gunichar ch)
{
    (void)self; (void)iter;
    /* Trigger on $ (for special vars like $fn) */
    return ch == '$';
}

static void
provider_populate_async(GtkSourceCompletionProvider *provider,
                        GtkSourceCompletionContext  *context,
                        GCancellable               *cancellable,
                        GAsyncReadyCallback         callback,
                        gpointer                    user_data)
{
    DcScadCompletion *self = DC_SCAD_COMPLETION(provider);
    (void)cancellable;

    GTask *task = g_task_new(self, cancellable, callback, user_data);

    char *word = gtk_source_completion_context_get_word(context);
    if (!word || strlen(word) < 1) {
        g_task_return_pointer(task, NULL, NULL);
        g_object_unref(task);
        g_free(word);
        return;
    }

    /* Build filtered list using fuzzy matching */
    char *casefold = g_utf8_casefold(word, -1);
    GListStore *results = g_list_store_new(DC_TYPE_SCAD_PROPOSAL);

    for (size_t i = 0; i < N_KEYWORDS; i++) {
        guint priority = 0;
        if (gtk_source_completion_fuzzy_match(SCAD_KEYWORDS[i].word,
                                              casefold, &priority)) {
            DcScadProposal *p = dc_scad_proposal_new(
                SCAD_KEYWORDS[i].word, SCAD_KEYWORDS[i].detail);
            g_list_store_append(results, p);
            g_object_unref(p);
        }
    }

    g_free(casefold);
    g_free(word);

    g_task_return_pointer(task, results, g_object_unref);
    g_object_unref(task);
}

static GListModel *
provider_populate_finish(GtkSourceCompletionProvider *self,
                         GAsyncResult *result, GError **error)
{
    (void)self;
    return g_task_propagate_pointer(G_TASK(result), error);
}

static void
provider_display(GtkSourceCompletionProvider *self,
                 GtkSourceCompletionContext  *context,
                 GtkSourceCompletionProposal *proposal,
                 GtkSourceCompletionCell     *cell)
{
    (void)self;
    DcScadProposal *p = DC_SCAD_PROPOSAL(proposal);

    GtkSourceCompletionColumn col = gtk_source_completion_cell_get_column(cell);

    switch (col) {
    case GTK_SOURCE_COMPLETION_COLUMN_ICON:
        gtk_source_completion_cell_set_icon_name(cell, "completion-word-symbolic");
        break;

    case GTK_SOURCE_COMPLETION_COLUMN_TYPED_TEXT: {
        char *word = gtk_source_completion_context_get_word(context);
        if (word && word[0]) {
            char *casefold = g_utf8_casefold(word, -1);
            PangoAttrList *attrs =
                gtk_source_completion_fuzzy_highlight(p->word, casefold);
            gtk_source_completion_cell_set_text_with_attributes(
                cell, p->word, attrs);
            pango_attr_list_unref(attrs);
            g_free(casefold);
        } else {
            gtk_source_completion_cell_set_text(cell, p->word);
        }
        g_free(word);
        break;
    }

    case GTK_SOURCE_COMPLETION_COLUMN_COMMENT:
        gtk_source_completion_cell_set_text(cell, p->detail);
        break;

    default:
        gtk_source_completion_cell_set_text(cell, NULL);
        break;
    }
}

static void
provider_activate(GtkSourceCompletionProvider *self,
                  GtkSourceCompletionContext  *context,
                  GtkSourceCompletionProposal *proposal)
{
    (void)self;
    DcScadProposal *p = DC_SCAD_PROPOSAL(proposal);

    GtkTextIter begin, end;
    if (!gtk_source_completion_context_get_bounds(context, &begin, &end))
        return;

    GtkSourceBuffer *buffer = gtk_source_completion_context_get_buffer(context);
    gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(buffer));
    gtk_text_buffer_delete(GTK_TEXT_BUFFER(buffer), &begin, &end);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(buffer), &begin, p->word, -1);
    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(buffer));
}

static void
provider_refilter(GtkSourceCompletionProvider *self,
                  GtkSourceCompletionContext  *context,
                  GListModel                 *model)
{
    (void)self; (void)context; (void)model;
    /* Re-populate on each keystroke (simple approach) */
}

static void
dc_scad_completion_provider_init(GtkSourceCompletionProviderInterface *iface)
{
    iface->get_title      = provider_get_title;
    iface->get_priority   = provider_get_priority;
    iface->is_trigger     = provider_is_trigger;
    iface->populate_async = provider_populate_async;
    iface->populate_finish = provider_populate_finish;
    iface->display        = provider_display;
    iface->activate       = provider_activate;
    iface->refilter       = provider_refilter;
}

static void
dc_scad_completion_finalize(GObject *obj)
{
    DcScadCompletion *self = DC_SCAD_COMPLETION(obj);
    g_clear_object(&self->all_proposals);
    G_OBJECT_CLASS(dc_scad_completion_parent_class)->finalize(obj);
}

static void
dc_scad_completion_class_init(DcScadCompletionClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = dc_scad_completion_finalize;
}

static void
dc_scad_completion_init(DcScadCompletion *self)
{
    (void)self;
}

DcScadCompletion *
dc_scad_completion_new(void)
{
    return g_object_new(DC_TYPE_SCAD_COMPLETION, NULL);
}
