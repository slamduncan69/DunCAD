#define _POSIX_C_SOURCE 200809L
#include "ui/scad_completion.h"
#include <string.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * OpenSCAD completion provider for GtkSourceView 5.
 *
 * Each keyword has an optional snippet template. When activated:
 *   - If template exists: delete typed prefix, push snippet with tabstops
 *   - If no template: insert plain word
 *
 * Snippets use GtkSourceView's ${ } syntax for tab-navigable placeholders.
 * ---------------------------------------------------------------------- */

/* ---- Proposal object ---- */

#define DC_TYPE_SCAD_PROPOSAL (dc_scad_proposal_get_type())
G_DECLARE_FINAL_TYPE(DcScadProposal, dc_scad_proposal,
                     DC, SCAD_PROPOSAL, GObject)

struct _DcScadProposal {
    GObject     parent;
    const char *word;       /* keyword name (static) */
    const char *snippet;    /* snippet template or NULL (static) */
    const char *detail;     /* shown in popup (static) */
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

/* ---- OpenSCAD keyword database ---- */

typedef struct {
    const char *word;
    const char *snippet;   /* NULL = insert word only */
    const char *detail;
} ScadKeyword;

static const ScadKeyword SCAD_KEYWORDS[] = {
    /* 3D Primitives */
    { "cube",           "cube([${1:10}, ${2:10}, ${3:10}], center=${4:true});",
                        "cube([x, y, z], center=true);" },
    { "sphere",         "sphere(r=${1:10}, $$fn=${2:64});",
                        "sphere(r, $fn);" },
    { "cylinder",       "cylinder(h=${1:10}, r=${2:5}, $$fn=${3:64}, center=${4:true});",
                        "cylinder(h, r, $fn, center);" },
    { "polyhedron",     "polyhedron(points=[${1}], faces=[${2}]);",
                        "polyhedron(points, faces);" },

    /* 2D Primitives */
    { "circle",         "circle(r=${1:10}, $$fn=${2:64});",
                        "circle(r, $fn);" },
    { "square",         "square([${1:10}, ${2:10}], center=${3:true});",
                        "square([x, y], center);" },
    { "polygon",        "polygon(points=[\n\t[${1:0}, ${2:0}],\n\t[${3:10}, ${4:0}],\n\t[${5:10}, ${6:10}],\n]);",
                        "polygon(points=[...]);" },
    { "text",           "text(\"${1:hello}\", size=${2:10});",
                        "text(t, size);" },

    /* Transforms */
    { "translate",      "translate([${1:0}, ${2:0}, ${3:0}])\n\t${0};",
                        "translate([x, y, z])" },
    { "rotate",         "rotate([${1:0}, ${2:0}, ${3:0}])\n\t${0};",
                        "rotate([x, y, z])" },
    { "scale",          "scale([${1:1}, ${2:1}, ${3:1}])\n\t${0};",
                        "scale([x, y, z])" },
    { "mirror",         "mirror([${1:1}, ${2:0}, ${3:0}])\n\t${0};",
                        "mirror([x, y, z])" },
    { "resize",         "resize([${1:10}, ${2:10}, ${3:10}])\n\t${0};",
                        "resize([x, y, z])" },
    { "multmatrix",     "multmatrix(${1:m})\n\t${0};",
                        "multmatrix(m)" },
    { "offset",         "offset(r=${1:1})\n\t${0};",
                        "offset(r) or offset(delta)" },
    { "color",          "color(\"${1:red}\")\n\t${0};",
                        "color(\"name\") or color([r,g,b,a])" },

    /* Boolean Operations */
    { "difference",     "difference() {\n\t${1:// base}\n\t${2:// subtract}\n}",
                        "difference() { base; subtract; }" },
    { "union",          "union() {\n\t${0}\n}",
                        "union() { ... }" },
    { "intersection",   "intersection() {\n\t${0}\n}",
                        "intersection() { ... }" },

    /* Extrusion */
    { "linear_extrude", "linear_extrude(height=${1:10}, center=${2:true})\n\t${0};",
                        "linear_extrude(height, center)" },
    { "rotate_extrude", "rotate_extrude(angle=${1:360}, $$fn=${2:64})\n\t${0};",
                        "rotate_extrude(angle, $fn)" },

    /* CSG Operations */
    { "hull",           "hull() {\n\t${0}\n}",
                        "hull() { ... }" },
    { "minkowski",      "minkowski() {\n\t${1:// base}\n\t${2:// round}\n}",
                        "minkowski() { base; round; }" },

    /* Import/Projection */
    { "import",         "import(\"${1:file.stl}\");",
                        "import(\"file\");" },
    { "surface",        "surface(\"${1:file.dat}\");",
                        "surface(\"file\");" },
    { "projection",     "projection(cut=${1:false})\n\t${0};",
                        "projection(cut)" },
    { "render",         "render(convexity=${1:10})\n\t${0};",
                        "render(convexity)" },

    /* Control Flow */
    { "module",         "module ${1:name}(${2}) {\n\t${0}\n}",
                        "module name(params) { ... }" },
    { "function",       "function ${1:name}(${2}) = ${0};",
                        "function name(params) = expr;" },
    { "for",            "for (${1:i} = [${2:0}:${3:1}:${4:10}]) {\n\t${0}\n}",
                        "for (i = [start:step:end]) { ... }" },
    { "if",             "if (${1:condition}) {\n\t${0}\n}",
                        "if (cond) { ... }" },
    { "let",            "let (${1:x} = ${2:value})\n\t${0};",
                        "let (x = value)" },
    { "echo",           "echo(${1:value});",
                        "echo(value);" },
    { "assert",         "assert(${1:condition}, \"${2:message}\");",
                        "assert(cond, msg);" },
    { "include",        "include <${1:file.scad}>",
                        "include <file.scad>" },
    { "use",            "use <${1:file.scad}>",
                        "use <file.scad>" },
    { "children",       "children(${1:0});",
                        "children(index);" },

    /* Simple keywords — no snippet template */
    { "else",       NULL, "else branch" },
    { "each",       NULL, "flatten in list comprehension" },
    { "true",       NULL, "boolean true" },
    { "false",      NULL, "boolean false" },
    { "undef",      NULL, "undefined value" },
    { "PI",         NULL, "3.14159..." },
    { "center",     NULL, "center=true/false" },
    { "convexity",  NULL, "convexity hint" },
    { "twist",      NULL, "twist angle for linear_extrude" },
    { "slices",     NULL, "slices for extrusion" },

    /* Math — simple function calls */
    { "abs",        "abs(${1:x})",          "absolute value" },
    { "sign",       "sign(${1:x})",         "sign (-1, 0, 1)" },
    { "sin",        "sin(${1:deg})",        "sine (degrees)" },
    { "cos",        "cos(${1:deg})",        "cosine (degrees)" },
    { "tan",        "tan(${1:deg})",        "tangent (degrees)" },
    { "asin",       "asin(${1:x})",         "arc sine (degrees)" },
    { "acos",       "acos(${1:x})",         "arc cosine (degrees)" },
    { "atan",       "atan(${1:x})",         "arc tangent (degrees)" },
    { "atan2",      "atan2(${1:y}, ${2:x})", "arc tangent of y/x" },
    { "floor",      "floor(${1:x})",        "round down" },
    { "ceil",       "ceil(${1:x})",         "round up" },
    { "round",      "round(${1:x})",        "round to nearest" },
    { "sqrt",       "sqrt(${1:x})",         "square root" },
    { "pow",        "pow(${1:base}, ${2:exp})", "power" },
    { "exp",        "exp(${1:x})",          "e^x" },
    { "log",        "log(${1:x})",          "natural logarithm" },
    { "ln",         "ln(${1:x})",           "natural logarithm" },
    { "min",        "min(${1:a}, ${2:b})",  "minimum" },
    { "max",        "max(${1:a}, ${2:b})",  "maximum" },
    { "norm",       "norm(${1:v})",         "vector length" },
    { "cross",      "cross(${1:a}, ${2:b})", "cross product" },

    /* List/String functions */
    { "len",        "len(${1:list})",       "length" },
    { "concat",     "concat(${1:a}, ${2:b})", "concatenate lists" },
    { "lookup",     "lookup(${1:key}, ${2:table})", "table lookup" },
    { "str",        "str(${1:value})",      "convert to string" },
    { "chr",        "chr(${1:code})",       "char from code point" },
    { "ord",        "ord(${1:char})",       "code point from char" },
    { "search",     "search(${1:needle}, ${2:haystack})", "search" },
    { "is_undef",   "is_undef(${1:x})",    "check if undefined" },
    { "is_bool",    "is_bool(${1:x})",      "check if boolean" },
    { "is_num",     "is_num(${1:x})",       "check if number" },
    { "is_string",  "is_string(${1:x})",    "check if string" },
    { "is_list",    "is_list(${1:x})",      "check if list" },
    { "is_function","is_function(${1:x})",  "check if function" },

    /* Special Variables */
    { "$fn",        NULL, "fragment count (resolution)" },
    { "$fa",        NULL, "fragment angle minimum" },
    { "$fs",        NULL, "fragment size minimum" },
    { "$t",         NULL, "animation time (0..1)" },
    { "$vpr",       NULL, "viewport rotation" },
    { "$vpt",       NULL, "viewport translation" },
    { "$vpd",       NULL, "viewport distance" },
    { "$vpf",       NULL, "viewport FOV" },
    { "$children",  NULL, "number of child modules" },
    { "$preview",   NULL, "true in preview, false in render" },
};

#define N_KEYWORDS (sizeof(SCAD_KEYWORDS) / sizeof(SCAD_KEYWORDS[0]))

/* ---- Provider implementation ---- */

struct _DcScadCompletion {
    GObject parent;
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
    return 200;
}

static gboolean
provider_is_trigger(GtkSourceCompletionProvider *self,
                    const GtkTextIter *iter, gunichar ch)
{
    (void)self; (void)iter;
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

    GTask *task = g_task_new(self, cancellable, callback, user_data);

    char *word = gtk_source_completion_context_get_word(context);
    if (!word || strlen(word) < 1) {
        /* Return empty list, never NULL (NULL can disable the provider) */
        GListStore *empty = g_list_store_new(DC_TYPE_SCAD_PROPOSAL);
        g_task_return_pointer(task, empty, g_object_unref);
        g_object_unref(task);
        g_free(word);
        return;
    }

    char *casefold = g_utf8_casefold(word, -1);
    GListStore *results = g_list_store_new(DC_TYPE_SCAD_PROPOSAL);

    for (size_t i = 0; i < N_KEYWORDS; i++) {
        guint priority = 0;
        if (gtk_source_completion_fuzzy_match(SCAD_KEYWORDS[i].word,
                                              casefold, &priority)) {
            DcScadProposal *p = g_object_new(DC_TYPE_SCAD_PROPOSAL, NULL);
            p->word    = SCAD_KEYWORDS[i].word;
            p->snippet = SCAD_KEYWORDS[i].snippet;
            p->detail  = SCAD_KEYWORDS[i].detail;
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
        gtk_source_completion_cell_set_icon_name(
            cell, p->snippet ? "completion-snippet-symbolic"
                             : "completion-word-symbolic");
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

    case GTK_SOURCE_COMPLETION_COLUMN_AFTER:
        /* Show the full syntax hint to the right of the keyword */
        if (p->detail)
            gtk_source_completion_cell_set_text(cell, p->detail);
        else
            gtk_source_completion_cell_set_text(cell, NULL);
        break;

    case GTK_SOURCE_COMPLETION_COLUMN_COMMENT:
        gtk_source_completion_cell_set_text(cell, NULL);
        break;

    default:
        gtk_source_completion_cell_set_text(cell, NULL);
        break;
    }
}

/* Deferred snippet push — runs after the completion system finishes */
typedef struct {
    GtkSourceView    *view;    /* borrowed ref kept alive by widget */
    GtkSourceSnippet *snippet; /* owned */
} SnippetIdle;

static gboolean
push_snippet_idle(gpointer data)
{
    SnippetIdle *si = data;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(si->view));
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(
        buf, &cursor, gtk_text_buffer_get_insert(buf));
    gtk_source_view_push_snippet(si->view, si->snippet, &cursor);
    g_object_unref(si->snippet);
    g_free(si);
    return G_SOURCE_REMOVE;
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
    GtkSourceView *view = gtk_source_completion_context_get_view(context);

    /* Delete the typed prefix */
    gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(buffer));
    gtk_text_buffer_delete(GTK_TEXT_BUFFER(buffer), &begin, &end);
    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(buffer));

    if (p->snippet) {
        /* Parse snippet and defer push to idle — the completion system
         * needs to finish its activate cycle before we push a snippet,
         * otherwise it permanently blocks interactive completion. */
        GError *err = NULL;
        GtkSourceSnippet *snip =
            gtk_source_snippet_new_parsed(p->snippet, &err);
        if (snip) {
            SnippetIdle *si = g_new(SnippetIdle, 1);
            si->view = view;
            si->snippet = snip;  /* transfer ownership */
            g_idle_add(push_snippet_idle, si);
        } else {
            if (err) g_error_free(err);
            gtk_text_buffer_insert(
                GTK_TEXT_BUFFER(buffer), &begin, p->word, -1);
        }
    } else {
        gtk_text_buffer_insert(
            GTK_TEXT_BUFFER(buffer), &begin, p->word, -1);
    }
}

static void
provider_refilter(GtkSourceCompletionProvider *self,
                  GtkSourceCompletionContext  *context,
                  GListModel                 *model)
{
    (void)self; (void)context; (void)model;
}

static void
dc_scad_completion_provider_init(GtkSourceCompletionProviderInterface *iface)
{
    iface->get_title       = provider_get_title;
    iface->get_priority    = provider_get_priority;
    iface->is_trigger      = provider_is_trigger;
    iface->populate_async  = provider_populate_async;
    iface->populate_finish = provider_populate_finish;
    iface->display         = provider_display;
    iface->activate        = provider_activate;
    iface->refilter        = provider_refilter;
}

static void
dc_scad_completion_class_init(DcScadCompletionClass *klass)
{
    (void)klass;
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
