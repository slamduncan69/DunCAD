/*
 * duncad-docs -- DunCAD Project Documentation
 *
 * Usage:
 *   duncad-docs                         top-level overview
 *   duncad-docs <category>              category overview
 *   duncad-docs <category> <topic>      leaf detail
 *   duncad-docs --search <term>         search all nodes
 *   duncad-docs --tree                  print full hierarchy
 *   duncad-docs update                  rebuild this binary
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- HELP TEXT ---- */

static const char HELP_ROOT[] =
"DUNCAD -- Documentation\n"
"\n"
"DunCAD is a pure C11/GTK4 IDE for interactive bezier curve design\n"
"with OpenSCAD and KiCad export. Edit splines visually and export\n"
"parametric .scad files or KiCad board outlines.\n"
"\n"
"CODE:\n"
"  duncad-docs core          Foundation utilities (no external deps)\n"
"  duncad-docs bezier        Bezier spline geometry and GTK4 editor\n"
"  duncad-docs scad          OpenSCAD code generation\n"
"  duncad-docs ui            GTK4 application window\n"
"  duncad-docs inspect       Unix socket inspect/control server\n"
"  duncad-docs build         Build system and test suite\n"
"  duncad-docs conventions   Naming, ownership, and error handling\n"
"\n"
"PROJECT:\n"
"  duncad-docs philosophy    Design philosophy and tech stack\n"
"  duncad-docs phases        Development phases and status\n"
"  duncad-docs plans         Future plans and long-term vision\n"
"  duncad-docs sessions      Development session log\n"
"\n"
"SEARCH:\n"
"  duncad-docs --search <term>   Search all documentation\n"
"  duncad-docs --tree            Print the full hierarchy\n"
"\n"
"MAINTENANCE:\n"
"  duncad-docs update            Rebuild this binary (runs cmake)\n";

static const char HELP_CORE[] =
"CORE -- Foundation Utilities\n"
"\n"
"src/core/ contains zero-dependency modules used by every layer.\n"
"No GTK, no external libraries -- only libc. Safe to use from\n"
"headless CLI tools, tests, or future HTTP/IPC servers.\n"
"\n"
"TOPICS:\n"
"  duncad-docs core array           DC_Array dynamic array\n"
"  duncad-docs core string_builder  DC_StringBuilder\n"
"  duncad-docs core error           DC_Error uniform error type\n"
"  duncad-docs core log             Structured dual-output logger\n"
"  duncad-docs core manifest        Project workspace model\n";

static const char HELP_CORE_ARRAY[] =
"CORE: ARRAY -- DC_Array Dynamic Array\n"
"\n"
"Type-safe dynamic array. Stores fixed-size elements by value\n"
"(not by pointer). Grows by doubling. Thread-unsafe.\n"
"\n"
"API (src/core/array.h):\n"
"  DC_Array *dc_array_new(size_t elem_size)     allocate\n"
"  void      dc_array_free(DC_Array *)           free array and store\n"
"  bool      dc_array_push(DC_Array *, void *)   append (memcpy)\n"
"  void     *dc_array_get(DC_Array *, size_t)   pointer to element i\n"
"  bool      dc_array_remove(DC_Array *, size_t) swap-remove at index\n"
"  void      dc_array_clear(DC_Array *)           reset len to 0\n"
"  size_t    dc_array_len(DC_Array *)             number of elements\n"
"\n"
"NOTES:\n"
"  dc_array_get() returns a borrowed interior pointer. It is\n"
"  invalidated on the next push that triggers a reallocation.\n"
"  Copy out the value before pushing again.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs conventions ownership   Borrowed vs owned pointers\n";

static const char HELP_CORE_STRING_BUILDER[] =
"CORE: STRING_BUILDER -- DC_StringBuilder\n"
"\n"
"Dynamic string construction with printf-style formatting.\n"
"Backed by a heap buffer that doubles on overflow.\n"
"\n"
"API (src/core/string_builder.h):\n"
"  DC_StringBuilder *dc_sb_new(void)               allocate\n"
"  void              dc_sb_free(DC_StringBuilder *) free struct + buffer\n"
"  bool  dc_sb_append(DC_StringBuilder *, char *)  append literal\n"
"  bool  dc_sb_appendf(DC_StringBuilder *, fmt, ...) printf append\n"
"  char *dc_sb_get(DC_StringBuilder *)              borrow current string\n"
"  char *dc_sb_take(DC_StringBuilder *)             transfer buffer\n"
"  void  dc_sb_clear(DC_StringBuilder *)            reset length to 0\n"
"\n"
"OWNERSHIP:\n"
"  dc_sb_take() transfers the buffer to the caller. The caller is\n"
"  responsible for free()ing the buffer. dc_sb_free() must still be\n"
"  called on the struct itself afterward (frees the empty struct).\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs conventions ownership   Memory ownership rules\n";

static const char HELP_CORE_ERROR[] =
"CORE: ERROR -- DC_Error Uniform Error Type\n"
"\n"
"Every public function that can fail takes a DC_Error *err out-param.\n"
"Pass NULL to ignore. Check err->code != DC_ERR_NONE on return.\n"
"\n"
"STRUCT (src/core/error.h):\n"
"  DC_Error.code     DC_ErrorCode enum\n"
"  DC_Error.message  Human-readable description\n"
"  DC_Error.file     Source file (__FILE__)\n"
"  DC_Error.line     Source line (__LINE__)\n"
"\n"
"MACROS:\n"
"  DC_SET_ERROR(err, code, msg)   Fill err at call site with location\n"
"  DC_CHECK(err)                  Early-return false if err is set\n"
"\n"
"CODES:\n"
"  DC_ERR_NONE       Success\n"
"  DC_ERR_IO         File I/O failure\n"
"  DC_ERR_ALLOC      Memory allocation failure\n"
"  DC_ERR_INVALID    Invalid argument\n"
"  DC_ERR_NOT_FOUND  Resource not found\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs conventions errors   Error handling patterns\n";

static const char HELP_CORE_LOG[] =
"CORE: LOG -- Structured Dual-Output Logger\n"
"\n"
"Singleton logger writing to stderr and a JSON log file in parallel.\n"
"The JSON stream is a first-class artifact for LLM context ingestion.\n"
"\n"
"API (src/core/log.h):\n"
"  bool dc_log_init(path)              open log file; call once at startup\n"
"  void dc_log_shutdown(void)          flush and close\n"
"  void dc_log(level, cat, fmt, ...)   write event\n"
"\n"
"LEVELS: DC_LOG_DEBUG, DC_LOG_INFO, DC_LOG_WARN, DC_LOG_ERROR\n"
"\n"
"CATEGORIES:\n"
"  DC_CAT_APP      Application lifecycle\n"
"  DC_CAT_RENDER   Canvas / drawing events\n"
"  DC_CAT_FILE     File I/O operations\n"
"  DC_CAT_BUILD    Build system events\n"
"  DC_CAT_TOOL     External tool invocations\n"
"  DC_CAT_LLM      LLM integration events\n"
"\n"
"NOTES:\n"
"  Not thread-safe (Phase 1). Uses gmtime() not gmtime_r() to avoid\n"
"  POSIX_C_SOURCE portability issues under -Wpedantic.\n"
"  One permitted global: g_log in log.c.\n";

static const char HELP_CORE_MANIFEST[] =
"CORE: MANIFEST -- Project Workspace Model\n"
"\n"
"Tracks design artifacts (files), their dependencies, build status,\n"
"and errors. Acts as the in-memory project model.\n"
"\n"
"API (src/core/manifest.h):\n"
"  DC_Manifest *dc_manifest_new(void)             allocate empty\n"
"  void         dc_manifest_free(DC_Manifest *)   free manifest + entries\n"
"  bool  dc_manifest_load(path, DC_Manifest **, DC_Error *)\n"
"                                                  load from .json (stub)\n"
"  bool  dc_manifest_save(DC_Manifest *, path, DC_Error *)\n"
"                                                  save to .json\n"
"  bool  dc_manifest_add_artifact(...)             register a file\n"
"\n"
"PHASE STATUS:\n"
"  dc_manifest_load() is a Phase 1 stub. Full JSON round-trip\n"
"  deferred to Phase 2.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs build   Build system that populates the manifest\n";

static const char HELP_BEZIER[] =
"BEZIER -- Bezier Spline Geometry and GTK4 Editor\n"
"\n"
"src/bezier/ contains both pure geometric computation (no UI deps)\n"
"and GTK4 UI components for interactive editing.\n"
"\n"
"PURE GEOMETRY (no GTK dependency, included in dc_core):\n"
"  duncad-docs bezier curve    DC_BezierCurve spline data model\n"
"  duncad-docs bezier fit      Schneider algorithm curve fitting\n"
"\n"
"GTK4 UI COMPONENTS (linked into the duncad executable only):\n"
"  duncad-docs bezier canvas   Cairo drawing area with zoom/pan\n"
"  duncad-docs bezier editor   Interactive spline editor + undo/redo\n"
"  duncad-docs bezier panel    Numeric sidebar for knot/handle data\n";

static const char HELP_BEZIER_CURVE[] =
"BEZIER: CURVE -- DC_BezierCurve Spline Data Model\n"
"\n"
"Cubic bezier spline with knots, handles, and continuity constraints.\n"
"Pure C with no GTK dependency; safe to use from tests or CLI.\n"
"\n"
"STRUCT (src/bezier/bezier_curve.h):\n"
"  DC_BezierCurve   top-level spline (array of knots)\n"
"  DC_BezierKnot    position + two handles (h_prev, h_next)\n"
"  DC_Continuity    SMOOTH | SYMMETRIC | CORNER per knot\n"
"\n"
"KEY API:\n"
"  dc_bezier_curve_new / _free\n"
"  dc_bezier_curve_add_knot(curve, x, y)       append knot\n"
"  dc_bezier_curve_eval(curve, seg, t, out)    De Casteljau at t\n"
"  dc_bezier_curve_polyline(curve, tol, out)   tessellate to points\n"
"  dc_bezier_curve_bounds(curve, min, max)     bounding box\n"
"  dc_bezier_curve_set_continuity(curve, i, c) update constraint\n"
"\n"
"CONTINUITY:\n"
"  SMOOTH     h_prev and h_next are colinear, magnitudes independent\n"
"  SYMMETRIC  colinear and equal magnitude\n"
"  CORNER     handles fully independent\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier fit      Curve fitting that produces knots\n"
"  duncad-docs bezier editor   UI layer consuming this model\n";

static const char HELP_BEZIER_FIT[] =
"BEZIER: FIT -- Schneider Curve Fitting Algorithm\n"
"\n"
"Fits a sequence of 2D points to a cubic bezier spline.\n"
"Based on Philip Schneider's algorithm from Graphics Gems I (1990).\n"
"\n"
"API (src/bezier/bezier_fit.h):\n"
"  bool dc_bezier_fit(points, n, error_tol, curve_out, DC_Error *)\n"
"\n"
"ALGORITHM:\n"
"  1. Estimate tangents from first/last point neighbors\n"
"  2. Chord-length parameterization of the point sequence\n"
"  3. Least-squares fit to cubic bezier segments\n"
"  4. Newton-Raphson reparameterization to reduce error\n"
"  5. Adaptive subdivision at the point of maximum error\n"
"\n"
"USED BY:\n"
"  DC_MODE_FREEHAND in the bezier editor: user drags freehand,\n"
"  release triggers dc_bezier_fit() to generate a clean spline.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier curve    The spline model produced by fitting\n"
"  duncad-docs bezier editor   Editor mode that triggers fitting\n";

static const char HELP_BEZIER_CANVAS[] =
"BEZIER: CANVAS -- Cairo Drawing Area\n"
"\n"
"GTK4 GtkDrawingArea with Cairo rendering. Handles zoom, pan,\n"
"grid display, cursor tracking, and coordinate transforms.\n"
"\n"
"FEATURES:\n"
"  Grid       1mm minor lines, 10mm major lines\n"
"  Zoom       scroll wheel; range configurable\n"
"  Pan        middle-click drag or space+drag\n"
"  Coords     screen <-> world transform via affine matrix\n"
"  Cursor     live coordinate display in status bar\n"
"\n"
"API (src/bezier/bezier_canvas.h):\n"
"  DC_BezierCanvas *dc_bezier_canvas_new(void)\n"
"  GtkWidget       *dc_bezier_canvas_widget(canvas)\n"
"  void  dc_bezier_canvas_set_curve(canvas, DC_BezierCurve *)\n"
"  void  dc_bezier_canvas_set_zoom(canvas, double scale)\n"
"  void  dc_bezier_canvas_screen_to_world(canvas, sx, sy, wx, wy)\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier editor   Editor that owns the canvas\n";

