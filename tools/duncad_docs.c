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
"CATEGORIES:\n"
"  duncad-docs core          Foundation utilities (no external deps)\n"
"  duncad-docs bezier        Bezier spline geometry and GTK4 editor\n"
"  duncad-docs scad          OpenSCAD code generation\n"
"  duncad-docs ui            GTK4 application window\n"
"  duncad-docs build         Build system and test suite\n"
"  duncad-docs conventions   Naming, ownership, and error handling\n"
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
"UNDO/REDO:\n"
"  Each edit pushes a snapshot of the full DC_BezierCurve.\n"
"  dc_bezier_editor_undo() / dc_bezier_editor_redo() restore them.\n"
"\n"
"API (src/bezier/bezier_editor.h):\n"
"  DC_BezierEditor *dc_bezier_editor_new(void)\n"
"  GtkWidget       *dc_bezier_editor_widget(editor)\n"
"  void  dc_bezier_editor_set_mode(editor, DC_EditorMode)\n"
"  void  dc_bezier_editor_undo(editor)\n"
"  void  dc_bezier_editor_redo(editor)\n"
"  void  dc_bezier_editor_set_selection_cb(editor, cb, userdata)\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier canvas   Drawing area owned by editor\n"
"  duncad-docs bezier panel    Sidebar that reads editor selection\n"
"  duncad-docs bezier fit      Fitting triggered by freehand mode\n";

static const char HELP_BEZIER_PANEL[] =
"BEZIER: PANEL -- Numeric Sidebar\n"
"\n"
"GTK4 sidebar displaying and editing the selected knot's coordinates,\n"
"handle positions, continuity, and overall spline statistics.\n"
"\n"
"DISPLAYS:\n"
"  Selected knot    X, Y position (editable)\n"
"  Handle prev      X, Y offset (editable)\n"
"  Handle next      X, Y offset (editable)\n"
"  Continuity       SMOOTH / SYMMETRIC / CORNER (dropdown)\n"
"  Spline stats     Knot count, bounding box (min/max X/Y)\n"
"\n"
"API (src/bezier/bezier_panel.h):\n"
"  DC_BezierPanel  *dc_bezier_panel_new(void)\n"
"  GtkWidget       *dc_bezier_panel_widget(panel)\n"
"  void  dc_bezier_panel_set_editor(panel, DC_BezierEditor *)\n"
"  void  dc_bezier_panel_refresh(panel)   re-read from editor\n"
"\n"
"SEE ALSO:\n"
"  duncad-docs bezier editor   Editor whose selection drives the panel\n";

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
