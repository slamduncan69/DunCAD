#define _POSIX_C_SOURCE 200809L
/*
 * eda_library.c — KiCad symbol and footprint library loader.
 */

#include "eda/eda_library.h"

#include <dirent.h>
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

/* A registered-but-not-yet-loaded library file */
typedef struct {
    char *path;      /* file path — owned */
    char *lib_name;  /* derived library name — owned */
} PendingLib;

static void
pending_lib_cleanup(PendingLib *pl)
{
    free(pl->path);
    free(pl->lib_name);
}

struct DC_ELibrary {
    DC_Array *symbols;    /* SymEntry */
    DC_Array *footprints; /* SymEntry (reuse same struct) */
    DC_Array *files;      /* LibFile */
    DC_Array *pending;    /* PendingLib — registered but not loaded */
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
    lib->pending    = dc_array_new(sizeof(PendingLib));

    if (!lib->symbols || !lib->footprints || !lib->files || !lib->pending) {
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
    if (lib->pending) {
        for (size_t i = 0; i < dc_array_length(lib->pending); i++)
            pending_lib_cleanup(dc_array_get(lib->pending, i));
        dc_array_free(lib->pending);
    }
    free(lib);
}

/* =========================================================================
 * Registration (lazy loading)
 * ========================================================================= */

int
dc_elibrary_register_symbols(DC_ELibrary *lib, const char *path)
{
    if (!lib || !path) return -1;

    char *lname = lib_name_from_path(path);
    if (!lname) return -1;

    PendingLib pl = {
        .path = strdup(path),
        .lib_name = lname,
    };
    if (!pl.path) { free(lname); return -1; }
    dc_array_push(lib->pending, &pl);
    return 0;
}

/* Attempt to load a pending library by name. Returns 1 if loaded, 0 if not found. */
static int
ensure_lib_loaded(DC_ELibrary *lib, const char *lib_name)
{
    if (!lib || !lib_name) return 0;

    /* Check if already loaded */
    size_t nfiles = dc_array_length(lib->files);
    for (size_t i = 0; i < nfiles; i++) {
        LibFile *lf = dc_array_get(lib->files, i);
        if (strcmp(lf->lib_name, lib_name) == 0) return 1; /* already loaded */
    }

    /* Find in pending and load */
    size_t npend = dc_array_length(lib->pending);
    for (size_t i = 0; i < npend; i++) {
        PendingLib *pl = dc_array_get(lib->pending, i);
        if (strcmp(pl->lib_name, lib_name) == 0) {
            DC_Error err = {0};
            dc_elibrary_load_symbols(lib, pl->path, &err);
            /* Remove from pending (already consumed) */
            pending_lib_cleanup(pl);
            dc_array_remove(lib->pending, i);
            return 1;
        }
    }
    return 0;
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

    /* Lazy-load this library if pending */
    char lname_buf[256];
    if (lib_len < sizeof(lname_buf)) {
        memcpy(lname_buf, lib_id, lib_len);
        lname_buf[lib_len] = '\0';
        ensure_lib_loaded((DC_ELibrary *)lib, lname_buf);
    }

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

const char *
dc_elibrary_symbol_lib_name(const DC_ELibrary *lib, size_t index)
{
    if (!lib) return NULL;
    SymEntry *e = dc_array_get(lib->symbols, index);
    return e ? e->lib_name : NULL;
}

/* =========================================================================
 * Per-library enumeration
 * ========================================================================= */

size_t
dc_elibrary_lib_count(const DC_ELibrary *lib)
{
    if (!lib) return 0;
    /* Count distinct lib_names from files (loaded) + pending */
    size_t count = 0;
    size_t nfiles = dc_array_length(lib->files);
    for (size_t i = 0; i < nfiles; i++) {
        LibFile *lf = dc_array_get(lib->files, i);
        int dup = 0;
        for (size_t j = 0; j < i; j++) {
            LibFile *prev = dc_array_get(lib->files, j);
            if (strcmp(lf->lib_name, prev->lib_name) == 0) { dup = 1; break; }
        }
        if (!dup) count++;
    }
    /* Add pending libs not already in loaded */
    size_t npend = dc_array_length(lib->pending);
    for (size_t i = 0; i < npend; i++) {
        PendingLib *pl = dc_array_get(lib->pending, i);
        int dup = 0;
        /* Check against loaded */
        for (size_t j = 0; j < nfiles; j++) {
            LibFile *lf = dc_array_get(lib->files, j);
            if (strcmp(lf->lib_name, pl->lib_name) == 0) { dup = 1; break; }
        }
        /* Check against earlier pending */
        if (!dup) {
            for (size_t j = 0; j < i; j++) {
                PendingLib *prev = dc_array_get(lib->pending, j);
                if (strcmp(pl->lib_name, prev->lib_name) == 0) { dup = 1; break; }
            }
        }
        if (!dup) count++;
    }
    return count;
}

const char *
dc_elibrary_lib_name(const DC_ELibrary *lib, size_t index)
{
    if (!lib) return NULL;
    size_t count = 0;
    /* Loaded libs first */
    size_t nfiles = dc_array_length(lib->files);
    for (size_t i = 0; i < nfiles; i++) {
        LibFile *lf = dc_array_get(lib->files, i);
        int dup = 0;
        for (size_t j = 0; j < i; j++) {
            LibFile *prev = dc_array_get(lib->files, j);
            if (strcmp(lf->lib_name, prev->lib_name) == 0) { dup = 1; break; }
        }
        if (!dup) {
            if (count == index) return lf->lib_name;
            count++;
        }
    }
    /* Then pending libs */
    size_t npend = dc_array_length(lib->pending);
    for (size_t i = 0; i < npend; i++) {
        PendingLib *pl = dc_array_get(lib->pending, i);
        int dup = 0;
        for (size_t j = 0; j < nfiles; j++) {
            LibFile *lf = dc_array_get(lib->files, j);
            if (strcmp(lf->lib_name, pl->lib_name) == 0) { dup = 1; break; }
        }
        if (!dup) {
            for (size_t j = 0; j < i; j++) {
                PendingLib *prev = dc_array_get(lib->pending, j);
                if (strcmp(pl->lib_name, prev->lib_name) == 0) { dup = 1; break; }
            }
        }
        if (!dup) {
            if (count == index) return pl->lib_name;
            count++;
        }
    }
    return NULL;
}

size_t
dc_elibrary_lib_symbol_count(const DC_ELibrary *lib, const char *lib_name)
{
    if (!lib || !lib_name) return 0;
    /* Trigger lazy load if needed (cast away const for mutation) */
    ensure_lib_loaded((DC_ELibrary *)lib, lib_name);
    size_t count = 0;
    size_t n = dc_array_length(lib->symbols);
    for (size_t i = 0; i < n; i++) {
        SymEntry *e = dc_array_get(lib->symbols, i);
        if (strcmp(e->lib_name, lib_name) == 0) count++;
    }
    return count;
}

const char *
dc_elibrary_lib_symbol_name(const DC_ELibrary *lib,
                              const char *lib_name, size_t index)
{
    if (!lib || !lib_name) return NULL;
    ensure_lib_loaded((DC_ELibrary *)lib, lib_name);
    size_t count = 0;
    size_t n = dc_array_length(lib->symbols);
    for (size_t i = 0; i < n; i++) {
        SymEntry *e = dc_array_get(lib->symbols, i);
        if (strcmp(e->lib_name, lib_name) == 0) {
            if (count == index) return e->name;
            count++;
        }
    }
    return NULL;
}

/* =========================================================================
 * Footprint enumeration + batch loading
 * ========================================================================= */

size_t
dc_elibrary_footprint_count(const DC_ELibrary *lib)
{
    return lib ? dc_array_length(lib->footprints) : 0;
}

const char *
dc_elibrary_footprint_name(const DC_ELibrary *lib, size_t index)
{
    if (!lib) return NULL;
    SymEntry *e = dc_array_get(lib->footprints, index);
    return e ? e->name : NULL;
}

const char *
dc_elibrary_footprint_lib_name(const DC_ELibrary *lib, size_t index)
{
    if (!lib) return NULL;
    SymEntry *e = dc_array_get(lib->footprints, index);
    return e ? e->lib_name : NULL;
}

int
dc_elibrary_load_footprint_dir(DC_ELibrary *lib, const char *dir_path,
                                 DC_Error *err)
{
    if (!lib || !dir_path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL arg");
        return -1;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "cannot open dir %s", dir_path);
        return -1;
    }

    int loaded = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        /* Only load .kicad_mod files */
        size_t nlen = strlen(ent->d_name);
        if (nlen < 10) continue;
        if (strcmp(ent->d_name + nlen - 10, ".kicad_mod") != 0) continue;

        /* Build full path */
        size_t plen = strlen(dir_path);
        char *fpath = malloc(plen + 1 + nlen + 1);
        if (!fpath) continue;
        memcpy(fpath, dir_path, plen);
        fpath[plen] = '/';
        memcpy(fpath + plen + 1, ent->d_name, nlen + 1);

        DC_Error lerr = {0};
        if (dc_elibrary_load_footprint(lib, fpath, &lerr) == 0)
            loaded++;

        free(fpath);
    }

