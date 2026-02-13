#include "internal.h"

#include <limits.h>
#include <stdlib.h>

typedef struct dg_room_graph_edge {
    int a;
    int b;
    int weight;
    int in_mst;
} dg_room_graph_edge_t;

typedef struct dg_room_graph_union_find {
    int parent;
    int rank;
} dg_room_graph_union_find_t;

static dg_point_t dg_room_graph_center(const dg_room_metadata_t *room)
{
    dg_point_t center;

    center.x = room->bounds.x + room->bounds.width / 2;
    center.y = room->bounds.y + room->bounds.height / 2;
    return center;
}

static void dg_room_graph_carve_room(dg_map_t *map, const dg_rect_t *room)
{
    int y;
    int x;

    for (y = room->y; y < room->y + room->height; ++y) {
        for (x = room->x; x < room->x + room->width; ++x) {
            (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
        }
    }
}

static void dg_room_graph_carve_horizontal(dg_map_t *map, int x0, int x1, int y)
{
    int x;
    int start;
    int end;

    start = dg_min_int(x0, x1);
    end = dg_max_int(x0, x1);
    for (x = start; x <= end; ++x) {
        (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
    }
}

static void dg_room_graph_carve_vertical(dg_map_t *map, int x, int y0, int y1)
{
    int y;
    int start;
    int end;

    start = dg_min_int(y0, y1);
    end = dg_max_int(y0, y1);
    for (y = start; y <= end; ++y) {
        (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
    }
}

static dg_status_t dg_room_graph_connect_rooms(
    dg_map_t *map,
    dg_rng_t *rng,
    int room_a,
    int room_b,
    unsigned char *connected,
    size_t room_count
)
{
    const dg_room_metadata_t *a;
    const dg_room_metadata_t *b;
    dg_point_t ca;
    dg_point_t cb;
    int corridor_length;
    int horizontal_first;
    size_t index_ab;
    size_t index_ba;

    if (map == NULL || rng == NULL || connected == NULL ||
        room_a < 0 || room_b < 0 || room_a == room_b) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if ((size_t)room_a >= room_count || (size_t)room_b >= room_count) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    index_ab = (size_t)room_a * room_count + (size_t)room_b;
    index_ba = (size_t)room_b * room_count + (size_t)room_a;
    if (connected[index_ab] != 0u || connected[index_ba] != 0u) {
        return DG_STATUS_OK;
    }

    a = &map->metadata.rooms[room_a];
    b = &map->metadata.rooms[room_b];
    ca = dg_room_graph_center(a);
    cb = dg_room_graph_center(b);

    horizontal_first = (dg_rng_next_u32(rng) & 1u) != 0u;
    if (horizontal_first != 0) {
        dg_room_graph_carve_horizontal(map, ca.x, cb.x, ca.y);
        dg_room_graph_carve_vertical(map, cb.x, ca.y, cb.y);
    } else {
        dg_room_graph_carve_vertical(map, ca.x, ca.y, cb.y);
        dg_room_graph_carve_horizontal(map, ca.x, cb.x, cb.y);
    }

    corridor_length = 1 + abs(ca.x - cb.x) + abs(ca.y - cb.y);
    if (dg_map_add_corridor(map, room_a, room_b, 1, corridor_length) != DG_STATUS_OK) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    connected[index_ab] = 1u;
    connected[index_ba] = 1u;
    return DG_STATUS_OK;
}

static int dg_room_graph_edge_cmp(const void *left, const void *right)
{
    const dg_room_graph_edge_t *a;
    const dg_room_graph_edge_t *b;

    a = (const dg_room_graph_edge_t *)left;
    b = (const dg_room_graph_edge_t *)right;
    if (a->weight < b->weight) {
        return -1;
    }
    if (a->weight > b->weight) {
        return 1;
    }
    if (a->a < b->a) {
        return -1;
    }
    if (a->a > b->a) {
        return 1;
    }
    return a->b - b->b;
}

static int dg_room_graph_find_root(dg_room_graph_union_find_t *set, int index)
{
    int root;

    root = index;
    while (set[root].parent != root) {
        root = set[root].parent;
    }
    while (set[index].parent != index) {
        int next = set[index].parent;
        set[index].parent = root;
        index = next;
    }

    return root;
}

static int dg_room_graph_union_sets(
    dg_room_graph_union_find_t *set,
    int a,
    int b
)
{
    int ra;
    int rb;

    ra = dg_room_graph_find_root(set, a);
    rb = dg_room_graph_find_root(set, b);
    if (ra == rb) {
        return 0;
    }

    if (set[ra].rank < set[rb].rank) {
        set[ra].parent = rb;
    } else if (set[ra].rank > set[rb].rank) {
        set[rb].parent = ra;
    } else {
        set[rb].parent = ra;
        set[ra].rank += 1;
    }

    return 1;
}

static int dg_room_graph_has_edge(
    const dg_room_graph_edge_t *edges,
    size_t edge_count,
    int a,
    int b
)
{
    size_t i;

    for (i = 0; i < edge_count; ++i) {
        if (edges[i].a == a && edges[i].b == b) {
            return 1;
        }
    }

    return 0;
}

static dg_status_t dg_room_graph_build_candidate_edges(
    const dg_map_t *map,
    int neighbor_candidates,
    dg_room_graph_edge_t **out_edges,
    size_t *out_edge_count
)
{
    size_t room_count;
    size_t max_edges;
    dg_room_graph_edge_t *edges;
    size_t edge_count;
    size_t i;

    if (map == NULL || out_edges == NULL || out_edge_count == NULL || neighbor_candidates < 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_edges = NULL;
    *out_edge_count = 0u;
    room_count = map->metadata.room_count;
    if (room_count < 2u) {
        return DG_STATUS_OK;
    }

    max_edges = room_count * (size_t)neighbor_candidates;
    if (max_edges < room_count - 1u) {
        max_edges = room_count - 1u;
    }

    edges = (dg_room_graph_edge_t *)calloc(max_edges, sizeof(*edges));
    if (edges == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    edge_count = 0u;

    for (i = 0; i < room_count; ++i) {
        int nearest_ids[8];
        int nearest_dist[8];
        int keep_count;
        size_t j;
        int k;

        keep_count = dg_clamp_int(neighbor_candidates, 1, 8);
        for (k = 0; k < keep_count; ++k) {
            nearest_ids[k] = -1;
            nearest_dist[k] = INT_MAX;
        }

        for (j = 0; j < room_count; ++j) {
            dg_point_t ci;
            dg_point_t cj;
            int dx;
            int dy;
            int dist;

            if (i == j) {
                continue;
            }

            ci = dg_room_graph_center(&map->metadata.rooms[i]);
            cj = dg_room_graph_center(&map->metadata.rooms[j]);
            dx = ci.x - cj.x;
            dy = ci.y - cj.y;
            dist = dx * dx + dy * dy;

            for (k = 0; k < keep_count; ++k) {
                if (dist < nearest_dist[k]) {
                    int m;
                    for (m = keep_count - 1; m > k; --m) {
                        nearest_dist[m] = nearest_dist[m - 1];
                        nearest_ids[m] = nearest_ids[m - 1];
                    }
                    nearest_dist[k] = dist;
                    nearest_ids[k] = (int)j;
                    break;
                }
            }
        }

        for (k = 0; k < keep_count; ++k) {
            int a;
            int b;

            if (nearest_ids[k] < 0) {
                continue;
            }

            a = (int)i;
            b = nearest_ids[k];
            if (a > b) {
                int tmp = a;
                a = b;
                b = tmp;
            }

            if (dg_room_graph_has_edge(edges, edge_count, a, b) != 0) {
                continue;
            }

            if (edge_count >= max_edges) {
                dg_room_graph_edge_t *grown;
                size_t new_max;

                new_max = max_edges * 2u;
                if (new_max <= max_edges) {
                    free(edges);
                    return DG_STATUS_ALLOCATION_FAILED;
                }
                grown = (dg_room_graph_edge_t *)realloc(edges, new_max * sizeof(*edges));
                if (grown == NULL) {
                    free(edges);
                    return DG_STATUS_ALLOCATION_FAILED;
                }
                edges = grown;
                max_edges = new_max;
            }

            edges[edge_count].a = a;
            edges[edge_count].b = b;
            edges[edge_count].weight = nearest_dist[k];
            edges[edge_count].in_mst = 0;
            edge_count += 1u;
        }
    }

    if (edge_count == 0u) {
        for (i = 1u; i < room_count; ++i) {
            if (edge_count >= max_edges) {
                break;
            }
            edges[edge_count].a = (int)(i - 1u);
            edges[edge_count].b = (int)i;
            edges[edge_count].weight = 1;
            edges[edge_count].in_mst = 0;
            edge_count += 1u;
        }
    }

    *out_edges = edges;
    *out_edge_count = edge_count;
    return DG_STATUS_OK;
}

dg_status_t dg_generate_room_graph_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    const dg_room_graph_config_t *config;
    int target_rooms;
    int max_place_attempts;
    int placed_rooms;
    int attempt;
    dg_room_graph_edge_t *edges;
    size_t edge_count;
    unsigned char *connected;
    dg_room_graph_union_find_t *union_find;
    size_t room_count;
    size_t i;
    int mst_edges;
    dg_status_t status;

    if (request == NULL || map == NULL || map->tiles == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    config = &request->params.room_graph;
    target_rooms = dg_rng_range(rng, config->min_rooms, config->max_rooms);
    max_place_attempts = target_rooms * 80;
    if (max_place_attempts < 400) {
        max_place_attempts = 400;
    }

    placed_rooms = 0;
    for (attempt = 0; attempt < max_place_attempts && placed_rooms < target_rooms; ++attempt) {
        int max_width;
        int max_height;
        int width;
        int height;
        int x;
        int y;
        dg_rect_t room;
        size_t ri;
        int overlaps;

        max_width = dg_min_int(config->room_max_size, map->width - 4);
        max_height = dg_min_int(config->room_max_size, map->height - 4);
        if (max_width < config->room_min_size || max_height < config->room_min_size) {
            break;
        }

        width = dg_rng_range(rng, config->room_min_size, max_width);
        height = dg_rng_range(rng, config->room_min_size, max_height);

        if (map->width - width - 2 <= 1 || map->height - height - 2 <= 1) {
            continue;
        }

        x = dg_rng_range(rng, 1, map->width - width - 2);
        y = dg_rng_range(rng, 1, map->height - height - 2);
        room = (dg_rect_t){x, y, width, height};

        overlaps = 0;
        for (ri = 0u; ri < map->metadata.room_count; ++ri) {
            if (dg_rects_overlap_with_padding(&map->metadata.rooms[ri].bounds, &room, 1)) {
                overlaps = 1;
                break;
            }
        }
        if (overlaps != 0) {
            continue;
        }

        dg_room_graph_carve_room(map, &room);
        if (dg_map_add_room(map, &room, DG_ROOM_FLAG_NONE) != DG_STATUS_OK) {
            return DG_STATUS_ALLOCATION_FAILED;
        }
        placed_rooms += 1;
    }

    room_count = map->metadata.room_count;
    if (room_count < 2u) {
        return DG_STATUS_GENERATION_FAILED;
    }

    edges = NULL;
    edge_count = 0u;
    status = dg_room_graph_build_candidate_edges(
        map,
        config->neighbor_candidates,
        &edges,
        &edge_count
    );
    if (status != DG_STATUS_OK) {
        return status;
    }
    if (edge_count == 0u) {
        free(edges);
        return DG_STATUS_GENERATION_FAILED;
    }

    connected = (unsigned char *)calloc(room_count * room_count, sizeof(*connected));
    union_find = (dg_room_graph_union_find_t *)calloc(room_count, sizeof(*union_find));
    if (connected == NULL || union_find == NULL) {
        free(edges);
        free(connected);
        free(union_find);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (i = 0u; i < room_count; ++i) {
        union_find[i].parent = (int)i;
        union_find[i].rank = 0;
    }

    qsort(edges, edge_count, sizeof(*edges), dg_room_graph_edge_cmp);

    mst_edges = 0;
    for (i = 0u; i < edge_count; ++i) {
        int a = edges[i].a;
        int b = edges[i].b;
        if (dg_room_graph_union_sets(union_find, a, b) == 0) {
            continue;
        }

        status = dg_room_graph_connect_rooms(map, rng, a, b, connected, room_count);
        if (status != DG_STATUS_OK) {
            free(edges);
            free(connected);
            free(union_find);
            return status;
        }
        edges[i].in_mst = 1;
        mst_edges += 1;
        if ((size_t)mst_edges >= room_count - 1u) {
            break;
        }
    }

    if ((size_t)mst_edges < room_count - 1u) {
        free(edges);
        free(connected);
        free(union_find);
        return DG_STATUS_GENERATION_FAILED;
    }

    for (i = 0u; i < edge_count; ++i) {
        if (edges[i].in_mst != 0) {
            continue;
        }

        if (dg_rng_range(rng, 0, 99) >= config->extra_connection_chance_percent) {
            continue;
        }

        status = dg_room_graph_connect_rooms(
            map,
            rng,
            edges[i].a,
            edges[i].b,
            connected,
            room_count
        );
        if (status != DG_STATUS_OK) {
            free(edges);
            free(connected);
            free(union_find);
            return status;
        }
    }

    free(edges);
    free(connected);
    free(union_find);
    return DG_STATUS_OK;
}
