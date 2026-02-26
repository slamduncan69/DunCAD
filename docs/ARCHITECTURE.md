# DunCAD — Architecture

## Layer Structure

```
┌─────────────────────────────────────────────────────────────────┐
│  Application Entry Point (src/main.c)                           │
│  • Minimal: init logger, create GtkApplication, run             │
└─────────────────────────────┬───────────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────────┐
│  UI Layer (src/ui/)                                             │
│  • GTK4 widgets and window management                           │
│  • app_window.c — root window, header bar, paned layout,        │
│                   status bar                                     │
│  • Depends on: Core layer, GTK4                                 │
│  • Future: bezier editor canvas, SCAD code editor,              │
│            3D viewport (GtkGLArea), KiCad window proxy          │
└─────────────────────────────┬───────────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────────┐
│  Domain Modules (src/bezier/, src/scad/, src/assembly/,         │
│                  src/kicad/)                                     │
│  • Each module is self-contained with no cross-dependencies     │
│  • No GTK dependency: domain logic only                         │
│  • Future phases only — currently empty stubs                   │
└─────────────────────────────┬───────────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────────┐
│  Core Layer (src/core/)                                         │
│  • Pure C utility modules with no external dependencies         │
│    beyond the C standard library                                │
│  • array.h/c      — type-safe dynamic array                     │
│  • string_builder.h/c — dynamic string construction             │
│  • error.h/c      — uniform error type and propagation macros   │
│  • log.h/c        — structured dual-output logger (singleton)   │
│  • manifest.h/c   — project manifest and LLM context capture    │
└─────────────────────────────────────────────────────────────────┘
```

The dependency rule is strict: **higher layers may depend on lower layers;
lower layers must never depend on higher layers.**  In particular, `src/core/`
has no knowledge of GTK, Cairo, or any domain module.

---

## Naming Conventions

### Prefixes

| Prefix     | Scope                                   |
|------------|-----------------------------------------|
| `dc_`      | All public identifiers (functions, types, enums) |
| `DC_`      | Macros and enum values                  |
| `g_log`    | The single permitted global (the logger singleton) |

### Types

- Structs are `typedef`-named: `DC_Array`, `DC_Manifest`, `DC_Error`.
- Enums are `typedef`-named: `DC_ErrorCode`, `DC_LogLevel`.
- Opaque types declare the struct in the `.c` file and expose only a typedef
  in the `.h` file (see `DC_Array`, `DC_StringBuilder`).

### Files

- Header files: `snake_case.h`, one public module per header.
- Source files: `snake_case.c`, same base name as the header.
- Test files: `test_<module_name>.c` in `tests/`.

### Functions

```
dc_<module>_<verb>[_<noun>]

Examples:
  dc_array_push
  dc_sb_appendf
  dc_manifest_capture_context
  dc_log_set_level
```

---

## Ownership Model

DunCAD uses a **single-owner model** with explicit transfer semantics.
There is no reference counting or garbage collection.

### Rules

1. **Every heap allocation has exactly one owner at any point in time.**
2. **Ownership is documented in every public function's header comment.**
3. **Three ownership patterns are used:**

   - **Owned** — the holder must `free()` or call the module's destructor.
   - **Borrowed** — the holder must not free; the pointer is valid only
     while the owning container is alive and unmodified.
   - **Transferred** — ownership moves from caller to callee (or vice versa)
     at the call site.

### Key examples

| Function | Caller's responsibility |
|---|---|
| `dc_array_new()` | Call `dc_array_free()` when done |
| `dc_array_get()` | **Do not free**; borrowed pointer invalidated by mutations |
| `dc_sb_take()` | Call `free()` on returned string; builder is reset |
| `dc_manifest_add_artifact()` | If `artifact->depends_on != NULL`, the manifest takes ownership |
| `dc_manifest_capture_context()` | Call `free()` on returned string |
| `dc_manifest_load()` | Call `dc_manifest_free()` on returned manifest |

### The logger singleton

`g_log` in `log.c` is the one permitted global mutable variable.  It is
initialised by `dc_log_init()` and torn down by `dc_log_shutdown()`.  These
must be the first and last calls in `main()`.  All other modules must be
stateless singletons or operate on explicit struct instances.

---

## Error Handling

All fallible functions that cannot return a value accept an `DC_Error *err`
out-parameter (may be NULL for callers that don't need detail).

```c
// Pattern A: functions that return int (0=ok, -1=err)
int dc_manifest_save(DC_Manifest *m, const char *path, DC_Error *err);

// Pattern B: functions that return a pointer (NULL=err)
DC_Manifest *dc_manifest_load(const char *path, DC_Error *err);
```

Use `DC_SET_ERROR(err, code, fmt, ...)` to record source location
automatically.  Use `DC_CHECK(err, call)` to propagate errors up the call
stack when the enclosing function returns `DC_ErrorCode`.

---

## Adding a New Component

Follow these steps when adding a new module (e.g., `src/bezier/`):

1. **Create `src/<module>/<name>.h` and `src/<module>/<name>.c`.**
   - All public symbols prefixed `dc_`.
   - Document ownership and error semantics in every function comment.
   - No GTK or domain-layer includes in `src/core/` modules.

2. **Add sources to `CMakeLists.txt`.**
   - Pure logic with no GTK dependency → add to `dc_core` static library.
   - UI code → add to the `duncad` executable target.

3. **Add `tests/test_<name>.c`** and register it with the `dc_add_test`
   macro in `CMakeLists.txt`.

4. **Update `docs/ARCHITECTURE.md`** (this file) to describe the new
   module's layer placement and any new ownership rules.

5. **Run `cmake --build build --target tests`** and verify zero ASan errors
   before committing.

---

## Future Deployment Note

The Core layer is intentionally free of all UI and platform dependencies.
`dc_manifest_capture_context()` in particular has no GTK dependency, which
allows it to be called from:

- A `--dump-context` CLI flag for automated LLM workflows.
- An HTTP endpoint (Phase 5+).
- A Unix domain socket for IPC with external tools.
- A future Android HAL integration on Raspberry Pi.

Preserve this property: never add UI includes to `src/core/`.

---

## Build Targets

| Target | Description |
|---|---|
| `duncad` | Main GTK4 application binary |
| `dc_core` | Static library of all core modules (no GTK) |
| `test_array` | Array unit tests |
| `test_string_builder` | StringBuilder unit tests |
| `test_manifest` | Manifest unit tests |
| `tests` | Build + run all tests via CTest |

Build in debug mode with AddressSanitizer (default):
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cmake --build build --target tests
```

Build in release mode without ASan:
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DDC_ASAN=OFF
cmake --build build
```
