#define _POSIX_C_SOURCE 200809L

#include "eda_ratsnest.h"
#include "eda/eda_pcb.h"
#include "core/array.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal structures
 * ========================================================================= */

/* A point belonging to a net (pad position or track/via endpoint). */
typedef struct {
    double x, y;
    int    net_id;
    int    cluster;  /* union-find cluster id */
} NetPoint;

struct DC_Ratsnest {
    DC_Array *lines;          /* DC_RatsnestLine elements */
    size_t    incomplete_nets;
};

/* =========================================================================
 * Union-Find for clustering connected copper
 * ========================================================================= */
static int uf_find(int *parent, int i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]]; /* path compression */
        i = parent[i];
    }
    return i;
}

static void uf_union(int *parent, int *rank, int a, int b)
{
    a = uf_find(parent, a);
    b = uf_find(parent, b);
    if (a == b) return;
    if (rank[a] < rank[b]) { int t = a; a = b; b = t; }
    parent[b] = a;
    if (rank[a] == rank[b]) rank[a]++;
}

/* =========================================================================
 * Distance helper
 * ========================================================================= */
static double dist2(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1, dy = y2 - y1;
    return dx * dx + dy * dy;
}

/* Check if a point is on a track segment (within tolerance). */
static int point_on_track(double px, double py,
                           double x1, double y1, double x2, double y2,
                           double tol)
{
    double dx = x2 - x1, dy = y2 - y1;
    double len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) return dist2(px, py, x1, y1) < tol * tol;

    double t = ((px - x1) * dx + (py - y1) * dy) / len2;
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    double cx = x1 + t * dx, cy = y1 + t * dy;
    return dist2(px, py, cx, cy) < tol * tol;
}

/* =========================================================================
 * Compute ratsnest
 *
 * Algorithm per net:
 * 1. Collect all pad positions + track endpoints + via positions
 * 2. Union-find: merge points connected by copper (same track, via proximity)
 * 3. For each pair of disconnected clusters, find the shortest bridge
 * 4. That bridge is a ratsnest line
 * ========================================================================= */