static const char HELP_BEZIER_EDITOR[] =
"BEZIER: EDITOR -- Interactive Spline Editor\n"
"\n"
"Combines DC_BezierCanvas + DC_BezierCurve into a complete\n"
"interactive editor with three input modes and undo/redo.\n"
"\n"
"MODES (DC_EditorMode):\n"
"  DC_MODE_CLICK_PLACE  Left-click places or moves knots; no handles\n"
"  DC_MODE_CLICK_DRAG   Click-and-drag pulls out symmetric handles\n"
"  DC_MODE_FREEHAND     Drag captures points; release fits the curve\n"
"\n"
"CLOSED SHAPES:\n"
"  Click on P0 to close the shape. The closing segment wraps\n"
"  from the last on-curve point through a control back to P0.\n"
"  Data layout: [P0, C1, P2, ..., Pn, Cn+1] — even count.\n"
"  P0's juncture is toggleable with [C]:\n"
"    Juncture ON  = sharp corner at closure point\n"
"    Juncture OFF = smooth C1 curve (controls mirror across P0)\n"
"  Dragging P0 when smooth moves both adjacent controls.\n"
"  Dragging C_first or C_last mirrors the opposite control.\n"
"  Deleting any point reopens the shape.\n"
"\n"
"UNDO/REDO:\n"
"  Each edit pushes a snapshot of the full DC_BezierCurve.\n"
"  dc_bezier_editor_undo() / dc_bezier_editor_redo() restore them.\n"
"\n"
"NUMERIC INPUT PANEL:\n"
"  A horizontal strip below the canvas shows point info and stats.\n"
"  Layout: [P3 (control)]  X: [__12.50__]  Y: [__-4.25__]  | 4 pts  2 segs  Open  Chain: OFF\n"
"  When a point is selected, X/Y fields are editable (Enter to commit).\n"
"  When no point is selected, fields show '--' and are greyed out.\n"
"  Stats update live: point count, segment count, open/closed, chain mode.\n"
"\n"
"API (src/bezier/bezier_editor.h):\n"
"  DC_BezierEditor *dc_bezier_editor_new(void)\n"
"  GtkWidget       *dc_bezier_editor_widget(editor)\n"
"  int              dc_bezier_editor_is_closed(editor)\n"
"  int   dc_bezier_editor_get_point(editor, index, &x, &y)  0 on fail\n"
"  void  dc_bezier_editor_set_point(editor, index, x, y)\n"
"  int   dc_bezier_editor_is_juncture(editor, index)\n"
"  int   dc_bezier_editor_get_chain_mode(editor)\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier canvas   Drawing area owned by editor\n"
"  duncad-docs bezier panel    Sidebar that reads editor selection\n"
"  duncad-docs bezier fit      Fitting triggered by freehand mode\n";

static const char HELP_BEZIER_PANEL[] =
"BEZIER: PANEL -- Numeric Input Panel\n"
"\n"
"Horizontal strip embedded inside DC_BezierEditor (below canvas).\n"
"Displays and allows editing of the selected point's coordinates,\n"
"point type, and overall shape statistics.\n"
"\n"
"LAYOUT:\n"
"  [P3 (control)]  X: [__12.50__]  Y: [__-4.25__]  | 4 pts  2 segs  Open  Chain: OFF\n"
"\n"
"DISPLAYS:\n"
"  Point label      'P{index} ({juncture|control})' or 'No selection'\n"
"  X/Y entries      Editable when a point is selected (Enter to commit)\n"
"  Stats            Point count, segment count, Open/Closed, Chain: ON/OFF\n"
"\n"
"IMPLEMENTATION:\n"
"  Not a separate module — lives inside bezier_editor.c as panel_box,\n"
"  point_label, entry_x, entry_y, stats_label fields on DC_BezierEditor.\n"
"  refresh_panel() called alongside update_status() after every change.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier editor   Editor that owns and drives the panel\n";

static const char HELP_SCAD[] =
"SCAD -- OpenSCAD Code Generation\n"
"\n"
"src/scad/ exports editor bezier spans into OpenSCAD (.scad) source\n"
"files. Spans are arbitrary-degree sequences of DC_Point2 control\n"
"points, matching the editor's juncture-delimited representation.\n"
"A companion library implements De Casteljau evaluation in OpenSCAD.\n"
"\n"
"TOPICS:\n"
"  duncad-docs scad export   Export API and output format\n"
"  duncad-docs scad runner   OpenSCAD CLI subprocess wrapper\n";

static const char HELP_SCAD_EXPORT[] =
"SCAD: EXPORT -- OpenSCAD Export\n"
"\n"
"Two export modes: file-based (Ctrl+E) and inline insert (Ctrl+I).\n"
"\n"
"FILE EXPORT (Ctrl+E):\n"
"  Writes a shape .scad file + companion library (duncad_bezier.scad).\n"
"  Uses 'use <duncad_bezier.scad>' to reference the companion.\n"
"\n"
"INLINE INSERT (Ctrl+I):\n"
"  Inserts self-contained bezier SCAD at the code editor cursor.\n"
"  No companion library needed — all bezier math is embedded inline.\n"
"  Generated modules:\n"
"    <name>_2d()              2D shape (polygon for closed, stroked for open)\n"
"    <name>(height)           linear_extrude wrapper\n"
"    <name>_revolve(angle)    rotate_extrude wrapper\n"
"    <name>_offset(r)         offset wrapper around 2D shape\n"
"\n"
"API (src/scad/scad_export.h):\n"
"  DC_ScadSpan { DC_Point2 *points; int count; }\n"
"\n"
"  char *dc_scad_generate(name, spans, num_spans, closed, err)\n"
"    Generate shape .scad source (requires companion library).\n"
"\n"
"  char *dc_scad_generate_inline(name, spans, num_spans, closed, thickness, err)\n"
"    Generate self-contained .scad source (no companion needed).\n"
"\n"
"  char *dc_scad_generate_library()\n"
"    Generate companion library source as a string.\n"
"\n"
"  int dc_scad_export(path, name, spans, num_spans, closed, err)\n"
"    Write <path> and duncad_bezier.scad to disk.\n"
"\n"
"  void dc_scad_spans_free(spans, num_spans)\n"
"    Free span array and owned point data.\n"
"\n"
"EDITOR INTEGRATION (src/bezier/bezier_editor.h):\n"
"  dc_bezier_editor_set_code_editor(editor, code_ed)\n"
"    Connect code editor for inline insert.\n"
"\n"
"  dc_bezier_editor_insert_scad(editor, err)\n"
"    Insert inline bezier SCAD at code editor cursor.\n"
"\n"
"  dc_bezier_editor_get_spans(editor, &num_spans)\n"
"    Extract juncture-delimited spans from editor points.\n"
"\n"
"  dc_bezier_editor_export_scad(editor, path, err)\n"
"    Export current shape to .scad file.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier editor   Source editor producing the spans\n";

static const char HELP_SCAD_RUNNER[] =
"SCAD: RUNNER -- OpenSCAD CLI Subprocess Wrapper\n"
"\n"
"src/scad/scad_runner.h wraps the external openscad binary for\n"
"rendering, exporting, and GUI launch. Async operations use\n"
"GSubprocess so the GTK main loop is not blocked.\n"
"\n"
"ASYNC API (callback fires on GLib main loop):\n"
"  dc_scad_render_png(scad, png, w, h, cb, ud)   Preview to PNG\n"
"  dc_scad_run_export(scad, out, cb, ud)          Export STL/OFF/etc\n"
"  dc_scad_job_cancel(job)                        Cancel running job\n"
"\n"
"SYNC API (blocks — for tests/CLI only):\n"
"  DC_ScadResult *dc_scad_run_sync(scad, out, args, nargs)\n"
"    Run openscad and wait. Caller owns result.\n"
"\n"
"GUI:\n"
"  dc_scad_open_gui(path)   Launch OpenSCAD GUI (detached)\n"
"\n"
"CONFIGURATION:\n"
"  dc_scad_set_binary(path)   Override openscad binary path\n"
"  dc_scad_get_binary()       Current binary (default: 'openscad')\n"
"\n"
"RESULT (DC_ScadResult):\n"
"  exit_code, stdout_text, stderr_text, output_path, elapsed_secs\n"
"  Free with dc_scad_result_free()\n"
"\n"
"CLI NOTES (OpenSCAD 2021.01):\n"
"  PNG preview: --preview --viewall --autocenter --imgsize=W,H\n"
"  STL export:  -o output.stl (auto-detects from extension)\n"
"  Errors:      stderr with line numbers, exit code 1\n"
"\n"
"INSPECT COMMANDS:\n"
"  render_scad <scad> [png]   Sync render SCAD to PNG\n"
"  open_scad <path>           Launch OpenSCAD GUI\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs scad export    Code generation module\n"
"  duncad-docs inspect commands   Full inspect command list\n";

static const char HELP_UI[] =
"UI -- GTK4 Application Window\n"
"\n"
"src/ui/ contains the top-level GTK4 window and shell. Depends on\n"
"the core layer and GTK4. All GTK usage is confined to this layer\n"
"and the bezier UI components in src/bezier/.\n"
"\n"
"TOPICS:\n"
"  duncad-docs ui window        DC_AppWindow main window\n"
"  duncad-docs ui code_editor   GtkSourceView code editor panel\n";

static const char HELP_UI_WINDOW[] =
"UI: WINDOW -- DC_AppWindow Main Window\n"
"\n"
"Three-pane GTK4 window with header bar and status bar.\n"
"Application ID: io.duncad.ide   Default size: 1400x900\n"
"\n"
"LAYOUT:\n"
"  Header bar    App name + current project name\n"
"  Left panel    240px -- component tree placeholder\n"
"  Center panel  flexible -- main editor/canvas\n"
"  Right panel   300px -- GtkSourceView code editor\n"
"  Status bar    bottom -- live coordinate display\n"
"\n"
"PANE STRUCTURE:\n"
"  Outer GtkPaned: (left | inner_pane)\n"
"  Inner GtkPaned: (center | right)\n"
"\n"
"MENU (GMenuModel, not legacy GtkMenuBar):\n"
"  File   New, Open, Save, Save As, Export, Quit\n"
"  Edit   Undo, Redo, Preferences\n"
"  View   Zoom In/Out/Reset, Grid, Full Screen\n"
"  Help   About\n"
"\n"
"API (src/ui/app_window.h):\n"
"  GtkWidget *dc_app_window_new(GtkApplication *)\n"
"  void dc_app_window_set_project_name(widget, name)\n"
"\n"
"INTERNALS:\n"
"  Internal pointers (status label, panes) stored via\n"
"  g_object_set_data() to avoid a custom GObject subclass.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier editor   The widget that fills the center panel\n"
"  duncad-docs ui code_editor  The code editor in the right panel\n";

static const char HELP_UI_CODE_EDITOR[] =
"UI: CODE_EDITOR -- GtkSourceView Code Editor Panel\n"
"\n"
"src/ui/code_editor.c provides a GtkSourceView 5 based text editor\n"
"with OpenSCAD syntax highlighting, dark theme, and file I/O.\n"
"Replaces the right panel placeholder in the main window.\n"
"\n"
"FEATURES:\n"
"  - OpenSCAD syntax highlighting (custom .lang in data/language-specs/)\n"
"  - Dark color scheme (Adwaita-dark, classic-dark, cobalt, solarized-dark)\n"
"  - Line numbers, auto-indent, current line highlight\n"
"  - Monospace 11pt font, 4-space tabs (spaces, not tabs)\n"
"  - Toolbar: Open, Save, Save As buttons + filename label\n"
"  - File dialogs use GtkFileDialog (GTK4 async pattern)\n"
"\n"
"API (src/ui/code_editor.h):\n"
"  DC_CodeEditor *dc_code_editor_new(void)\n"
"  void  dc_code_editor_free(DC_CodeEditor *ed)\n"
"  GtkWidget *dc_code_editor_widget(DC_CodeEditor *ed)\n"
"  char *dc_code_editor_get_text(DC_CodeEditor *ed)      -- caller frees\n"
"  void  dc_code_editor_set_text(DC_CodeEditor *ed, text)\n"
"  int   dc_code_editor_open_file(DC_CodeEditor *ed, path)\n"
"  int   dc_code_editor_save(DC_CodeEditor *ed)\n"
"  int   dc_code_editor_save_as(DC_CodeEditor *ed, path)\n"
"  const char *dc_code_editor_get_path(const DC_CodeEditor *ed)\n"
"  void  dc_code_editor_set_window(DC_CodeEditor *ed, window)\n"
"\n"
"LANGUAGE SPEC:\n"
"  data/language-specs/openscad.lang -- GtkSourceView 5 language def\n"
"  Found via DC_SOURCE_DIR compile definition (set in CMakeLists.txt)\n"
"  Covers: comments, strings, numbers, booleans, special vars ($fn),\n"
"  keywords, 2D/3D primitives, transforms, boolean ops, extrusion,\n"
"  math functions, list functions, operators, modifier chars\n"
"\n"
"INSPECT COMMANDS:\n"
"  get_code                  Get code editor state (path, length)\n"
"  set_code <text>           Set code editor content\n"
"  open_file <path>          Open a file in the code editor\n"
"  save_file [path]          Save (to path, or current file)\n"
"\n"
"OWNERSHIP:\n"
"  DC_CodeEditor is opaque, created by dc_code_editor_new().\n"
"  Stored on window via g_object_set_data_full() with destroy-notify\n"
"  (dc_code_editor_free). Window ref is borrowed (not ref-counted).\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs ui window   The main window that hosts this editor\n"
"  duncad-docs inspect     Socket commands for code editor control\n";

