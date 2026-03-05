#include "gl/stl_loader.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * Binary STL format:
 *   80 bytes header
 *   4 bytes uint32 triangle count
 *   Per triangle (50 bytes):
 *     12 bytes normal (3 floats)
 *     36 bytes vertices (3 * 3 floats)
 *     2 bytes attribute byte count
 * ---------------------------------------------------------------------- */

static int
is_ascii_stl(const unsigned char *data, size_t size)
{
    /* ASCII STL starts with "solid " */
    if (size < 6) return 0;
    if (memcmp(data, "solid", 5) != 0) return 0;
    /* But binary can also start with "solid" in the header.
     * Check if first 200 bytes are all printable ASCII. */
    size_t check = size < 200 ? size : 200;
    for (size_t i = 0; i < check; i++) {
        if (data[i] > 127 && data[i] != 0) return 0;
    }
    return 1;
}

static void
compute_bounds(DC_StlMesh *mesh)
{
    if (mesh->num_vertices == 0) {
        memset(mesh->min, 0, sizeof(mesh->min));
        memset(mesh->max, 0, sizeof(mesh->max));
        memset(mesh->center, 0, sizeof(mesh->center));
        mesh->extent = 1.0f;
        return;
    }

    mesh->min[0] = mesh->min[1] = mesh->min[2] = 1e30f;
    mesh->max[0] = mesh->max[1] = mesh->max[2] = -1e30f;

    for (int i = 0; i < mesh->num_vertices; i++) {
        float *v = mesh->data + i * 6 + 3; /* skip normal */
        for (int j = 0; j < 3; j++) {
            if (v[j] < mesh->min[j]) mesh->min[j] = v[j];
            if (v[j] > mesh->max[j]) mesh->max[j] = v[j];
        }
    }

    float dx = mesh->max[0] - mesh->min[0];
    float dy = mesh->max[1] - mesh->min[1];
    float dz = mesh->max[2] - mesh->min[2];

    mesh->center[0] = (mesh->min[0] + mesh->max[0]) * 0.5f;
    mesh->center[1] = (mesh->min[1] + mesh->max[1]) * 0.5f;
    mesh->center[2] = (mesh->min[2] + mesh->max[2]) * 0.5f;

    mesh->extent = dx;
    if (dy > mesh->extent) mesh->extent = dy;
    if (dz > mesh->extent) mesh->extent = dz;
    if (mesh->extent < 0.001f) mesh->extent = 1.0f;
}

static DC_StlMesh *
load_binary(const unsigned char *data, size_t size)
{
    if (size < 84) return NULL;

    unsigned int ntri;
    memcpy(&ntri, data + 80, 4);

    size_t expected = 84 + (size_t)ntri * 50;
    if (size < expected) return NULL;

    DC_StlMesh *mesh = calloc(1, sizeof(*mesh));
    if (!mesh) return NULL;

    mesh->num_triangles = (int)ntri;
    mesh->num_vertices = (int)ntri * 3;
    mesh->data = malloc((size_t)mesh->num_vertices * 6 * sizeof(float));
    if (!mesh->data) { free(mesh); return NULL; }

    const unsigned char *ptr = data + 84;
    float *out = mesh->data;

    for (unsigned int t = 0; t < ntri; t++) {
        float normal[3];
        memcpy(normal, ptr, 12);
        ptr += 12;

        for (int v = 0; v < 3; v++) {
            /* Normal */
            out[0] = normal[0];
            out[1] = normal[1];
            out[2] = normal[2];
            /* Vertex */
            memcpy(out + 3, ptr, 12);
            ptr += 12;
            out += 6;
        }

        ptr += 2; /* attribute byte count */
    }

    compute_bounds(mesh);
    return mesh;
}

static DC_StlMesh *
load_ascii(const char *text, size_t size)
{
    /* Count triangles first */
    int ntri = 0;
    const char *p = text;
    const char *end = text + size;
    while (p < end) {
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p + 5 <= end && memcmp(p, "facet", 5) == 0) ntri++;
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }

    if (ntri == 0) return NULL;

    DC_StlMesh *mesh = calloc(1, sizeof(*mesh));
    if (!mesh) return NULL;
    mesh->num_triangles = ntri;
    mesh->num_vertices = ntri * 3;
    mesh->data = malloc((size_t)mesh->num_vertices * 6 * sizeof(float));
    if (!mesh->data) { free(mesh); return NULL; }

    float *out = mesh->data;
    p = text;
    float normal[3] = {0};
    int in_facet = 0;

    while (p < end) {
        while (p < end && isspace((unsigned char)*p)) p++;
        const char *line = p;
        while (p < end && *p != '\n') p++;
        if (p < end) p++;

        if (!in_facet && memcmp(line, "facet", 5) == 0) {
            sscanf(line, "facet normal %f %f %f", &normal[0], &normal[1], &normal[2]);
            in_facet = 1;
        } else if (in_facet && memcmp(line, "vertex", 6) == 0) {
            out[0] = normal[0];
            out[1] = normal[1];
            out[2] = normal[2];
            sscanf(line, "vertex %f %f %f", &out[3], &out[4], &out[5]);
            out += 6;
        } else if (in_facet && memcmp(line, "endfacet", 8) == 0) {
            in_facet = 0;
        }
    }

    /* Adjust actual count if parsing was short */
    int actual_verts = (int)(out - mesh->data) / 6;
    mesh->num_vertices = actual_verts;
    mesh->num_triangles = actual_verts / 3;

    compute_bounds(mesh);
    return mesh;
}

DC_StlMesh *
dc_stl_load(const char *path)
{
    if (!path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 84) { fclose(f); return NULL; }

    unsigned char *data = malloc((size_t)size);
    if (!data) { fclose(f); return NULL; }

    size_t n = fread(data, 1, (size_t)size, f);
    fclose(f);

    DC_StlMesh *mesh;
    if (is_ascii_stl(data, n))
        mesh = load_ascii((const char *)data, n);
    else
        mesh = load_binary(data, n);

    free(data);
    return mesh;
}

void
dc_stl_free(DC_StlMesh *mesh)
{
    if (!mesh) return;
    free(mesh->data);
    free(mesh);
}