DC_Ratsnest *
dc_ratsnest_compute(const DC_EPcb *pcb)
{
    if (!pcb) return NULL;

    DC_Ratsnest *rn = calloc(1, sizeof(*rn));
    if (!rn) return NULL;
    rn->lines = dc_array_new(sizeof(DC_RatsnestLine));
    if (!rn->lines) { free(rn); return NULL; }

    size_t n_nets = dc_epcb_net_count(pcb);
    if (n_nets == 0) return rn;

    /* For each net, collect points and compute MST of disconnected clusters */
    for (size_t ni = 0; ni < n_nets; ni++) {
        DC_PcbNet *net = dc_epcb_get_net(pcb, ni);
        if (!net || net->id == 0) continue; /* skip unconnected net */

        int net_id = net->id;

        /* Collect all points belonging to this net */
        DC_Array *points = dc_array_new(sizeof(NetPoint));
        if (!points) continue;

        /* Pad positions from footprints */
        for (size_t fi = 0; fi < dc_epcb_footprint_count(pcb); fi++) {
            DC_PcbFootprint *fp = dc_epcb_get_footprint(pcb, fi);
            if (!fp->pads) continue;
            for (size_t pi = 0; pi < dc_array_length(fp->pads); pi++) {
                DC_PcbPad *pad = dc_array_get(fp->pads, pi);
                if (pad->net_id == net_id) {
                    NetPoint np = {
                        .x = fp->x + pad->x,
                        .y = fp->y + pad->y,
                        .net_id = net_id,
                        .cluster = (int)dc_array_length(points)
                    };
                    dc_array_push(points, &np);
                }
            }
        }

        /* Track endpoints */
        for (size_t ti = 0; ti < dc_epcb_track_count(pcb); ti++) {
            DC_PcbTrack *t = dc_epcb_get_track(pcb, ti);
            if (t->net_id != net_id) continue;

            NetPoint np1 = { t->x1, t->y1, net_id, (int)dc_array_length(points) };
            dc_array_push(points, &np1);
            NetPoint np2 = { t->x2, t->y2, net_id, (int)dc_array_length(points) };
            dc_array_push(points, &np2);
        }

        /* Via positions */
        for (size_t vi = 0; vi < dc_epcb_via_count(pcb); vi++) {
            DC_PcbVia *v = dc_epcb_get_via(pcb, vi);
            if (v->net_id != net_id) continue;
            NetPoint np = { v->x, v->y, net_id, (int)dc_array_length(points) };
            dc_array_push(points, &np);
        }

        size_t n_pts = dc_array_length(points);
        if (n_pts < 2) { dc_array_free(points); continue; }

        /* Union-Find: initialize */
        int *parent = malloc(n_pts * sizeof(int));
        int *rank = calloc(n_pts, sizeof(int));
        if (!parent || !rank) {
            free(parent); free(rank);
            dc_array_free(points);
            continue;
        }
        for (size_t i = 0; i < n_pts; i++) parent[i] = (int)i;

        /* Merge points that are coincident (within 0.01mm tolerance) */
        double tol = 0.01;
        for (size_t i = 0; i < n_pts; i++) {
            NetPoint *a = dc_array_get(points, i);
            for (size_t j = i + 1; j < n_pts; j++) {
                NetPoint *b = dc_array_get(points, j);
                if (dist2(a->x, a->y, b->x, b->y) < tol * tol) {
                    uf_union(parent, rank, (int)i, (int)j);
                }
            }
        }

        /* Merge points connected by tracks */
        for (size_t ti = 0; ti < dc_epcb_track_count(pcb); ti++) {
            DC_PcbTrack *t = dc_epcb_get_track(pcb, ti);
            if (t->net_id != net_id) continue;

            for (size_t i = 0; i < n_pts; i++) {
                NetPoint *p = dc_array_get(points, i);
                if (point_on_track(p->x, p->y, t->x1, t->y1, t->x2, t->y2, tol + t->width / 2)) {
                    /* Find another point on this track and merge */
                    for (size_t j = i + 1; j < n_pts; j++) {
                        NetPoint *q = dc_array_get(points, j);
                        if (point_on_track(q->x, q->y, t->x1, t->y1, t->x2, t->y2, tol + t->width / 2)) {
                            uf_union(parent, rank, (int)i, (int)j);
                        }
                    }
                }
            }
        }

        /* Count distinct clusters */
        int n_clusters = 0;
        int *cluster_map = calloc(n_pts, sizeof(int));
        int *cluster_ids = malloc(n_pts * sizeof(int));
        if (!cluster_map || !cluster_ids) {
            free(parent); free(rank); free(cluster_map); free(cluster_ids);
            dc_array_free(points);
            continue;
        }

        for (size_t i = 0; i < n_pts; i++) {
            int root = uf_find(parent, (int)i);
            int found = 0;
            for (int c = 0; c < n_clusters; c++) {
                if (cluster_ids[c] == root) {
                    cluster_map[i] = c;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                cluster_ids[n_clusters] = root;
                cluster_map[i] = n_clusters;
                n_clusters++;
            }
        }

        /* If more than 1 cluster, generate ratsnest lines (MST between clusters) */
        if (n_clusters > 1) {
            rn->incomplete_nets++;

            /* Greedy MST: repeatedly find shortest inter-cluster edge */
            int *merged = calloc((size_t)n_clusters, sizeof(int));
            if (merged) {
                merged[0] = 1;
                for (int step = 1; step < n_clusters; step++) {
                    double best_d = 1e30;
                    size_t best_i = 0, best_j = 0;

                    for (size_t i = 0; i < n_pts; i++) {
                        if (!merged[cluster_map[i]]) continue;
                        NetPoint *a = dc_array_get(points, i);
                        for (size_t j = 0; j < n_pts; j++) {
                            if (merged[cluster_map[j]]) continue;
                            NetPoint *b = dc_array_get(points, j);
                            double d = dist2(a->x, a->y, b->x, b->y);
                            if (d < best_d) {
                                best_d = d;
                                best_i = i;
                                best_j = j;
                            }
                        }
                    }

                    if (best_d < 1e30) {
                        NetPoint *a = dc_array_get(points, best_i);
                        NetPoint *b = dc_array_get(points, best_j);
                        DC_RatsnestLine line = {
                            a->x, a->y, b->x, b->y, net_id
                        };
                        dc_array_push(rn->lines, &line);
                        merged[cluster_map[best_j]] = 1;
                    }
                }
                free(merged);
            }
        }

        free(parent);
        free(rank);
        free(cluster_map);
        free(cluster_ids);
        dc_array_free(points);
    }

    return rn;
}

void
dc_ratsnest_free(DC_Ratsnest *rn)
{
    if (!rn) return;
    dc_array_free(rn->lines);
    free(rn);
}

size_t dc_ratsnest_line_count(const DC_Ratsnest *rn)
{
    return rn ? dc_array_length(rn->lines) : 0;
}

const DC_RatsnestLine *dc_ratsnest_get_line(const DC_Ratsnest *rn, size_t i)
{
    if (!rn || i >= dc_array_length(rn->lines)) return NULL;
    return dc_array_get(rn->lines, i);
}

size_t dc_ratsnest_incomplete_net_count(const DC_Ratsnest *rn)
{
    return rn ? rn->incomplete_nets : 0;
}