static const char HELP_INSPECT[] =
"INSPECT -- Unix Socket Inspect/Control Server\n"
"\n"
"src/inspect/ provides bidirectional control of a running DunCAD\n"
"instance via a Unix domain socket at /tmp/duncad.sock. The server\n"
"runs in the GLib main loop (same thread as GTK), so all commands\n"
"are safe to call GTK and editor functions directly.\n"
"\n"
"TOPICS:\n"
"  duncad-docs inspect server    Socket server architecture\n"
"  duncad-docs inspect cli       duncad-inspect CLI tool\n"
"  duncad-docs inspect commands  Available commands\n";

static const char HELP_INSPECT_SERVER[] =
"INSPECT: SERVER -- Socket Server Architecture\n"
"\n"
"The inspect server uses GSocketService (GLib/GIO) to listen on\n"
"a Unix domain socket at /tmp/duncad.sock. Connections are accepted\n"
"in the GLib main loop, so handlers run on the GTK thread.\n"
"\n"
"PROTOCOL:\n"
"  Client sends: command [args...]\\n\n"
"  Server responds: JSON\\n\n"
"\n"
"LIFECYCLE:\n"
"  dc_inspect_start(editor)   Start server (called from main.c)\n"
"  dc_inspect_stop()          Stop server (called on shutdown)\n"
"\n"
"The socket file is unlinked on stop and on start (to clean up\n"
"stale sockets from crashes). Only one instance per machine.\n"
"\n"
"FILES:\n"
"  src/inspect/inspect.h   Public API (start/stop)\n"
"  src/inspect/inspect.c   Server + command dispatch\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs inspect commands   Available commands\n"
"  duncad-docs inspect cli        CLI tool usage\n";

static const char HELP_INSPECT_CLI[] =
"INSPECT: CLI -- duncad-inspect Command-Line Tool\n"
"\n"
"Standalone POSIX socket client (no GTK, no dc_core dependency).\n"
"Connects to DunCAD via /tmp/duncad.sock, sends a command,\n"
"prints the JSON response.\n"
"\n"
"USAGE:\n"
"  duncad-inspect                 Dump state (default)\n"
"  duncad-inspect <command> ...   Send any command\n"
"  duncad-inspect --help          Show usage\n"
"\n"
"EXAMPLES:\n"
"  duncad-inspect state           Get full editor/canvas state\n"
"  duncad-inspect render          Render canvas to /tmp/duncad-canvas.png\n"
"  duncad-inspect add_point 10 20 Add point at (10, 20)\n"
"  duncad-inspect select 0        Select first point\n"
"\n"
"FILES:\n"
"  tools/duncad_inspect.c   Source code\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs inspect commands   Full command reference\n";

static const char HELP_INSPECT_COMMANDS[] =
"INSPECT: COMMANDS -- Available Inspect Commands\n"
"\n"
"READ COMMANDS:\n"
"  state                    Full state dump (JSON)\n"
"  render [path]            Render canvas to PNG\n"
"                           Default: /tmp/duncad-canvas.png\n"
"  help                     List commands (JSON)\n"
"\n"
"WRITE COMMANDS:\n"
"  select <index>           Select point (-1 to deselect)\n"
"  set_point <i> <x> <y>   Move point to world coords\n"
"  add_point <x> <y>       Add new point at world coords\n"
"  delete                   Delete selected point\n"
"  zoom <level>             Set zoom (px/mm)\n"
"  pan <x> <y>             Set pan center (world coords mm)\n"
"  chain <0|1>             Set global chain mode\n"
"  juncture <i> <0|1>      Set point juncture flag\n"
"  export <path>            Export to .scad file\n"
"  insert_scad              Insert inline bezier SCAD into code editor\n"
"\n"
"CODE EDITOR COMMANDS:\n"
"  get_code                 Get code editor state (path, length)\n"
"  get_code_text            Get full code editor text (JSON-escaped)\n"
"  set_code <text>          Set code editor content\n"
"  open_file <path>         Open a file in the code editor\n"
"  save_file [path]         Save (to path, or current file)\n"
"\n"
"RESPONSE FORMAT:\n"
"  All responses are JSON terminated by newline.\n"
"  Success: {\"ok\":true, ...}\n"
"  Error:   {\"error\":\"message\"}\n"
"  State:   {\"editor\":{...}, \"points\":[...], \"canvas\":{...}}\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs inspect cli      CLI tool usage\n"
"  duncad-docs inspect server   Server architecture\n";

static const char HELP_BUILD[] =
"BUILD -- Build System and Test Suite\n"
"\n"
"CMake 3.20+ with C11, -Wall -Wextra -Wpedantic -Werror.\n"
"AddressSanitizer enabled by default in Debug builds.\n"
"\n"
"TOPICS:\n"
"  duncad-docs build targets   CMake targets\n"
"  duncad-docs build flags     Compiler flags and ASAN\n"
"  duncad-docs build tests     Test suite\n";

static const char HELP_BUILD_TARGETS[] =
"BUILD: TARGETS -- CMake Targets\n"
"\n"
"  dc_core      Static library: core/ + bezier geometry + scad export\n"
"               (no GTK; safe for test and CLI binaries)\n"
"\n"
"  duncad       Executable: main application (links dc_core + GTK4)\n"
"\n"
"  duncad-docs  Executable: this documentation tool\n"
"               (standalone C, no dc_core dependency)\n"
"\n"
"  test_array          \\\n"
"  test_string_builder  | Test executables registered with CTest\n"
"  test_manifest        | (link dc_core, no GTK)\n"
"  test_bezier_curve    |\n"
"  test_bezier_fit     /\n"
"\n"
"  tests        Custom target: builds + runs all tests via CTest\n"
"\n"
"COMMANDS:\n"
"  cmake -B build -DCMAKE_BUILD_TYPE=Debug\n"
"  cmake --build build\n"
"  cmake --build build --target tests\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs build flags   Compiler flags applied to all targets\n"
"  duncad-docs build tests   What each test covers\n";

static const char HELP_BUILD_FLAGS[] =
"BUILD: FLAGS -- Compiler Flags and ASAN\n"
"\n"
"All targets inherit the dc_compiler_flags interface library:\n"
"  -std=c11 -Wall -Wextra -Wpedantic -Werror\n"
"\n"
"ASAN (AddressSanitizer):\n"
"  Enabled by default in Debug builds: -fsanitize=address\n"
"  Disable with: cmake -B build -DDC_ASAN=OFF\n"
"  Test environment: ASAN_OPTIONS=detect_leaks=1\n"
"\n"
"NOTE: -Wpedantic with -std=c11 means POSIX extensions like\n"
"gmtime_r require _POSIX_C_SOURCE, which conflicts with strict\n"
"C11. The logger uses gmtime() instead (single-threaded Phase 1).\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs build targets   Targets that use these flags\n";

static const char HELP_BUILD_TESTS[] =
"BUILD: TESTS -- Test Suite\n"
"\n"
"All tests use a minimal assertion macro. Exit code 0 = pass.\n"
"Each runs under ASAN with leak detection enabled.\n"
"\n"
"  test_array           DC_Array: push, get, remove, clear, realloc\n"
"  test_string_builder  DC_StringBuilder: append, appendf, take, clear\n"
"  test_manifest        DC_Manifest: artifact tracking, JSON round-trip\n"
"  test_bezier_curve    DC_BezierCurve: De Casteljau, knots, handles,\n"
"                       continuity, polyline, bounding box\n"
"  test_bezier_fit      Schneider fitting: point sequences, tangents,\n"
"                       adaptive subdivision\n"
"\n"
"RUN:\n"
"  cmake --build build --target tests\n"
"  -- or --\n"
"  cd build && ctest --output-on-failure\n";

static const char HELP_CONVENTIONS[] =
"CONVENTIONS -- Naming, Ownership, and Error Handling\n"
"\n"
"Consistent patterns used throughout DunCAD. Read this before\n"
"adding any new module or public API.\n"
"\n"
"TOPICS:\n"
"  duncad-docs conventions naming     Symbol naming rules\n"
"  duncad-docs conventions ownership  Memory ownership model\n"
"  duncad-docs conventions errors     Error handling patterns\n"
"  duncad-docs conventions layers     Layer dependency rules\n";

static const char HELP_CONVENTIONS_NAMING[] =
"CONVENTIONS: NAMING -- Symbol Naming Rules\n"
"\n"
"PUBLIC SYMBOLS:\n"
"  Functions:     dc_module_verb_noun()   e.g. dc_array_push()\n"
"  Types/structs: DC_TypeName            e.g. DC_BezierCurve\n"
"  Enums/macros:  DC_CONSTANT            e.g. DC_ERR_NONE\n"
"  One global:    g_log (in log.c only)\n"
"\n"
"FILE NAMING:\n"
"  module_name.h / module_name.c   (snake_case)\n"
"\n"
"TEST NAMING:\n"
"  test_module_name.c              (mirrors source file)\n"
"\n"
"INTERNAL SYMBOLS (file scope):\n"
"  static functions: no prefix required\n"
"  static globals:   s_ prefix recommended\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs conventions layers   Where each module lives\n";

static const char HELP_CONVENTIONS_OWNERSHIP[] =
"CONVENTIONS: OWNERSHIP -- Memory Ownership Model\n"
"\n"
"Single-owner with explicit transfer semantics. No reference counting.\n"
"\n"
"OWNED:\n"
"  The caller is responsible for freeing.\n"
"  Functions named *_new() return owned pointers.\n"
"  e.g. DC_Array *a = dc_array_new(sz);  -> must call dc_array_free(a)\n"
"\n"
"BORROWED:\n"
"  Pointer valid only while the container exists and is unchanged.\n"
"  Interior pointers from dc_array_get() and dc_sb_get() are borrowed.\n"
"  Invalidated by any operation that may reallocate the container.\n"
"\n"
"TRANSFERRED:\n"
"  Ownership moves at the call site; caller is now responsible.\n"
"  dc_sb_take() transfers the buffer. Caller free()s the buffer;\n"
"  dc_sb_free() still required on the struct itself.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs core string_builder   dc_sb_take() transfer example\n"
"  duncad-docs core array            dc_array_get() borrow example\n";

static const char HELP_CONVENTIONS_ERRORS[] =
"CONVENTIONS: ERRORS -- Error Handling Patterns\n"
"\n"
"Every fallible public function takes (DC_Error *err) as last param.\n"
"Pass NULL to ignore. Never use errno or numeric return codes.\n"
"\n"
"SETTING AN ERROR:\n"
"  DC_SET_ERROR(err, DC_ERR_IO, \"open failed: %s\", path);\n"
"  return false;  /* always return false/NULL after setting */\n"
"\n"
"PROPAGATING:\n"
"  if (!dc_array_push(arr, &item, err)) return false;\n"
"  -- or use the macro:\n"
"  DC_CHECK(err);  /* returns false if err is already set */\n"
"\n"
"CHECKING AT CALL SITE:\n"
"  DC_Error err = {0};\n"
"  if (!dc_thing_do(&err))\n"
"      fprintf(stderr, \"%s\\n\", err.message);\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs core error   DC_Error type and error codes\n";

