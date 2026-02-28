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
"src/scad/ converts DC_BezierCurve splines into OpenSCAD (.scad)\n"
"source files. The generated code uses a companion library that\n"
"implements De Casteljau evaluation inside OpenSCAD.\n"
"\n"
"TOPICS:\n"
"  duncad-docs scad export   dc_scad_export_spline() and output format\n";

static const char HELP_SCAD_EXPORT[] =
"SCAD: EXPORT -- OpenSCAD Export\n"
"\n"
"Generates two files per export: a per-spline .scad file and a\n"
"companion library (duncad_bezier.scad).\n"
"\n"
"API (src/scad/scad_export.h):\n"
"  bool dc_scad_export_spline(DC_BezierCurve *, path, DC_Error *)\n"
"\n"
"GENERATED: <name>.scad\n"
"  include <duncad_bezier.scad>\n"
"  Encodes each segment as a 4-element control point list.\n"
"  Calls dc_bezier_path(segments, steps) to produce the outline.\n"
"\n"
"GENERATED: duncad_bezier.scad (companion library)\n"
"  dc_bezier_point(p0,p1,p2,p3,t)  De Casteljau evaluation at t\n"
"  dc_bezier_path(segs, steps)      tessellated polygon path\n"
"  dc_bezier_shape(segs, steps, h)  linear_extrude wrapper\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier curve   Source spline consumed by the exporter\n";

static const char HELP_UI[] =
"UI -- GTK4 Application Window\n"
"\n"
"src/ui/ contains the top-level GTK4 window and shell. Depends on\n"
"the core layer and GTK4. All GTK usage is confined to this layer\n"
"and the bezier UI components in src/bezier/.\n"
"\n"
"TOPICS:\n"
"  duncad-docs ui window   DC_AppWindow main window\n";

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
"  Right panel   300px -- properties/inspector\n"
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
"  duncad-docs bezier editor   The widget that fills the center panel\n";

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

    /* ui */
    { "ui",                    HELP_UI },
    { "ui.window",             HELP_UI_WINDOW },

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
