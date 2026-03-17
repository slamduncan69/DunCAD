#ifndef DC_EDA_LIBRARY_H
#define DC_EDA_LIBRARY_H

/*
 * eda_library.h — KiCad symbol and footprint library loader.
 *
 * Loads .kicad_sym (schematic symbol) and .kicad_mod (footprint) files,
 * providing lookup by lib_id string. The library caches parsed ASTs and
 * returns borrowed pointers into the tree.
 *
 * Ownership: DC_ELibrary owns all loaded data. dc_elibrary_free() releases
 * everything. Returned DC_Sexpr pointers are borrowed and must not be freed.
 */

#include "core/array.h"
#include "core/error.h"
#include "eda/sexpr.h"
#include <stddef.h>

/* -------------------------------------------------------------------------
 * DC_ELibrary — opaque library container
 * ---------------------------------------------------------------------- */
typedef struct DC_ELibrary DC_ELibrary;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_ELibrary *dc_elibrary_new(void);
void dc_elibrary_free(DC_ELibrary *lib);

/* =========================================================================
 * Loading — add library files to the collection
 * ========================================================================= */

/* Load a .kicad_sym symbol library file.
 * Symbols are indexed by their names within the file. */
int dc_elibrary_load_symbols(DC_ELibrary *lib, const char *path, DC_Error *err);

/* Register a .kicad_sym library file path without parsing.
 * The library name is derived from the filename. Symbols are loaded
 * on demand when accessed via per-library enumeration or lookup.
 * This is O(1) per call — suitable for registering hundreds of libs. */
int dc_elibrary_register_symbols(DC_ELibrary *lib, const char *path);

/* Load a .kicad_mod footprint file (single footprint).
 * The footprint is indexed by its name. */
int dc_elibrary_load_footprint(DC_ELibrary *lib, const char *path, DC_Error *err);

/* =========================================================================
 * Lookup
 * ========================================================================= */

/* Look up a symbol by lib_id (e.g. "Device:R_Small").
 * The lib_id format is "library_name:symbol_name".
 * Returns a borrowed DC_Sexpr subtree, or NULL if not found. */
const DC_Sexpr *dc_elibrary_find_symbol(const DC_ELibrary *lib,
                                          const char *lib_id);

/* Look up a symbol by name only (without library prefix).
 * Searches all loaded libraries. Returns first match. */
const DC_Sexpr *dc_elibrary_find_symbol_by_name(const DC_ELibrary *lib,
                                                   const char *name);

/* Look up a footprint by lib_id.
 * Returns a borrowed DC_Sexpr subtree, or NULL if not found. */
const DC_Sexpr *dc_elibrary_find_footprint(const DC_ELibrary *lib,
                                              const char *lib_id);

/* =========================================================================
 * Enumeration
 * ========================================================================= */

/* Get the number of loaded symbols across all libraries. */
size_t dc_elibrary_symbol_count(const DC_ELibrary *lib);

/* Get the name of the Nth loaded symbol. Borrowed pointer. */
const char *dc_elibrary_symbol_name(const DC_ELibrary *lib, size_t index);

/* =========================================================================
 * Per-library enumeration
 * ========================================================================= */

/* Get the number of distinct loaded library names. */
size_t dc_elibrary_lib_count(const DC_ELibrary *lib);

/* Get the Nth library name. Borrowed pointer. */
const char *dc_elibrary_lib_name(const DC_ELibrary *lib, size_t index);

/* Get the number of symbols in a specific library. */
size_t dc_elibrary_lib_symbol_count(const DC_ELibrary *lib, const char *lib_name);

/* Get the name of the Nth symbol in a specific library. Borrowed pointer. */
const char *dc_elibrary_lib_symbol_name(const DC_ELibrary *lib,
                                          const char *lib_name, size_t index);

/* =========================================================================
 * Footprint enumeration + batch loading
 * ========================================================================= */

/* Get the number of loaded footprints across all libraries. */
size_t dc_elibrary_footprint_count(const DC_ELibrary *lib);

/* Get the name of the Nth loaded footprint. Borrowed pointer. */
const char *dc_elibrary_footprint_name(const DC_ELibrary *lib, size_t index);

/* Get the library name of the Nth loaded footprint. Borrowed pointer. */
const char *dc_elibrary_footprint_lib_name(const DC_ELibrary *lib, size_t index);

/* Scan a .pretty directory and load all .kicad_mod files within.
 * The library name is derived from the directory name (e.g. "Resistor_SMD"). */
int dc_elibrary_load_footprint_dir(DC_ELibrary *lib, const char *dir_path,
                                     DC_Error *err);

/* Register a .pretty footprint directory without loading.
 * Footprints are loaded on demand when accessed. O(1) per call. */
int dc_elibrary_register_footprint_dir(DC_ELibrary *lib, const char *dir_path);

/* Get the number of registered footprint library directories
 * (loaded + pending). */
size_t dc_elibrary_fp_lib_count(const DC_ELibrary *lib);

/* Get the Nth footprint library name. Borrowed pointer. */
const char *dc_elibrary_fp_lib_name(const DC_ELibrary *lib, size_t index);

/* =========================================================================
 * Symbol property / pin inspection
 * ========================================================================= */

/* Extract a property value from a symbol definition sexpr node.
 * Searches for (property "key" "value" ...) children.
 * Returns borrowed pointer or NULL. */
const char *dc_elibrary_symbol_property(const DC_Sexpr *sym_node,
                                          const char *key);

/* Count all pins across all sub-units of a symbol definition. */
size_t dc_elibrary_symbol_pin_count(const DC_Sexpr *sym_node);

/* Get the library name of the Nth loaded symbol. Borrowed pointer. */
const char *dc_elibrary_symbol_lib_name(const DC_ELibrary *lib, size_t index);

#endif /* DC_EDA_LIBRARY_H */