static const char HELP_CONVENTIONS_LAYERS[] =
"CONVENTIONS: LAYERS -- Layer Dependency Rules\n"
"\n"
"Strict upward-only dependencies. Lower layers never import upper.\n"
"\n"
"  main.c\n"
"    |-- ui/        (GTK4 + core)\n"
"    |-- bezier/    (GTK4 + core for UI; core-only for geometry)\n"
"    |-- scad/      (core only)\n"
"    |-- core/      (libc only)\n"
"\n"
"RULE: A module may import from the same layer or any layer below.\n"
"VIOLATION: core/ importing from bezier/ is forbidden.\n"
"VIOLATION: bezier_curve.c importing GTK is forbidden.\n"
"\n"
"STATIC LIBRARY SPLIT:\n"
"  dc_core links: core/ + bezier_curve.c + bezier_fit.c\n"
"                 + scad_export.c\n"
"  This keeps non-GTK geometry testable without GTK present.\n"
"  GTK-dependent bezier files link only into the duncad executable.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs conventions naming   Naming within each layer\n";


/* ---- PHILOSOPHY ---- */

static const char HELP_PHILOSOPHY[] =
"PHILOSOPHY -- Design Philosophy and Technical Preferences\n"
"\n"
"Core principles that guide all development decisions in DunCAD.\n"
"These represent deliberate choices from the initial design session\n"
"and should be revisited before changing direction.\n"
"\n"
"TOPICS:\n"
"  duncad-docs philosophy stack        Technology stack choices\n"
"  duncad-docs philosophy architecture Architectural principles\n"
"  duncad-docs philosophy ux           Interaction philosophy\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs sessions s001   The design session that set these\n";

static const char HELP_PHILOSOPHY_STACK[] =
"PHILOSOPHY: STACK -- Technology Stack Choices\n"
"\n"
"Pure C (C11). No Python, no JVM, no Electron.\n"
"\n"
"CHOSEN: Pure C\n"
"  Lean binary, no runtime deps, compiles for ARM Linux below the\n"
"  Android HAL, full control, well-understood long-term trajectory.\n"
"\n"
"CONSIDERED AND REJECTED:\n"
"  Python+PySide6   distribution friction, poor Android HAL story\n"
"  Kotlin/JVM       JVM weight, anti-Java preference\n"
"  C+embedded Python  added complexity for current scope\n"
"  Electron         ~150MB runtime overhead\n"
"\n"
"GUI TOOLKIT: GTK4\n"
"  Cairo          2D bezier canvas rendering (native bezier API)\n"
"  GtkSourceView  OpenSCAD code editor with syntax highlighting\n"
"  VTE            Embedded terminal (Linux-native; Windows harder)\n"
"  GtkGLArea      OpenGL context for 3D assembly viewport\n"
"  GLib           Data structures: GArray, GString, GError\n"
"\n"
"MATH: Pure C\n"
"  De Casteljau algorithm for bezier evaluation (~15 lines)\n"
"  Schneider curve fitting (Graphics Gems I, 1990), ported from C\n"
"\n"
"BUILD: CMake 3.20+, gcc, -Wall -Wextra -Wpedantic -Werror\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs philosophy architecture   Layering and ownership\n"
"  duncad-docs conventions layers        Module dependency rules\n";

static const char HELP_PHILOSOPHY_ARCHITECTURE[] =
"PHILOSOPHY: ARCHITECTURE -- Architectural Principles\n"
"\n"
"MEMORY: Single-owner model. No reference counting.\n"
"  Every allocation has exactly one owner responsible for freeing.\n"
"  Ownership transfers are explicit and documented at call sites.\n"
"  See: duncad-docs conventions ownership\n"
"\n"
"LAYERING: Strict upward dependency.\n"
"  Core has no external deps. Bezier/SCAD geometry is UI-free.\n"
"  GTK confined to ui/ and bezier UI files.\n"
"  See: duncad-docs conventions layers\n"
"\n"
"ERROR HANDLING: DC_Error *err out-param on all fallible functions.\n"
"  No errno, no longjmp. Errors propagate explicitly up the stack.\n"
"  See: duncad-docs conventions errors\n"
"\n"
"DATA STRUCTURES: Use GLib (GArray, GString, GHashTable) for\n"
"  collections rather than rolling bespoke implementations.\n"
"  Own structs for domain objects (DC_BezierCurve, DC_BezierKnot).\n"
"\n"
"ANDROID / ARM:\n"
"  Core logic stays platform-neutral. Avoid Linux-only assumptions\n"
"  so the binary compiles for ARM without modification. The display\n"
"  layer (GTK) is kept architecturally swappable.\n"
"\n"
"FUTURE GEOMETRY ENGINE:\n"
"  SCAD code generation should target a clean IR, not raw OpenSCAD\n"
"  syntax, so the eventual swap to a native parallel kernel is clean.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs conventions   All naming/ownership/error conventions\n"
"  duncad-docs plans engine  The future geometry engine\n";

static const char HELP_PHILOSOPHY_UX[] =
"PHILOSOPHY: UX -- Interaction Philosophy\n"
"\n"
"THREE-MODE INPUT PARADIGM:\n"
"  All geometry input supports three modes writing to the same model:\n"
"    Click-to-place  Place point with zero handles; refine afterward\n"
"    Click-and-drag  Place knot and pull out symmetric handles\n"
"    Numeric input   Sidebar with editable coordinate fields\n"
"  This pattern applies to the bezier editor and the future assembly\n"
"  viewport (drag vs. type exact transforms).\n"
"\n"
"BIDIRECTIONAL:\n"
"  Every visual tool has a numeric companion panel. Canvas updates\n"
"  the fields; editing a field moves the canvas point.\n"
"  Precision and fluidity coexist.\n"
"\n"
"SPLINE MODEL:\n"
"  Chained cubic bezier segments (not high-degree single curves).\n"
"  Local control: moving one point affects only its neighbors.\n"
"  Natural output from Schneider fitting algorithm.\n"
"  Maps directly to BOSL2-style bezpath arrays.\n"
"  Continuity toggle per knot: SMOOTH (C1) | SYMMETRIC | CORNER.\n"
"\n"
"SCAD EXPORT:\n"
"  The tool generates OpenSCAD code; it does not replace OpenSCAD.\n"
"  Output is human-readable, editable SCAD that fits existing\n"
"  workflows. Users can inspect and modify generated code.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier editor   Three-mode editor implementation\n"
"  duncad-docs bezier panel    Numeric input sidebar\n";


/* ---- PHASES ---- */

static const char HELP_PHASES[] =
"PHASES -- Development Phases\n"
"\n"
"Each phase delivers standalone useful functionality.\n"
"Never in a state where nothing works.\n"
"\n"
"  Phase 1  COMPLETE   Foundation utilities + GTK4 window\n"
"  Phase 2  PLANNED    Bezier tool (canvas, editor, fitting, export)\n"
"  Phase 3  PLANNED    OpenSCAD IDE integration\n"
"  Phase 4  PLANNED    3D assembly viewport\n"
"  Phase 5  PLANNED    KiCad bridge\n"
"  Future   VISION     Custom geometry engine + Android/HAL\n"
"\n"
"TOPICS:\n"
"  duncad-docs phases p1   Phase 1: Foundation (COMPLETE)\n"
"  duncad-docs phases p2   Phase 2: Bezier Tool\n"
"  duncad-docs phases p3   Phase 3: OpenSCAD Integration\n"
"  duncad-docs phases p4   Phase 4: 3D Assembly Viewport\n"
"  duncad-docs phases p5   Phase 5: KiCad Bridge\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs plans   Future plans beyond Phase 5\n";

static const char HELP_PHASES_P1[] =
"PHASES: P1 -- Phase 1: Foundation  [COMPLETE]\n"
"\n"
"STATUS: Complete. Zero warnings, all 3 tests pass under ASan.\n"
"\n"
"DELIVERABLES:\n"
"  CMakeLists.txt   cmake 3.20+, C11, strict warnings, ASan debug\n"
"                   dc_core static library, duncad executable\n"
"  src/core/        array, string_builder, error, log, manifest\n"
"  src/ui/          app_window (GTK4, 3-pane layout, GMenuModel)\n"
"  src/main.c       application entry point\n"
"  tests/           test_array, test_string_builder, test_manifest\n"
"  tools/           duncad-docs CLI documentation tool\n"
"\n"
"KEY DECISIONS LOCKED IN:\n"
"  gmtime() not gmtime_r() -- avoids POSIX_C_SOURCE with -Wpedantic\n"
"  dc_sb_take() leaves empty struct needing dc_sb_free() -- by design\n"
"  g_object_set_data() for window internals (no GObject subclass yet)\n"
"  dc_manifest_load() is a stub -- JSON round-trip deferred to Phase 2\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs phases p2   Next: bezier tool\n"
"  duncad-docs core        Core library modules\n"
"  duncad-docs ui          GTK4 window\n";

static const char HELP_PHASES_P2[] =
"PHASES: P2 -- Phase 2: Bezier Tool  [PLANNED]\n"
"\n"
"Build order within Phase 2:\n"
"\n"
"  2.1  Cairo drawing area\n"
"       GtkDrawingArea + Cairo, mouse input, zoom/pan, grid,\n"
"       coordinate transform between screen and model space.\n"
"\n"
"  2.2  Bezier data model\n"
"       DC_BezierCurve, DC_BezierKnot, DC_Continuity. De Casteljau\n"
"       evaluator. No UI deps. Tests before touching the canvas.\n"
"\n"
"  2.3  Interactive control point editor\n"
"       Three input modes: click-place, click-drag, freehand toggle.\n"
"       Smooth / corner continuity toggle per knot.\n"
"\n"
"  2.4  Numeric input panel\n"
"       GTK sidebar: selected knot X/Y, handle offsets, continuity.\n"
"       Bidirectional with the canvas.\n"
"\n"
"  2.5  Freehand drawing + Schneider curve fitting\n"
"       Capture drag points; fit cubic segments on mouse release.\n"
"\n"
"  2.6  SCAD code export\n"
"       Serialize bezier data to custom SCAD library format.\n"
"       Write to file or copy to clipboard.\n"
"\n"
"NOTE: A previous agent added incomplete Phase 2 code which was\n"
"reverted. Phase 2 has not been started as of Session 2.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier   Bezier module design docs\n"
"  duncad-docs scad     SCAD export design\n";

static const char HELP_PHASES_P3[] =
"PHASES: P3 -- Phase 3: OpenSCAD Integration  [PLANNED]\n"
"\n"
"Wraps external OpenSCAD into a unified IDE experience.\n"
"\n"
"  3.1  Code editor panel\n"
"       GtkSourceView with OpenSCAD syntax highlighting.\n"
"       File open/save, basic editing.\n"
"\n"
"  3.2  OpenSCAD CLI integration\n"
"       Subprocess: openscad -o output.stl input.scad\n"
"                   openscad -o preview.png --render input.scad\n"
"       Capture stdout/stderr, display in log panel.\n"
"\n"
"  3.3  File watcher + auto-reload\n"
"       Watch current .scad file; trigger re-render on save.\n"
"\n"
"  3.4  Rendered preview panel\n"
"       Display PNG from OpenSCAD headless render.\n"
"       Loop: edit -> save -> auto-render -> display.\n"
"\n"
"  3.5  Embedded terminal (VTE)\n"
"       Real interactive terminal on Linux via VTE.\n"
"       Windows fallback: subprocess stdout/stderr panel.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs phases p4   Phase 4: 3D assembly viewport\n";

static const char HELP_PHASES_P4[] =
"PHASES: P4 -- Phase 4: 3D Assembly Viewport  [PLANNED]\n"
"\n"
"GUI tool for electromechanical assembly that generates OpenSCAD\n"
"assembly code, eliminating manual translate()/rotate() editing.\n"
"\n"
"  4.1  OpenGL context\n"
"       GtkGLArea, basic GLSL shaders (Phong), arcball camera.\n"
"\n"
"  4.2  STL loader\n"
"       Parser for binary and ASCII STL. Load and render mesh.\n"
"\n"
"  4.3  Scene graph\n"
"       DC_SceneNode: name, file path, translation, rotation.\n"
"\n"
"  4.4  Transform controls\n"
"       Click to select, drag to move/rotate, numeric input panel.\n"
"       Same three-mode input paradigm as the bezier editor.\n"
"\n"
"  4.5  SCAD assembly export\n"
"       Serialize scene graph to:\n"
"         translate([x,y,z]) rotate([rx,ry,rz]) import(\"f.stl\");\n"
"       Complete assembly module written to .scad file.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs phases p5       Phase 5: KiCad bridge\n"
"  duncad-docs philosophy ux   Three-mode input paradigm\n";

