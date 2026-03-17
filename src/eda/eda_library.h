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

#endif /* DC_EDA_LIBRARY_H */
