#define _POSIX_C_SOURCE 200809L
/*
 * eda_library.c — KiCad symbol and footprint library loader.
 */

#include "eda/eda_library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal structure
 * ========================================================================= */

/* A single symbol entry: name → borrowed subtree pointer */
typedef struct {
    char           *name;       /* symbol name (e.g. "R_Small") — owned */
    char           *lib_name;   /* library name (e.g. "Device") — owned */
    const DC_Sexpr *node;       /* borrowed pointer into AST */
} SymEntry;

/* A loaded library file: keeps the AST alive */
typedef struct {
    char     *path;     /* file path — owned */
    char     *lib_name; /* derived library name — owned */
    DC_Sexpr *ast;      /* parsed AST — owned */
} LibFile;

struct DC_ELibrary {
    DC_Array *symbols;    /* SymEntry */
    DC_Array *footprints; /* SymEntry (reuse same struct) */
    DC_Array *files;      /* LibFile */
};

/* ---- Cleanup ---- */

static void
sym_entry_cleanup(SymEntry *e)
{
    free(e->name);
    free(e->lib_name);
}

static void
lib_file_cleanup(LibFile *lf)
{
    free(lf->path);
    free(lf->lib_name);
    dc_sexpr_free(lf->ast);
}

/* ---- Extract library name from file path ---- */
static char *
lib_name_from_path(const char *path)
{
    const char *base = path;
    const char *p = path;
    while (*p) {
        if (*p == '/' || *p == '\\') base = p + 1;
        p++;
    }
    /* Remove extension */
    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    char *name = malloc(len + 1);
    if (name) {
        memcpy(name, base, len);
        name[len] = '\0';
    }
    return name;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_ELibrary *
dc_elibrary_new(void)
{
    DC_ELibrary *lib = calloc(1, sizeof(DC_ELibrary));
    if (!lib) return NULL;

    lib->symbols    = dc_array_new(sizeof(SymEntry));
    lib->footprints = dc_array_new(sizeof(SymEntry));
    lib->files      = dc_array_new(sizeof(LibFile));

    if (!lib->symbols || !lib->footprints || !lib->files) {
        dc_elibrary_free(lib);
        return NULL;
    }
    return lib;
}

void
dc_elibrary_free(DC_ELibrary *lib)
{
    if (!lib) return;
    if (lib->symbols) {
        for (size_t i = 0; i < dc_array_length(lib->symbols); i++)
            sym_entry_cleanup(dc_array_get(lib->symbols, i));
        dc_array_free(lib->symbols);
    }
    if (lib->footprints) {
        for (size_t i = 0; i < dc_array_length(lib->footprints); i++)
            sym_entry_cleanup(dc_array_get(lib->footprints, i));
        dc_array_free(lib->footprints);
    }
    if (lib->files) {
        for (size_t i = 0; i < dc_array_length(lib->files); i++)
            lib_file_cleanup(dc_array_get(lib->files, i));
        dc_array_free(lib->files);
    }
    free(lib);
}

/* =========================================================================
 * Loading
 * ========================================================================= */

int
dc_elibrary_load_symbols(DC_ELibrary *lib, const char *path, DC_Error *err)
{
    if (!lib || !path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL arg");
        return -1;
    }

    /* Read file */
    FILE *f = fopen(path, "r");
    if (!f) {
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "cannot open %s", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *text = malloc((size_t)size + 1);
    if (!text) { fclose(f); return -1; }

    size_t read = fread(text, 1, (size_t)size, f);
    text[read] = '\0';
    fclose(f);

    /* Parse */
    DC_Sexpr *ast = dc_sexpr_parse(text, err);
    free(text);
    if (!ast) return -1;

    const char *tag = dc_sexpr_tag(ast);
    if (!tag || strcmp(tag, "kicad_symbol_lib") != 0) {
        if (err) DC_SET_ERROR(err, DC_ERROR_PARSE, "expected (kicad_symbol_lib ...)");
        dc_sexpr_free(ast);
        return -1;
    }

    char *lname = lib_name_from_path(path);

    /* Index all (symbol "name" ...) children */
    size_t sym_count = 0;
    DC_Sexpr **syms = dc_sexpr_find_all(ast, "symbol", &sym_count);
    if (syms) {
        for (size_t i = 0; i < sym_count; i++) {
            const char *sname = dc_sexpr_value(syms[i]);
            if (!sname) continue;

            /* Skip sub-symbols (e.g. "R_Small_0_1") — they're children of the main symbol */
            /* Main symbols don't contain '_' followed by a digit at the end... actually
             * KiCad nests sub-units inside the parent symbol node. Top-level symbols
             * in the lib are the ones we want. */
            SymEntry entry = {
                .name = strdup(sname),
                .lib_name = strdup(lname),
                .node = syms[i],
            };
            dc_array_push(lib->symbols, &entry);
        }
        free(syms);
    }

    /* Store the AST to keep it alive */
    LibFile lf = {
        .path = strdup(path),
        .lib_name = lname,
        .ast = ast,
    };
    dc_array_push(lib->files, &lf);

    return 0;
}

int
dc_elibrary_load_footprint(DC_ELibrary *lib, const char *path, DC_Error *err)
{
    if (!lib || !path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL arg");
        return -1;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "cannot open %s", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *text = malloc((size_t)size + 1);
    if (!text) { fclose(f); return -1; }

    size_t read = fread(text, 1, (size_t)size, f);
    text[read] = '\0';
    fclose(f);

    DC_Sexpr *ast = dc_sexpr_parse(text, err);
    free(text);
    if (!ast) return -1;

    const char *tag = dc_sexpr_tag(ast);
    if (!tag || strcmp(tag, "footprint") != 0) {
        if (err) DC_SET_ERROR(err, DC_ERROR_PARSE, "expected (footprint ...)");
        dc_sexpr_free(ast);
        return -1;
    }

    const char *fp_name = dc_sexpr_value(ast);
    char *lname = lib_name_from_path(path);

    SymEntry entry = {
        .name = fp_name ? strdup(fp_name) : strdup("unknown"),
        .lib_name = strdup(lname),
        .node = ast,
    };
    dc_array_push(lib->footprints, &entry);

    LibFile lf = {
        .path = strdup(path),
        .lib_name = lname,
        .ast = ast,
    };
    dc_array_push(lib->files, &lf);

    return 0;
}

/* =========================================================================
 * Lookup
 * ========================================================================= */

const DC_Sexpr *
dc_elibrary_find_symbol(const DC_ELibrary *lib, const char *lib_id)
{
    if (!lib || !lib_id) return NULL;

    /* Split lib_id into "library:name" */
    const char *colon = strchr(lib_id, ':');
    if (!colon) {
        /* No prefix — search by name only */
        return dc_elibrary_find_symbol_by_name(lib, lib_id);
    }

    size_t lib_len = (size_t)(colon - lib_id);
    const char *sym_name = colon + 1;

    for (size_t i = 0; i < dc_array_length(lib->symbols); i++) {
        SymEntry *e = dc_array_get(lib->symbols, i);
        if (strlen(e->lib_name) == lib_len &&
            memcmp(e->lib_name, lib_id, lib_len) == 0 &&
            strcmp(e->name, sym_name) == 0) {
            return e->node;
        }
    }
    return NULL;
}

const DC_Sexpr *
dc_elibrary_find_symbol_by_name(const DC_ELibrary *lib, const char *name)
{
    if (!lib || !name) return NULL;
    for (size_t i = 0; i < dc_array_length(lib->symbols); i++) {
        SymEntry *e = dc_array_get(lib->symbols, i);
        if (strcmp(e->name, name) == 0) return e->node;
    }
    return NULL;
}

const DC_Sexpr *
dc_elibrary_find_footprint(const DC_ELibrary *lib, const char *lib_id)
{
    if (!lib || !lib_id) return NULL;

    const char *colon = strchr(lib_id, ':');
    const char *fp_name = colon ? colon + 1 : lib_id;

    for (size_t i = 0; i < dc_array_length(lib->footprints); i++) {
        SymEntry *e = dc_array_get(lib->footprints, i);
        if (strcmp(e->name, fp_name) == 0) return e->node;
    }
    return NULL;
}

/* =========================================================================
 * Enumeration
 * ========================================================================= */

size_t
dc_elibrary_symbol_count(const DC_ELibrary *lib)
{
    return lib ? dc_array_length(lib->symbols) : 0;
}

const char *
dc_elibrary_symbol_name(const DC_ELibrary *lib, size_t index)
{
    if (!lib) return NULL;
    SymEntry *e = dc_array_get(lib->symbols, index);
    return e ? e->name : NULL;
}