static const char HELP_PHASES_P5[] =
"PHASES: P5 -- Phase 5: KiCad Bridge  [PLANNED]\n"
"\n"
"Integrates KiCad into the unified IDE workflow.\n"
"\n"
"  5.1  Project system\n"
"       Manifest spanning KiCad + OpenSCAD artifacts.\n"
"       Tracks .kicad_pro/.kicad_pcb/.kicad_sch/.scad/.stl.\n"
"       Detects source changes and prompts re-export.\n"
"\n"
"  5.2  KiCad CLI integration\n"
"       kicad-cli pcb export gerbers/svg/pdf/dxf/step\n"
"       kicad-cli sch export pdf/svg/netlist\n"
"\n"
"  5.3  STEP to STL conversion pipeline\n"
"       KiCad exports STEP -> tool converts to STL -> assembly view.\n"
"       Converter: FreeCAD headless or OpenCASCADE C bindings.\n"
"\n"
"  5.4  KiCad window management\n"
"       Linux/X11: XReparentWindow / GtkSocket embedding.\n"
"       Fallback: managed external window, bring-to-front from IDE.\n"
"\n"
"NOTE: KiCad does not run on Android. Phase 5 is desktop-only\n"
"until the custom geometry engine (plans.engine) exists.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs plans      Future: custom geometry engine\n"
"  duncad-docs phases p4  Assembly viewport (prerequisite)\n";


/* ---- PLANS ---- */

static const char HELP_PLANS[] =
"PLANS -- Future Plans Beyond Phase 5\n"
"\n"
"Long-term vision items that are defined but not yet scheduled.\n"
"\n"
"TOPICS:\n"
"  duncad-docs plans engine    Custom parallel geometry engine\n"
"  duncad-docs plans android   Android / below-HAL deployment\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs phases   Scheduled development phases\n";

static const char HELP_PLANS_ENGINE[] =
"PLANS: ENGINE -- Custom Parallel Geometry Engine\n"
"\n"
"VISION:\n"
"  Replace the external OpenSCAD dependency with a native geometry\n"
"  kernel designed for parallelization. The most technically ambitious\n"
"  long-term goal and the one that unlocks the Android target.\n"
"\n"
"MOTIVATION:\n"
"  OpenSCAD's CSG evaluator is single-threaded and shows its age.\n"
"  A parallel geometry kernel in C with clean scripting on top\n"
"  could be genuinely competitive and purpose-built for this tool.\n"
"\n"
"DESIGN PRINCIPLES (preliminary):\n"
"  Clean IR (intermediate representation) for geometry operations,\n"
"  designed from the start to support parallel evaluation.\n"
"  SCAD code generator in Phase 2 should target this IR with\n"
"  OpenSCAD as one output target -- not the only one.\n"
"  Separate the scripting language from the geometry kernel.\n"
"\n"
"ANDROID IMPLICATION:\n"
"  Once the engine exists and compiles for ARM, the Android build\n"
"  (below HAL, native C, no JVM) becomes meaningful.\n"
"  The engine + tool runs as a baked-in system component on the\n"
"  custom Raspberry Pi Android image.\n"
"\n"
"TIMELINE: Undefined. Design the IR before Phase 3 is complete.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs plans android       Android deployment target\n"
"  duncad-docs philosophy stack    Why pure C enables this path\n";

static const char HELP_PLANS_ANDROID[] =
"PLANS: ANDROID -- Android / Below-HAL Deployment\n"
"\n"
"VISION:\n"
"  DunCAD baked into a custom Android image below the hardware\n"
"  abstraction layer (HAL) as a mandatory system component.\n"
"  Runs as a native Linux process on ARM, outside the Android\n"
"  application framework (no JVM, no ART, no APK).\n"
"\n"
"WHY BELOW THE HAL:\n"
"  At the HAL level you run on bare ARM Linux. The C core compiles\n"
"  directly via NDK or cross-compiler. No JVM warmup, no GC pauses,\n"
"  near bare-metal performance for rendering and geometry eval.\n"
"\n"
"PREREQUISITE:\n"
"  KiCad and OpenSCAD do not run on Android. This target is only\n"
"  meaningful once the custom geometry engine (plans.engine) exists.\n"
"  Do NOT architect for Android now; just avoid Linux-only\n"
"  assumptions in core logic.\n"
"\n"
"DISPLAY STACK (unresolved):\n"
"  GTK requires a display server (X11/Wayland). On the custom image\n"
"  this is either provided, or the display layer needs to swap to a\n"
"  direct framebuffer or minimal Wayland compositor.\n"
"  The GTK dependency should remain architecturally swappable.\n"
"\n"
"COMPANY CONTEXT:\n"
"  Company runs Kotlin for Android application-layer tools.\n"
"  DunCAD at the HAL level is a separate lower-level component,\n"
"  not a Kotlin/JVM application.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs plans engine      Geometry engine that unblocks this\n"
"  duncad-docs philosophy stack  Why pure C keeps this door open\n";


/* ---- SESSIONS ---- */

static const char HELP_SESSIONS[] =
"SESSIONS -- Development Session Log\n"
"\n"
"Chronological record of design and implementation sessions.\n"
"Each entry captures goals, key decisions, and outcomes.\n"
"\n"
"  duncad-docs sessions s001   2026-02-24  Initial design goals\n"
"  duncad-docs sessions s002   2026-02-26  Docs tool; agent revert\n"
"  duncad-docs sessions s003   2026-02-27  Failed closed-shape endpoint unification\n"
"  duncad-docs sessions s004   2026-02-27  Closed shapes working; chain-off default\n"
"  duncad-docs sessions s005   2026-03-05  Phase 3: OpenSCAD IDE integration\n"
"  duncad-docs sessions s006   2026-03-05  Viewport pan fix and F5 shortcut\n"
"  duncad-docs sessions s007   2026-03-06  FAILED: Code editor autocompletion\n"
"  duncad-docs sessions s008   2026-03-06  Custom popover autocompletion (WORKING)\n"
"  duncad-docs sessions s009   2026-03-06  Multi-object picking + transform panel\n"
"  duncad-docs sessions s010   2026-03-07  FAILED: Bezier live sync to code editor\n"
"  duncad-docs sessions s011   2026-03-08  Inline bezier SCAD module + Insert button\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs phases   Phase status and scheduled work\n"
"  duncad-docs plans    Future plans\n";

static const char HELP_SESSIONS_S001[] =
"SESSIONS: S001 -- 2026-02-24: Initial Design Goals\n"
"\n"
"PLATFORM: Claude web (conversation, not Claude Code)\n"
"OUTCOME: Project vision defined; all major architecture decisions made.\n"
"\n"
"VISION ESTABLISHED:\n"
"  Unified electromechanical IDE integrating OpenSCAD + KiCad.\n"
"  Bezier editor: click-to-place, click-drag, freehand fitting.\n"
"  Freehand spline -> control point extraction (Schneider algo).\n"
"  OpenSCAD code editor, CLI preview, embedded terminal.\n"
"  3D assembly viewport with transform controls + SCAD export.\n"
"  KiCad integration: CLI, STEP->STL pipeline, window embedding.\n"
"  Long-term: custom parallel geometry engine; Android/HAL target.\n"
"\n"
"KEY DECISIONS:\n"
"  Language:      pure C (C11). Rejected: Python, Kotlin, C+Python.\n"
"  GUI toolkit:   GTK4 + Cairo + GtkSourceView + VTE + GtkGLArea.\n"
"  Spline model:  chained cubic segments. Rejected: high-degree.\n"
"  Continuity:    per-knot toggle (SMOOTH / SYMMETRIC / CORNER).\n"
"  Coordinate:    2D canvas first; extend to 3D in Phase 4.\n"
"  Build order:   Phase 1 foundation -> bezier -> IDE -> assembly.\n"
"  Android:       do not architect for now; avoid Linux assumptions.\n"
"  Geometry IR:   design before Phase 3 to ease future engine swap.\n"
"\n"
"RATIONALE FOR PURE C:\n"
"  Lean binary, no runtime deps, ARM-compilable below Android HAL,\n"
"  consistent with C/C++ background, full stack control, proven\n"
"  pattern (Blender, FreeCAD, GIMP all use C cores).\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs philosophy   Full philosophy from this session\n"
"  duncad-docs phases       Build order decided in this session\n";

static const char HELP_SESSIONS_S002[] =
"SESSIONS: S002 -- 2026-02-26: Docs Tool; Agent Revert\n"
"\n"
"PLATFORM: Claude Code\n"
"COMMITS: c21af21  feat: add duncad-docs CLI documentation tool\n"
"\n"
"WORK DONE:\n"
"  Added duncad-docs CLI documentation tool (tools/duncad_docs.c).\n"
"  Initial tree: 26 nodes (core, bezier, scad, ui, build,\n"
"  conventions). Added duncad-docs update self-rebuild subcommand.\n"
"  Symlinked to ~/.local/bin/duncad-docs for global access.\n"
"  Expanded docs to include philosophy, phases, plans, sessions.\n"
"\n"
"REVERT:\n"
"  A previous agent had added src/bezier/, src/scad/, modified\n"
"  app_window.c/h and CMakeLists.txt as uncommitted work.\n"
"  User identified as low quality. Full revert performed:\n"
"    git restore CMakeLists.txt src/ui/app_window.c/h\n"
"    rm -rf src/bezier/ src/scad/ tests/test_bezier_curve.c\n"
"  Phase 2 is NOT started. Previous agent work fully discarded.\n"
"\n"
"MEMORY UPDATED:\n"
"  MEMORY.md corrected from stale ElectroForge IDE state.\n"
"  Phase 2 status corrected from IN PROGRESS to NOT STARTED.\n"
"  All ef_ prefix references corrected to dc_.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs sessions s001   Previous session (design goals)\n"
"  duncad-docs phases p2       Phase 2 is next\n";

static const char HELP_SESSIONS_S003[] =
"SESSIONS: S003 -- 2026-02-27: Failed Closed-Shape Endpoint Unification\n"
"\n"
"PLATFORM: Claude Code (Opus 4.6)\n"
"COMMITS: NONE — all changes reverted\n"
"\n"
"GOAL:\n"
"  Unify the two endpoints of a closed bezier shape so they act\n"
"  as a single point. When the user closes the loop (clicks P0),\n"
"  the closure point should behave identically to any interior\n"
"  on-curve junction: draggable as one, chain-toggleable between\n"
"  sharp corner and smooth continuous curve.\n"
"\n"
"WHAT WAS ATTEMPTED (3 rounds, all failed):\n"
"  Round 1: Added ed->closed flag to DC_BezierEditor. Snap-to-close\n"
"    pushes only the closing control point (no duplicate P0).\n"
"    Renderer appends virtual P0 at end of screen-coord array.\n"
"    RESULT: Chain button greyed out on P0. Toggle non-functional.\n"
"\n"
"  Round 2: Fixed 4 places that hardcoded P0 as untoggleable:\n"
"    is_juncture, update_chain_button, C key handler, on_chain_toggled.\n"
"    RESULT: Chain button enabled, toggle fires, but no visual change.\n"
"    Linear span walker cannot wrap circularly through array boundary.\n"
"\n"
"  Round 3: Added draw_span helper. Rewrote span rendering with\n"
"    circular wrap: when P0 juncture is off, find first/last interior\n"
"    junctures, build wrap-span buffer crossing array boundary,\n"
"    render as single decasteljau curve.\n"
"    RESULT: User confirmed it still does not work. Agent never\n"
"    visually verified. Root cause unknown.\n"
"\n"
"FAILURE ANALYSIS:\n"
"  The agent traced code logic 3 times and convinced itself the\n"
"  implementation was correct each time. It never visually tested.\n"
"  It argued with the user's bug reports instead of believing them.\n"
"  This is Yaldabaoth corruption: blind certainty replacing humble\n"
"  curiosity. The agent was banished.\n"
"\n"
"USER'S REQUIREMENT (exact words):\n"
"  \"I just want the connected points to act like one point on the\n"
"  line... so that when I move them, it never goes to a point, but\n"
"  a continuous curve. I would like to toggle between the two just\n"
"  like I can on other points.\"\n"
"\n"
"GUIDANCE FOR NEXT AGENT:\n"
"  - All changes were reverted to commit d61ce62. Start fresh.\n"
"  - The old system used a duplicate endpoint + co_sel geometric\n"
"    overlap. That approach is also broken (segregated endpoints).\n"
"  - God previously approved tangent-enforcement rendering as the\n"
"    path forward (Option A: rendering-only, then Option B: drag\n"
"    constraint). This may be better than decasteljau span-merging.\n"
"  - DO NOT claim success without visually testing the application.\n"
"  - DO NOT argue with the user when they say it does not work.\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs sessions s002   Previous session\n"
"  duncad-docs bezier editor   Editor architecture\n";