    closedir(dir);
    return loaded;
}

/* =========================================================================
 * Symbol property / pin inspection
 * ========================================================================= */

const char *
dc_elibrary_symbol_property(const DC_Sexpr *sym_node, const char *key)
{
    if (!sym_node || !key) return NULL;

    size_t count = 0;
    DC_Sexpr **props = dc_sexpr_find_all(sym_node, "property", &count);
    if (!props) return NULL;

    const char *result = NULL;
    for (size_t i = 0; i < count; i++) {
        const char *pname = dc_sexpr_value(props[i]);
        if (pname && strcmp(pname, key) == 0) {
            result = dc_sexpr_value_at(props[i], 1);
            break;
        }
    }
    free(props);
    return result;
}

size_t
dc_elibrary_symbol_pin_count(const DC_Sexpr *sym_node)
{
    if (!sym_node) return 0;

    size_t total = 0;

    /* Count pins directly in this node */
    size_t pin_count = 0;
    DC_Sexpr **pins = dc_sexpr_find_all(sym_node, "pin", &pin_count);
    if (pins) {
        total += pin_count;
        free(pins);
    }

    /* Count pins in sub-symbols */
    size_t sub_count = 0;
    DC_Sexpr **subs = dc_sexpr_find_all(sym_node, "symbol", &sub_count);
    if (subs) {
        for (size_t i = 0; i < sub_count; i++) {
            size_t sp = 0;
            DC_Sexpr **sub_pins = dc_sexpr_find_all(subs[i], "pin", &sp);
            if (sub_pins) {
                total += sp;
                free(sub_pins);
            }
        }
        free(subs);
    }

    return total;
}