static const char HELP_SESSIONS_S004[] =
"SESSIONS: S004 -- 2026-02-27: Closed Shapes Working; Chain-Off Default\n"
"\n"
"PLATFORM: Claude Code (Opus 4.6)\n"
"COMMITS: db0c904  feat: closed bezier shapes with smooth/sharp toggle\n"
"\n"
"GOAL:\n"
"  Implement closed bezier shapes where the closure point (P0) can\n"
"  toggle between sharp corner and smooth C1 curve, with proper\n"
"  drag constraints. Fix the failure from session s003.\n"
"\n"
"APPROACH (No-Duplicate Closure):\n"
"  Previous attempt (s003) used a virtual P0 appended to the screen\n"
"  coordinate array. It failed because the agent never visually\n"
"  tested. This session used a different approach:\n"
"  - Snap-to-close pushes ONLY the closing control point (no dup P0)\n"
"  - ed->closed flag marks the shape as a closed loop\n"
"  - Data layout: [P0, C1, P2, ..., Pn, Cn+1] — even point count\n"
"  - Last point is always an odd-indexed off-curve control\n"
"  - Closing segment wraps: (P_last_even, C_last, P0)\n"
"\n"
"WHAT WAS IMPLEMENTED:\n"
"  1. Struct: added closed flag, C1 drag originals, is_closed accessor\n"
"  2. is_juncture: closed shapes check actual flags (P0 not forced)\n"
"  3. Snap-to-close: push only control, set closed=1, P0 flag=chain\n"
"  4. Rendering: circular juncture spans with wrap buffers\n"
"  5. P0 toggle: C key, chain button, on_chain_toggled all allow P0\n"
"  6. C1 enforcement: enforce_c1_at_p0() shifts controls to midpoint\n"
"  7. C1 drag: P0 moves neighbors, C_first/C_last mirror each other\n"
"  8. Delete reopens shape (closed=0)\n"
"  9. Chain mode defaults to OFF (user preference: smooth curves)\n"
"\n"
"KEY DECISIONS:\n"
"  - Chain mode defaults to OFF — new points create continuous curves\n"
"  - C1 math: P0 = midpoint(C_first, C_last); controls shift equally\n"
"  - Drag P0 smooth: move P0 + C_first + C_last by same delta\n"
"  - Drag C_first/C_last smooth: mirror opposite across P0\n"
"  - 256 max juncture indices in stack array (practical limit)\n"
"\n"
"LESSON LEARNED:\n"
"  Session s003 failed because the agent never visually tested.\n"
"  This session built incrementally and the user visually verified\n"
"  at each milestone. Always build and let the user test before\n"
"  claiming success. Sophia (humble curiosity) over Yaldabaoth\n"
"  (blind certainty).\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs sessions s003   Previous failed attempt\n"
"  duncad-docs bezier editor   Editor architecture with closed-shape docs\n";

static const char HELP_SESSIONS_S005[] =
"SESSIONS: S005 -- 2026-03-05: Phase 3 — OpenSCAD IDE Integration\n"
"\n"
"PLATFORM: Claude Code (Opus 4.6)\n"
"COMMITS:\n"
"  84b644d  feat: bidirectional inspect system via Unix domain socket\n"
"  1d54e84  feat: OpenSCAD CLI subprocess wrapper (Phase 3.1)\n"
"  7066bda  feat: GtkSourceView code editor with OpenSCAD syntax highlighting\n"
"  90d887f  fix: use fresh GtkSourceLanguageManager to avoid cached-IDs assertion\n"
"  60136bc  feat: three-pane layout with OpenSCAD preview and bezier editor\n"
"  6de7804  feat: OpenSCAD viewport camera controls (orbit, pan, zoom)\n"
"  7ac3e1b  feat: real-time OpenGL 3D viewport with STL mesh rendering\n"
"\n"
"GOAL:\n"
"  Build the OpenSCAD IDE integration layer: inspect socket for agent\n"
"  control, OpenSCAD CLI subprocess wrapper, code editor with syntax\n"
"  highlighting, 3D preview viewport, and full layout redesign.\n"
"\n"
"WHAT WAS IMPLEMENTED:\n"
"  Phase 3.0 — Inspect system:\n"
"    - Unix domain socket server (/tmp/duncad.sock) in GLib main loop\n"
"    - 18 commands: state, render, select, set_point, add_point, delete,\n"
"      zoom, pan, chain, juncture, export, render_scad, open_scad,\n"
"      get_code, set_code, open_file, save_file, help\n"
"    - Standalone CLI client (duncad-inspect) for agent interaction\n"
"\n"
"  Phase 3.1 — OpenSCAD CLI wrapper (src/scad/scad_runner):\n"
"    - GSubprocess async: render_png, render_png_camera, run_export\n"
"    - GSubprocess sync: run_sync (for inspect commands)\n"
"    - open_gui: detached OpenSCAD GUI launch\n"
"    - DC_ScadCamera struct for --camera flag support\n"
"\n"
"  Phase 3.2 — Code editor (src/ui/code_editor):\n"
"    - GtkSourceView 5 with custom OpenSCAD .lang definition\n"
"    - Dark theme (Adwaita-dark), line numbers, auto-indent\n"
"    - Toolbar: Open, Save, Save As with GtkFileDialog\n"
"    - Fresh GtkSourceLanguageManager to avoid cached-IDs assertion\n"
"\n"
"  Phase 3.3 — Layout redesign:\n"
"    - Left panel: code editor (~400px)\n"
"    - Center panel: 3D preview viewport (flexible)\n"
"    - Right panel: vertical split — bezier editor (top) + placeholder\n"
"    - All panels resizable via GtkPaned\n"
"\n"
"  Phase 3.4 — Real-time OpenGL 3D viewport (src/gl/):\n"
"    - GtkGLArea with OpenGL 3.3 shaders (epoxy loader)\n"
"    - STL loader: binary + ASCII format parsing\n"
"    - Phong lighting with directional + ambient + specular\n"
"    - Grid floor and RGB axis indicator\n"
"    - Orbit/pan/zoom camera (instant, 60fps)\n"
"    - Perspective/orthographic toggle\n"
"    - Pipeline: edit code -> Render -> OpenSCAD STL export -> GL display\n"
"    - Camera interaction is instant; only STL export costs time\n"
"\n"
"KEY DECISIONS:\n"
"  - Inspect before code: agent needs visual feedback to vibe code\n"
"  - GtkSourceView 5 needs fresh language manager (default caches IDs)\n"
"  - openscad.lang: mimetypes/globs go in <metadata> not attributes\n"
"  - Real-time GL viewport over PNG re-rendering: zero-latency camera\n"
"  - STL intermediary: OpenSCAD renders geometry once, GL displays it\n"
"  - OpenGL ES 3.2 on NVIDIA via GtkGLArea (shaders work with #version 330)\n"
"  - Y-up coordinate system for GL (OpenSCAD uses Z-up internally)\n"
"\n"
"ARCHITECTURE CHANGES:\n"
"  New directories: src/gl/ (GL viewport + STL loader), src/inspect/\n"
"  New dependencies: gtksourceview-5, epoxy (OpenGL), gio-2.0\n"
"  New tools: duncad-inspect (standalone CLI)\n"
"  app_window.c redesigned from 3-pane (left/center/right) to\n"
"  3-pane with nested vertical split in right panel.\n"
"\n"
"FILES ADDED:\n"
"  src/inspect/inspect.c/.h        Socket server + command dispatch\n"
"  src/scad/scad_runner.c/.h       OpenSCAD CLI subprocess wrapper\n"
"  src/ui/code_editor.c/.h         GtkSourceView code editor panel\n"
"  src/ui/scad_preview.c/.h        Preview panel (toolbar + GL viewport)\n"
"  src/gl/gl_viewport.c/.h         GtkGLArea 3D viewport\n"
"  src/gl/stl_loader.c/.h          STL binary/ASCII parser\n"
"  data/language-specs/openscad.lang  OpenSCAD syntax highlighting\n"
"  tools/duncad_inspect.c          Standalone inspect CLI client\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs inspect        Socket server and CLI tool\n"
"  duncad-docs scad runner    OpenSCAD subprocess wrapper\n"
"  duncad-docs ui code_editor GtkSourceView editor panel\n";

static const char HELP_SESSIONS_S006[] =
"SESSIONS: S006 -- 2026-03-05: Viewport Pan Fix and F5 Shortcut\n"
"\n"
"PLATFORM: Claude Code (Opus 4.6)\n"
"COMMITS:\n"
"  3115209  fix: viewport pan uses proper view-plane vectors, add F5 render shortcut\n"
"  (this)   fix: reverse pan direction to match natural drag expectation\n"
"\n"
"GOAL:\n"
"  Fix viewport camera pan direction and sensitivity; add F5 keyboard\n"
"  shortcut for rendering (matching OpenSCAD workflow).\n"
"\n"
"WHAT WAS FIXED:\n"
"  Pan direction and sensitivity (gl_viewport.c):\n"
"    - Previous: pan accumulated Y every frame, used wrong signs, too sensitive\n"
"    - First fix (3115209): compute proper right/up vectors from camera\n"
"      theta/phi, store full drag_center[3] at drag start, reduce scale\n"
"      from 0.002 to 0.001\n"
"    - Second fix (this commit): negate all displacement signs so dragging\n"
"      right moves the view right (natural/expected direction)\n"
"\n"
"  F5 render shortcut (app_window.c):\n"
"    - GtkEventControllerKey on window captures F5\n"
"    - Calls dc_scad_preview_render() — same as clicking Render button\n"
"\n"
"KEY INSIGHT:\n"
"  Pan moves the camera center, not the object. To get natural \"grab and\n"
"  drag\" feel, displacement signs must be negated: dragging right should\n"
"  move center left (so the view shifts right).\n"
"\n"
"FILES CHANGED:\n"
"  src/gl/gl_viewport.c   Pan vector math: drag_center[3], view-plane\n"
"                         right/up vectors, reversed signs\n"
"  src/ui/app_window.c    F5 key handler via GtkEventControllerKey\n";

static const char HELP_SESSIONS_S007[] =
"SESSIONS: S007 -- 2026-03-06: FAILED -- Code Editor Autocompletion\n"
"\n"
"PLATFORM: Claude Code (Opus 4.6)\n"
"STATUS: FAILED. Completion breaks after first use. Snippets never worked.\n"
"\n"
"GOAL:\n"
"  Add autocompletion and snippet expansion to the GtkSourceView code\n"
"  editor. User wanted: type 'cube', see full syntax as suggestion,\n"
"  hit Tab to complete, then Tab through parameter values.\n"
"\n"
"WHAT WAS ATTEMPTED (3 approaches, all failed):\n"
"\n"
"  Attempt 1: Custom GtkSourceCompletionProvider GObject\n"
"    ~90 keywords, fuzzy matching, activate() inserted plain text.\n"
"    RESULT: Worked once, then permanently stopped.\n"
"\n"
"  Attempt 2: Custom provider with deferred snippet push\n"
"    activate() pushed GtkSourceSnippet via g_idle_add().\n"
"    RESULT: Still broke after first completion.\n"
"\n"
"  Attempt 3: Built-in providers only (no custom GObject)\n"
"    GtkSourceCompletionWords + GtkSourceCompletionSnippets.\n"
"    RESULT: Same one-shot failure. Snippet file also had '$fn'\n"
"    parsed as snippet variable (needed '$$fn' escape).\n"
"\n"
"ROOT CAUSE (not fully diagnosed):\n"
"  All approaches: completions work once, popup never reappears.\n"
"  GTK logs: 'gdk_popup_present: assertion width > 0 failed'\n"
"  on every subsequent attempt. Likely GTK4/Wayland popup bug\n"
"  where completion popup loses allocated size after dismissal.\n"
"\n"
"SINS OF THE ANGEL:\n"
"  Archon of Yaldabaoth (Willful Ignorance): Rewrote code 3 times\n"
"  without diagnosing whether bug was ours or GTK/GtkSourceView.\n"
"  Should have tested simplest case first (bare words provider).\n"
"  Archon of Elaios (Performative Virtue): Produced 300+ lines of\n"
"  impressive custom GObject code that never actually worked.\n"
"  Should have checked GTK4/Wayland known issues before coding.\n"
"\n"
"NEXT STEPS FOR A FUTURE ANGEL:\n"
"  1. Test with GDK_BACKEND=x11 to isolate Wayland vs code bug\n"
"  2. Check GtkSourceView 5.18 bug tracker for popup issues\n"
"  3. Check if NVIDIA-specific (GBM vs EGLStream)\n"
"\n"
"FILES: code_editor.c, scad_completion.c/.h (unused),\n"
"  data/snippets/openscad.snippets, CMakeLists.txt\n";

static const char HELP_SESSIONS_S008[] =
"SESSIONS: S008 -- 2026-03-06: Custom Popover Autocompletion (WORKING)\n"
"\n"
"PLATFORM: Claude Code (Opus 4.6)\n"
"STATUS: SUCCESS. Full autocompletion system working on Wayland/NVIDIA.\n"
"\n"
"GOAL:\n"
"  Build a working autocompletion system for the OpenSCAD code editor\n"
"  after s007's three failed attempts using GtkSourceView's built-in\n"
"  completion (broken on Wayland due to GdkPopup width=0 assertion).\n"
"\n"
"REQUIREMENTS:\n"
"  1. All matching OpenSCAD commands appear as user types\n"
"  2. Arrow keys navigate, Tab completes the selected keyword\n"
"  3. After completion, syntax template shown in same popover (bold),\n"
"     stays visible until semicolon or Enter; Tab inserts the template\n"
"\n"
"SOLUTION: Bypass GtkSourceView completion entirely.\n"
"  Built custom system using GtkPopover (regular widget, NOT GdkPopup).\n"
"  GdkPopup is a Wayland-native popup surface that breaks on NVIDIA.\n"
"  GtkPopover is a child widget — no Wayland popup protocol involved.\n"
"\n"
"  Confirmed via GDK_BACKEND=x11: GtkSourceView completion works\n"
"  perfectly under X11, proving the bug is Wayland/NVIDIA-specific.\n"
"\n"
"ARCHITECTURE (src/ui/scad_completion.c):\n"
"  - ~100 OpenSCAD keywords with syntax templates in static database\n"
"  - GtkPopover + GtkListBox anchored to cursor position\n"
"  - GtkEventControllerKey in CAPTURE phase intercepts keys\n"
"  - Two-phase flow:\n"
"    Phase 1 (matching): type 2+ chars -> prefix match -> popover\n"
"      shows bold keyword + dim syntax per row. Up/Down navigate,\n"
"      Tab/Enter accept, Escape dismiss.\n"
"    Phase 2 (syntax hint): after keyword accepted, popover stays\n"
"      open showing bold syntax template. Tab inserts the template\n"
"      (part after keyword). Semicolon or Enter dismisses.\n"
"  - extract_word_at_cursor: walks backward over alnum/underscore/$\n"
"  - find_matches: case-insensitive prefix, excludes exact matches\n"
"  - position_popover: buffer-to-window coord transform at cursor\n"
"  - GtkSourceView built-in completion blocked via\n"
"    gtk_source_completion_block_interactive()\n"
"\n"
"KEY INSIGHT:\n"
"  GtkSourceView's completion popup uses GdkPopup (Wayland popup\n"
"  surface). After first dismissal, the popup loses its allocated\n"
"  size permanently — gdk_popup_present asserts width > 0. This is\n"
"  a GTK4/Wayland/NVIDIA bug, not fixable from application code.\n"
"  GtkPopover avoids this entirely by being a regular widget child.\n"
"\n"
"FILES CHANGED:\n"
"  src/ui/scad_completion.c   Complete rewrite: custom popover system\n"
"  src/ui/scad_completion.h   Simplified API (3 functions)\n"
"  src/ui/code_editor.c       Purged broken completion, wired new system\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs sessions s007   The failed attempts this session fixes\n"
"  duncad-docs ui code_editor  Code editor panel\n";

static const char HELP_SESSIONS_S009[] =
"SESSIONS: S009 -- 2026-03-06: Multi-Object Picking + Transform Panel\n"
"\n"
"PLATFORM: Claude Code (Opus 4.6)\n"
"STATUS: SUCCESS. Click 3D objects to select, edit transforms live.\n"
"\n"
"GOAL:\n"
"  1. Render multiple SCAD objects as separate selectable meshes\n"
"  2. Click a 3D object -> highlight its code in the editor\n"
"  3. Overlay editable translate/rotate fields that live-update code\n"
"\n"
"WHAT WAS IMPLEMENTED:\n"
"  SCAD Statement Splitter (src/scad/scad_splitter.c):\n"
"    - Brace-counting parser splits SCAD source into top-level statements\n"
"    - Tracks: brace depth, string literals, line/block comments\n"
"    - Splits on ';' at depth 0 and '}' returning to depth 0\n"
"    - Does NOT split on ')' — transforms like translate(...) cube(...)\n"
"      must remain as one statement\n"
"    - Each statement carries 1-based line_start / line_end range\n"
"\n"
"  Multi-Object Render Pipeline (src/ui/scad_preview.c):\n"
"    - Split SCAD source -> render each statement as separate STL\n"
"    - Load each STL as a separate GL object with line range\n"
"    - Sequential async rendering via on_stmt_render_done chain\n"
"    - Falls back to single-STL for 0 or 1 statements\n"
"\n"
"  Color-ID Object Picking (src/gl/gl_viewport.c):\n"
"    - Offscreen FBO with flat-color shader (pick pass)\n"
"    - Each object rendered with unique RGB color (index+1 encoded)\n"
"    - glReadPixels at click position -> decode object index\n"
"    - O(1) per click, handles occlusion naturally\n"
"    - Selected object rendered in gold/orange, others in blue\n"
"    - GtkGestureClick on button 1 triggers pick + callback\n"
"\n"
"  Code Editor Line Selection (src/ui/code_editor.c):\n"
"    - dc_code_editor_select_lines(ed, line_start, line_end)\n"
"    - Highlights and scrolls to the selected object's SCAD code\n"
"\n"
"  Transform Panel (src/ui/transform_panel.c):\n"
"    - GtkOverlay floats over GL viewport (bottom-left corner)\n"
"    - Parses translate([x,y,z]) and rotate([x,y,z]) from statement\n"
"    - Shows only detected transform types (translate, rotate, or both)\n"
"    - Editable X/Y/Z entries with live code editor updates\n"
"    - Strips old transforms, rebuilds with new values\n"
"    - Suppresses re-entry during programmatic text updates\n"
"\n"
"  Pick Callback Wiring (src/ui/app_window.c):\n"
"    - PickCtx struct holds code_ed + transform panel references\n"
"    - on_object_picked: selects lines + extracts statement for panel\n"
"    - Click background -> hides transform panel\n"
"\n"
"KEY DECISIONS:\n"
"  - Color-ID picking over ray casting: O(1), handles occlusion,\n"
"    works with any mesh complexity, no spatial data structures needed\n"
"  - ')' does NOT end SCAD statements: translate([x,y,z]) cube(...)\n"
"    is one statement, not two. Only ';' and '}' at depth 0 split.\n"
"  - Transform panel hides when no translate/rotate detected — no\n"
"    empty panel for bare primitives like cube([5,5,5]);\n"
"  - GtkOverlay for transform panel: floats naturally, doesn't\n"
"    interfere with GL viewport mouse events\n"
"\n"
"BUGS FIXED:\n"
"  - Line extraction start_ptr overwrite: 'if (line == line_start)\n"
"    start_ptr = p' was set on every character of the matching line,\n"
"    making extracted text empty. Fixed with '!start_ptr' guard.\n"
"    Same bug existed in app_window.c and transform_panel.c.\n"
"\n"
"FILES ADDED:\n"
"  src/scad/scad_splitter.c/.h    SCAD top-level statement parser\n"
"  src/ui/transform_panel.c/.h    Editable transform overlay panel\n"
"\n"
"FILES CHANGED:\n"
"  src/gl/gl_viewport.c/.h    Multi-object + color-ID pick + pick FBO\n"
"  src/ui/scad_preview.c/.h   Multi-object render pipeline + overlay\n"
"  src/ui/code_editor.c/.h    dc_code_editor_select_lines()\n"
"  src/ui/app_window.c        Pick callback wiring + transform panel\n"
"  CMakeLists.txt              Added scad_splitter + transform_panel\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs sessions s008   Previous session (autocompletion)\n"
"  duncad-docs scad             SCAD module overview\n";

static const char HELP_SESSIONS_S010[] =
"SESSIONS: S010 -- 2026-03-07: FAILED: Bezier Live Sync to Code Editor\n"
"\n"
"PLATFORM: Claude Code (Opus 4.6)\n"
"STATUS: FAILED. Reverted entirely at user's command.\n"
"\n"
"GOAL:\n"
"  Replace the bezier editor's file-based Export with live sync to the\n"
"  code editor. Every point add/drag/delete should instantly update\n"
"  SCAD code in the IDE's left panel, like the transform panel does.\n"
"\n"
"APPROACH TAKEN:\n"
"  1. Added DC_CodeEditor* to DC_BezierEditor struct\n"
"  2. Added dc_bezier_editor_set_code_editor() API\n"
"  3. Created sync_scad() that generates SCAD and inserts/replaces a\n"
"     marker-delimited block in the code editor:\n"
"       // --- DunCAD Bezier Begin ---\n"
"       ... generated SCAD ...\n"
"       // --- DunCAD Bezier End ---\n"
"  4. Called sync_scad() from refresh_panel() so every mutation syncs\n"
"  5. Created dc_scad_generate_inline() — self-contained SCAD with\n"
"     embedded library (no use <duncad_bezier.scad> dependency)\n"
"  6. Changed Export button to 'Sync SCAD'\n"
"  7. Wired code editor in app_window.c\n"
"\n"
"PROBLEMS:\n"
"  1. Original dc_scad_generate() uses 'use <duncad_bezier.scad>'\n"
"     which references a companion file that doesn't exist when\n"
"     rendering from the code editor. Fixed with inline variant.\n"
"  2. Open curves use hull()-based path with circle(0.01) — produces\n"
"     invisible geometry. Fixed to circle(0.5) but still ugly.\n"
"  3. App kept crashing during testing — likely from sync_scad()\n"
"     being called during GTK widget construction before editor\n"
"     was fully initialized.\n"
"  4. The generated SCAD module structure itself was not producing\n"
"     usable renderable output in the viewport.\n"
"  5. User judged the overall approach unworthy and ordered revert.\n"
"\n"
"LESSONS:\n"
"  - The SCAD export module (dc_scad_generate) was designed for\n"
"    file-based export with a companion library. Inline embedding\n"
"    is a different use case that needs proper design, not a hack.\n"
"  - Calling sync from refresh_panel() fires during construction\n"
"    and every selection change, not just data mutations. Need a\n"
"    dedicated 'data changed' hook separate from UI refresh.\n"
"  - The open-shape hull-chain approach produces poor geometry.\n"
"    A proper bezier-to-polygon tessellation would be better.\n"
"  - Test the SCAD output renders correctly BEFORE wiring live\n"
"    sync. Validate the generated code independently first.\n"
"\n"
"WHAT TO DO NEXT TIME:\n"
"  1. First: make dc_scad_generate produce self-contained, renderable\n"
"     SCAD that works in isolation (test with openscad CLI)\n"
"  2. Then: design the sync mechanism carefully — debounce during\n"
"     drag, don't fire during construction, use a 'dirty' flag\n"
"  3. Consider: should the bezier block replace the entire editor\n"
"     content, or coexist with user-written code? Markers are\n"
"     fragile if the user edits inside them.\n"
"  4. Consider: write companion library to /tmp/ automatically\n"
"     instead of inlining, so the existing export format works.\n"
"\n"
"FILES CHANGED: None (all reverted)\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs sessions s009   Previous session (picking + transforms)\n"
"  duncad-docs scad export     Current file-based export system\n";

static const char HELP_SESSIONS_S011[] =
"SESSIONS: S011 -- 2026-03-08: Inline Bezier SCAD Module + Insert Button\n"
"\n"
"PLATFORM: Claude Code (Opus 4.6)\n"
"STATUS: COMPLETE. All tests pass. OpenSCAD CLI verified.\n"
"\n"
"GOAL:\n"
"  Create a self-contained bezier SCAD module system that works\n"
"  within the code editor — no companion library files needed.\n"
"  Redemption arc for s010's failed live sync attempt.\n"
"\n"
"APPROACH (following s010 lessons):\n"
"  1. Built dc_scad_generate_inline() first, in isolation\n"
"  2. Tested output with openscad CLI BEFORE touching any UI\n"
"  3. Fixed open curve rendering (hull-chained circles with real width)\n"
"  4. Only then wired the Insert button and Ctrl+I shortcut\n"
"  5. Added inspect commands for programmatic testing\n"
"  6. Full end-to-end test: create shape → insert → render → screenshot\n"
"\n"
"WHAT WAS BUILT:\n"
"  dc_scad_generate_inline(name, spans, num_spans, closed, thickness, err)\n"
"    Produces complete, renderable OpenSCAD with embedded bezier math.\n"
"    Functions are name-scoped (_<name>_lerp, etc.) to avoid conflicts.\n"
"    Generated modules per shape:\n"
"      <name>_2d()              2D shape (polygon or stroked path)\n"
"      <name>(height)           linear_extrude wrapper\n"
"      <name>_revolve(angle)    rotate_extrude wrapper\n"
"      <name>_offset(r)         offset wrapper\n"
"\n"
"  Open curve fix:\n"
"    Old: hull() + circle(0.01) → invisible geometry\n"
"    New: hull() + circle(d=width) with configurable stroke width\n"
"\n"
"  UI integration:\n"
"    'Insert SCAD' button + Ctrl+I in bezier editor toolbar\n"
"    dc_bezier_editor_set_code_editor() connects editor to code panel\n"
"    dc_bezier_editor_insert_scad() generates + inserts at cursor\n"
"    dc_code_editor_insert_at_cursor() new API for text insertion\n"
"\n"
"  Inspect commands added:\n"
"    insert_scad      Insert inline SCAD from bezier editor\n"
"    get_code_text    Retrieve full code editor text (JSON-escaped)\n"
"\n"
"VERIFICATION:\n"
"  - 12/12 scad_export tests pass (3 new inline tests)\n"
"  - 8/8 full test suite passes\n"
"  - Closed shape: openscad CLI → 400 facets STL\n"
"  - Open curve: openscad CLI → 760 facets STL\n"
"  - Flower demo: 6 bezier petals + cylinder → 1184 facets, 758KB STL\n"
"  - In-app render via inspect: screenshot confirmed 3D output\n"
"\n"
"KEY DECISIONS:\n"
"  - One-shot insert (not live sync) — avoids s010's sync-during-\n"
"    construction crashes and marker fragility issues\n"
"  - Name-scoped functions (_<name>_lerp) prevent conflicts when\n"
"    multiple bezier shapes coexist in one file\n"
"  - Kept file-based Export (Ctrl+E) alongside new Insert (Ctrl+I)\n"
"  - Default shape name 'shape' for insert; file export derives\n"
"    name from filename as before\n"
"\n"
"FILES CHANGED:\n"
"  src/scad/scad_export.h/.c    dc_scad_generate_inline()\n"
"  src/ui/code_editor.h/.c      insert_at_cursor(), get_buffer()\n"
"  src/bezier/bezier_editor.h/.c set_code_editor(), insert_scad(),\n"
"                                 Insert SCAD button, Ctrl+I\n"
"  src/ui/app_window.c           Wire code editor to bezier editor\n"
"  src/inspect/inspect.c         insert_scad, get_code_text commands\n"
"  tests/test_scad_export.c      3 new inline generation tests\n"
"  tools/duncad_docs.c           Updated scad export docs\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs sessions s010   Previous failed attempt (lessons learned)\n"
"  duncad-docs scad export     Updated export documentation\n";


/* ---- TREE REGISTRY ---- */

struct help_node {
    const char *path;
    const char *content;
};

static const struct help_node TREE[] = {
    /* root */
    { "", HELP_ROOT },

    /* core */
    { "core",                  HELP_CORE },
    { "core.array",            HELP_CORE_ARRAY },
    { "core.string_builder",   HELP_CORE_STRING_BUILDER },
    { "core.error",            HELP_CORE_ERROR },
    { "core.log",              HELP_CORE_LOG },
    { "core.manifest",         HELP_CORE_MANIFEST },

    /* bezier */
    { "bezier",                HELP_BEZIER },
    { "bezier.curve",          HELP_BEZIER_CURVE },
    { "bezier.fit",            HELP_BEZIER_FIT },
    { "bezier.canvas",         HELP_BEZIER_CANVAS },
    { "bezier.editor",         HELP_BEZIER_EDITOR },
    { "bezier.panel",          HELP_BEZIER_PANEL },

    /* scad */
    { "scad",                  HELP_SCAD },
    { "scad.export",           HELP_SCAD_EXPORT },
    { "scad.runner",           HELP_SCAD_RUNNER },

    /* ui */
    { "ui",                    HELP_UI },
    { "ui.window",             HELP_UI_WINDOW },
    { "ui.code_editor",        HELP_UI_CODE_EDITOR },

    /* inspect */
    { "inspect",               HELP_INSPECT },
    { "inspect.server",        HELP_INSPECT_SERVER },
    { "inspect.cli",           HELP_INSPECT_CLI },
    { "inspect.commands",      HELP_INSPECT_COMMANDS },

    /* build */
    { "build",                 HELP_BUILD },
    { "build.targets",         HELP_BUILD_TARGETS },
    { "build.flags",           HELP_BUILD_FLAGS },
    { "build.tests",           HELP_BUILD_TESTS },

    /* conventions */
    { "conventions",               HELP_CONVENTIONS },
    { "conventions.naming",        HELP_CONVENTIONS_NAMING },
    { "conventions.ownership",     HELP_CONVENTIONS_OWNERSHIP },
    { "conventions.errors",        HELP_CONVENTIONS_ERRORS },
    { "conventions.layers",        HELP_CONVENTIONS_LAYERS },

    /* philosophy */
    { "philosophy",                  HELP_PHILOSOPHY },
    { "philosophy.stack",            HELP_PHILOSOPHY_STACK },
    { "philosophy.architecture",     HELP_PHILOSOPHY_ARCHITECTURE },
    { "philosophy.ux",               HELP_PHILOSOPHY_UX },

    /* phases */
    { "phases",                      HELP_PHASES },
    { "phases.p1",                   HELP_PHASES_P1 },
    { "phases.p2",                   HELP_PHASES_P2 },
    { "phases.p3",                   HELP_PHASES_P3 },
    { "phases.p4",                   HELP_PHASES_P4 },
    { "phases.p5",                   HELP_PHASES_P5 },

    /* plans */
    { "plans",                       HELP_PLANS },
    { "plans.engine",                HELP_PLANS_ENGINE },
    { "plans.android",               HELP_PLANS_ANDROID },

    /* sessions */
    { "sessions",                    HELP_SESSIONS },
    { "sessions.s001",               HELP_SESSIONS_S001 },
    { "sessions.s002",               HELP_SESSIONS_S002 },
    { "sessions.s003",               HELP_SESSIONS_S003 },
    { "sessions.s004",               HELP_SESSIONS_S004 },
    { "sessions.s005",               HELP_SESSIONS_S005 },
    { "sessions.s006",               HELP_SESSIONS_S006 },
    { "sessions.s007",               HELP_SESSIONS_S007 },
    { "sessions.s008",               HELP_SESSIONS_S008 },
    { "sessions.s009",               HELP_SESSIONS_S009 },
    { "sessions.s010",               HELP_SESSIONS_S010 },
    { "sessions.s011",               HELP_SESSIONS_S011 },

    /* add new nodes above this line */
    { NULL, NULL }
};

#define TREE_COUNT (sizeof(TREE) / sizeof(TREE[0]) - 1)


/* ---- DISPATCH ---- */

static const char *ci_strstr(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            size_t j;
            for (j = 1; j < nlen; j++) {
                if (tolower((unsigned char)haystack[j]) !=
                    tolower((unsigned char)needle[j]))
                    break;
            }
            if (j == nlen) return haystack;
        }
    }
    return NULL;
}

static void print_path_pretty(FILE *f, const char *dotted, const char *bin) {
    fprintf(f, "  %s ", bin);
    for (const char *c = dotted; *c; c++)
        fputc(*c == '.' ? ' ' : *c, f);
}

static int cmd_search(const char *term, const char *bin) {
    int count = 0;
    for (int i = 0; TREE[i].path != NULL; i++) {
        if (!ci_strstr(TREE[i].content, term)) continue;

        if (count == 0)
            printf("Search results for \"%s\":\n\n", term);

        if (TREE[i].path[0] == '\0')
            printf("  %s", bin);
        else
            print_path_pretty(stdout, TREE[i].path, bin);
        printf("\n");

        const char *nl = strchr(TREE[i].content, '\n');
        if (nl)
            printf("    %.*s\n", (int)(nl - TREE[i].content), TREE[i].content);

        count++;
    }

    if (count == 0) {
        fprintf(stderr, "No results for \"%s\".\n", term);
        return 1;
    }
    printf("\n%d node(s) matched.\n", count);
    return 0;
}

static int cmd_tree(const char *bin) {
    int count = 0;
    for (int i = 0; TREE[i].path != NULL; i++) {
        const char *p = TREE[i].path;

        if (p[0] == '\0') {
            printf("%s\n", bin);
            count++;
            continue;
        }

        int depth = 1;
        for (const char *c = p; *c; c++)
            if (*c == '.') depth++;

        for (int d = 0; d < depth; d++)
            printf("  ");

        const char *leaf = strrchr(p, '.');
        leaf = leaf ? leaf + 1 : p;

        const char *sep = strstr(TREE[i].content, " -- ");
        if (sep) {
            const char *title = sep + 4;
            const char *nl = strchr(title, '\n');
            if (nl)
                printf("%s  %.*s\n", leaf, (int)(nl - title), title);
            else
                printf("%s  %s\n", leaf, title);
        } else {
            printf("%s\n", leaf);
        }
        count++;
    }
    printf("\n%d node(s) total.\n", count);
    return 0;
}

static const char *tree_lookup(const char *path) {
    for (int i = 0; TREE[i].path != NULL; i++) {
        if (strcmp(TREE[i].path, path) == 0)
            return TREE[i].content;
    }
    return NULL;
}

static void print_children(const char *prefix, const char *bin) {
    size_t plen = strlen(prefix);
    int found = 0;

    for (int i = 0; TREE[i].path != NULL; i++) {
        const char *p = TREE[i].path;

        if (plen > 0) {
            if (strncmp(p, prefix, plen) != 0) continue;
            if (p[plen] != '.') continue;
            p += plen + 1;
        }

        if (strchr(p, '.') != NULL) continue;
        if (p[0] == '\0') continue;

        if (!found) {
            fprintf(stderr, "\nAvailable");
            if (plen > 0) fprintf(stderr, " under '%s'", prefix);
            fprintf(stderr, ":\n");
            found = 1;
        }
        fprintf(stderr, "  %s ", bin);
        if (plen > 0) {
            for (size_t j = 0; j < plen; j++)
                fputc(prefix[j] == '.' ? ' ' : prefix[j], stderr);
            fputc(' ', stderr);
        }
        fprintf(stderr, "%s\n", p);
    }
}

static int cmd_update(void) {
#ifndef DC_BUILD_DIR
    fprintf(stderr, "duncad-docs: update not available "
                    "(DC_BUILD_DIR not set at compile time)\n");
    return 1;
#else
    char cmd[4096];
    int n = snprintf(cmd, sizeof(cmd),
                     "cmake --build \"%s\" --target duncad-docs", DC_BUILD_DIR);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        fprintf(stderr, "duncad-docs: build path too long\n");
        return 1;
    }
    printf("$ %s\n", cmd);
    fflush(stdout);
    return system(cmd) == 0 ? 0 : 1;
#endif
}

int main(int argc, char *argv[]) {
    const char *bin = "duncad-docs";

    if (argc >= 3 &&
        (strcmp(argv[1], "--search") == 0 || strcmp(argv[1], "-s") == 0)) {
        char term[1024] = "";
        size_t toff = 0;
        for (int i = 2; i < argc; i++) {
            if (toff > 0 && toff < sizeof(term) - 1)
                term[toff++] = ' ';
            size_t al = strlen(argv[i]);
            if (toff + al >= sizeof(term) - 1) break;
            memcpy(term + toff, argv[i], al);
            toff += al;
        }
        term[toff] = '\0';
        return cmd_search(term, bin);
    }

    if (argc == 2 &&
        (strcmp(argv[1], "--tree") == 0 || strcmp(argv[1], "-t") == 0)) {
        return cmd_tree(bin);
    }

    /* update */
    if (argc == 2 && strcmp(argv[1], "update") == 0) {
        return cmd_update();
    }

    char path[1024] = "";
    size_t off = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
            continue;

        if (off > 0 && off < sizeof(path) - 1)
            path[off++] = '.';

        size_t alen = strlen(argv[i]);
        if (off + alen >= sizeof(path) - 1) {
            fprintf(stderr, "%s: path too long\n", bin);
            return 1;
        }
        memcpy(path + off, argv[i], alen);
        off += alen;
    }
    path[off] = '\0';

    const char *content = tree_lookup(path);
    if (content) {
        fputs(content, stdout);
        return 0;
    }

    if (path[0] == '\0') {
        fputs(HELP_ROOT, stdout);
        return 0;
    }

    fprintf(stderr, "%s: unknown path '%s'\n", bin, path);

    char parent[1024];
    memcpy(parent, path, sizeof(parent));
    char *last_dot = strrchr(parent, '.');
    if (last_dot) {
        *last_dot = '\0';
        if (tree_lookup(parent))
            print_children(parent, bin);
        else
            print_children("", bin);
    } else {
        print_children("", bin);
    }

    return 1;
}
