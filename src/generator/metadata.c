#include "internal.h"

#include <limits.h>
#include <stdlib.h>

static bool dg_point_in_rect(const dg_rect_t *rect, int x, int y)
{
    if (rect == NULL) {
        return false;
    }

    return x >= rect->x &&
           y >= rect->y &&
           x < rect->x + rect->width &&
           y < rect->y + rect->height;
}

static bool dg_point_in_any_room(const dg_map_t *map, int x, int y)
{
    size_t i;

    if (map == NULL || map->metadata.rooms == NULL) {
        return false;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        if (dg_point_in_rect(&map->metadata.rooms[i].bounds, x, y)) {
            return true;
        }
    }

    return false;
}

static void dg_clear_room_entrance_metadata(dg_map_t *map)
{
    if (map == NULL) {
        return;
    }

    free(map->metadata.room_entrances);
    map->metadata.room_entrances = NULL;
    map->metadata.room_entrance_count = 0;
    map->metadata.room_entrance_capacity = 0;
}

static void dg_clear_map_edge_opening_metadata(dg_map_t *map)
{
    if (map == NULL) {
        return;
    }

    free(map->metadata.edge_openings);
    map->metadata.edge_openings = NULL;
    map->metadata.edge_opening_count = 0;
    map->metadata.edge_opening_capacity = 0;
    map->metadata.primary_entrance_opening_id = -1;
    map->metadata.primary_exit_opening_id = -1;
}

static void dg_clear_existing_room_door_tiles(dg_map_t *map)
{
    size_t i;
    size_t cell_count;

    if (map == NULL || map->tiles == NULL) {
        return;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    for (i = 0; i < cell_count; ++i) {
        if (map->tiles[i] == DG_TILE_DOOR) {
            map->tiles[i] = DG_TILE_FLOOR;
        }
    }
}

static dg_status_t dg_append_room_entrance(
    dg_map_t *map,
    int room_id,
    int room_x,
    int room_y,
    int corridor_x,
    int corridor_y,
    int normal_x,
    int normal_y
)
{
    dg_room_entrance_metadata_t *entry;

    if (map == NULL || map->tiles == NULL || room_id < 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.room_entrance_count == map->metadata.room_entrance_capacity) {
        size_t new_capacity;
        dg_room_entrance_metadata_t *expanded;

        if (map->metadata.room_entrance_capacity == 0) {
            new_capacity = 16u;
        } else {
            if (map->metadata.room_entrance_capacity > (SIZE_MAX / 2u)) {
                return DG_STATUS_ALLOCATION_FAILED;
            }
            new_capacity = map->metadata.room_entrance_capacity * 2u;
        }

        if (new_capacity > (SIZE_MAX / sizeof(*expanded))) {
            return DG_STATUS_ALLOCATION_FAILED;
        }

        expanded = (dg_room_entrance_metadata_t *)realloc(
            map->metadata.room_entrances,
            new_capacity * sizeof(*expanded)
        );
        if (expanded == NULL) {
            return DG_STATUS_ALLOCATION_FAILED;
        }

        map->metadata.room_entrances = expanded;
        map->metadata.room_entrance_capacity = new_capacity;
    }

    entry = &map->metadata.room_entrances[map->metadata.room_entrance_count];
    entry->room_id = room_id;
    entry->room_tile = (dg_point_t){room_x, room_y};
    entry->corridor_tile = (dg_point_t){corridor_x, corridor_y};
    entry->normal_x = normal_x;
    entry->normal_y = normal_y;
    map->metadata.room_entrance_count += 1u;

    if (dg_map_in_bounds(map, room_x, room_y)) {
        map->tiles[dg_tile_index(map, room_x, room_y)] = DG_TILE_DOOR;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_append_map_edge_opening(
    dg_map_t *map,
    dg_map_edge_side_t side,
    int start,
    int end,
    int edge_x,
    int edge_y,
    int normal_x,
    int normal_y,
    size_t component_id
)
{
    dg_map_edge_opening_t *opening;

    if (map == NULL || map->tiles == NULL || start < 0 || end < start) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.edge_opening_count == map->metadata.edge_opening_capacity) {
        size_t new_capacity;
        dg_map_edge_opening_t *expanded;

        if (map->metadata.edge_opening_capacity == 0u) {
            new_capacity = 8u;
        } else {
            if (map->metadata.edge_opening_capacity > (SIZE_MAX / 2u)) {
                return DG_STATUS_ALLOCATION_FAILED;
            }
            new_capacity = map->metadata.edge_opening_capacity * 2u;
        }

        if (new_capacity > (SIZE_MAX / sizeof(*expanded))) {
            return DG_STATUS_ALLOCATION_FAILED;
        }

        expanded = (dg_map_edge_opening_t *)realloc(
            map->metadata.edge_openings,
            new_capacity * sizeof(*expanded)
        );
        if (expanded == NULL) {
            return DG_STATUS_ALLOCATION_FAILED;
        }

        map->metadata.edge_openings = expanded;
        map->metadata.edge_opening_capacity = new_capacity;
    }

    opening = &map->metadata.edge_openings[map->metadata.edge_opening_count];
    opening->id = (int)map->metadata.edge_opening_count;
    opening->side = side;
    opening->start = start;
    opening->end = end;
    opening->length = (end - start) + 1;
    opening->edge_tile = (dg_point_t){edge_x, edge_y};
    opening->inward_tile = (dg_point_t){edge_x + normal_x, edge_y + normal_y};
    opening->normal_x = normal_x;
    opening->normal_y = normal_y;
    opening->component_id = component_id;
    opening->role = DG_MAP_EDGE_OPENING_ROLE_NONE;
    map->metadata.edge_opening_count += 1u;

    return DG_STATUS_OK;
}

static bool dg_find_room_entrance_direction(
    const dg_map_t *map,
    const dg_rect_t *room,
    int x,
    int y,
    int *out_normal_x,
    int *out_normal_y
)
{
    static const int k_dirs[4][2] = {
        {0, -1},
        {1, 0},
        {0, 1},
        {-1, 0}
    };
    int d;

    if (map == NULL || room == NULL || out_normal_x == NULL || out_normal_y == NULL) {
        return false;
    }

    if (!dg_map_in_bounds(map, x, y) || !dg_is_walkable_tile(dg_map_get_tile(map, x, y))) {
        return false;
    }

    for (d = 0; d < 4; ++d) {
        int nx = x + k_dirs[d][0];
        int ny = y + k_dirs[d][1];

        if (!dg_map_in_bounds(map, nx, ny)) {
            continue;
        }

        if (dg_point_in_rect(room, nx, ny)) {
            continue;
        }
        if (dg_point_in_any_room(map, nx, ny)) {
            continue;
        }
        if (!dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
            continue;
        }

        *out_normal_x = k_dirs[d][0];
        *out_normal_y = k_dirs[d][1];
        return true;
    }

    return false;
}

static dg_status_t dg_build_room_entrance_metadata(
    dg_map_t *map,
    dg_map_generation_class_t generation_class
)
{
    size_t room_id;
    dg_status_t status;

    if (map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_clear_existing_room_door_tiles(map);
    dg_clear_room_entrance_metadata(map);

    if (generation_class != DG_MAP_GENERATION_CLASS_ROOM_LIKE ||
        map->metadata.room_count == 0 ||
        map->metadata.rooms == NULL) {
        return DG_STATUS_OK;
    }

    for (room_id = 0; room_id < map->metadata.room_count; ++room_id) {
        const dg_rect_t *room = &map->metadata.rooms[room_id].bounds;
        size_t room_area;
        unsigned char *candidate_mask;
        signed char *candidate_normal_x;
        signed char *candidate_normal_y;
        int *queue;
        int local_x;
        int local_y;
        int center_x;
        int center_y;

        if (room->width <= 0 || room->height <= 0) {
            continue;
        }
        if ((size_t)room->width > (SIZE_MAX / (size_t)room->height)) {
            return DG_STATUS_ALLOCATION_FAILED;
        }
        room_area = (size_t)room->width * (size_t)room->height;
        if (room_area == 0u) {
            continue;
        }

        candidate_mask = (unsigned char *)calloc(room_area, sizeof(unsigned char));
        candidate_normal_x = (signed char *)calloc(room_area, sizeof(signed char));
        candidate_normal_y = (signed char *)calloc(room_area, sizeof(signed char));
        queue = (int *)malloc(room_area * sizeof(int));
        if (candidate_mask == NULL || candidate_normal_x == NULL || candidate_normal_y == NULL || queue == NULL) {
            free(candidate_mask);
            free(candidate_normal_x);
            free(candidate_normal_y);
            free(queue);
            return DG_STATUS_ALLOCATION_FAILED;
        }

        for (local_y = 0; local_y < room->height; ++local_y) {
            for (local_x = 0; local_x < room->width; ++local_x) {
                int x = room->x + local_x;
                int y = room->y + local_y;
                int normal_x;
                int normal_y;
                size_t index;

                if (!dg_find_room_entrance_direction(map, room, x, y, &normal_x, &normal_y)) {
                    continue;
                }

                index = (size_t)local_y * (size_t)room->width + (size_t)local_x;
                candidate_mask[index] = 1u;
                candidate_normal_x[index] = (signed char)normal_x;
                candidate_normal_y[index] = (signed char)normal_y;
            }
        }

        center_x = room->x + room->width / 2;
        center_y = room->y + room->height / 2;
        for (local_y = 0; local_y < room->height; ++local_y) {
            for (local_x = 0; local_x < room->width; ++local_x) {
                size_t seed_index = (size_t)local_y * (size_t)room->width + (size_t)local_x;

                if (candidate_mask[seed_index] == 0u) {
                    continue;
                }

                {
                    int head;
                    int tail;
                    int best_x;
                    int best_y;
                    int best_metric;
                    int best_normal_x;
                    int best_normal_y;

                    candidate_mask[seed_index] = 0u;
                    head = 0;
                    tail = 0;
                    queue[tail++] = (int)seed_index;
                    best_x = room->x + local_x;
                    best_y = room->y + local_y;
                    best_metric = abs(best_x - center_x) + abs(best_y - center_y);
                    best_normal_x = candidate_normal_x[seed_index];
                    best_normal_y = candidate_normal_y[seed_index];

                    while (head < tail) {
                        static const int k_dirs[4][2] = {
                            {0, -1},
                            {1, 0},
                            {0, 1},
                            {-1, 0}
                        };
                        int cell = queue[head++];
                        int cx = cell % room->width;
                        int cy = cell / room->width;
                        int gx = room->x + cx;
                        int gy = room->y + cy;
                        int metric = abs(gx - center_x) + abs(gy - center_y);
                        int d;

                        if (metric < best_metric ||
                            (metric == best_metric &&
                             (gy < best_y || (gy == best_y && gx < best_x)))) {
                            best_x = gx;
                            best_y = gy;
                            best_metric = metric;
                            best_normal_x = candidate_normal_x[(size_t)cell];
                            best_normal_y = candidate_normal_y[(size_t)cell];
                        }

                        for (d = 0; d < 4; ++d) {
                            int nx = cx + k_dirs[d][0];
                            int ny = cy + k_dirs[d][1];
                            size_t nindex;

                            if (nx < 0 || ny < 0 || nx >= room->width || ny >= room->height) {
                                continue;
                            }

                            nindex = (size_t)ny * (size_t)room->width + (size_t)nx;
                            if (candidate_mask[nindex] == 0u) {
                                continue;
                            }

                            candidate_mask[nindex] = 0u;
                            queue[tail++] = (int)nindex;
                        }
                    }

                    status = dg_append_room_entrance(
                        map,
                        (int)room_id,
                        best_x,
                        best_y,
                        best_x + best_normal_x,
                        best_y + best_normal_y,
                        best_normal_x,
                        best_normal_y
                    );
                    if (status != DG_STATUS_OK) {
                        free(candidate_mask);
                        free(candidate_normal_x);
                        free(candidate_normal_y);
                        free(queue);
                        return status;
                    }

                    {
                        int node_index;
                        for (node_index = 0; node_index < tail; ++node_index) {
                            int cell = queue[node_index];
                            int cx = cell % room->width;
                            int cy = cell / room->width;
                            int gx = room->x + cx;
                            int gy = room->y + cy;

                            if (!dg_map_in_bounds(map, gx, gy)) {
                                continue;
                            }
                            if (!dg_is_walkable_tile(dg_map_get_tile(map, gx, gy))) {
                                continue;
                            }
                            map->tiles[dg_tile_index(map, gx, gy)] = DG_TILE_DOOR;
                        }
                    }
                }
            }
        }

        free(candidate_mask);
        free(candidate_normal_x);
        free(candidate_normal_y);
        free(queue);
    }

    return DG_STATUS_OK;
}

static void dg_edge_side_normal(dg_map_edge_side_t side, int *out_normal_x, int *out_normal_y)
{
    if (out_normal_x == NULL || out_normal_y == NULL) {
        return;
    }

    switch (side) {
    case DG_MAP_EDGE_TOP:
        *out_normal_x = 0;
        *out_normal_y = 1;
        break;
    case DG_MAP_EDGE_RIGHT:
        *out_normal_x = -1;
        *out_normal_y = 0;
        break;
    case DG_MAP_EDGE_BOTTOM:
        *out_normal_x = 0;
        *out_normal_y = -1;
        break;
    case DG_MAP_EDGE_LEFT:
    default:
        *out_normal_x = 1;
        *out_normal_y = 0;
        break;
    }
}

static void dg_edge_coord_to_point(
    const dg_map_t *map,
    dg_map_edge_side_t side,
    int edge_coord,
    int *out_x,
    int *out_y
)
{
    if (map == NULL || out_x == NULL || out_y == NULL) {
        return;
    }

    switch (side) {
    case DG_MAP_EDGE_TOP:
        *out_x = edge_coord;
        *out_y = 0;
        break;
    case DG_MAP_EDGE_RIGHT:
        *out_x = map->width - 1;
        *out_y = edge_coord;
        break;
    case DG_MAP_EDGE_BOTTOM:
        *out_x = edge_coord;
        *out_y = map->height - 1;
        break;
    case DG_MAP_EDGE_LEFT:
    default:
        *out_x = 0;
        *out_y = edge_coord;
        break;
    }
}

static dg_status_t dg_label_walkable_components(
    const dg_map_t *map,
    size_t **out_component_by_tile,
    size_t *out_component_count
)
{
    size_t cell_count;
    size_t i;
    size_t component_count;
    size_t *component_by_tile;
    size_t *queue;

    if (out_component_by_tile == NULL || out_component_count == NULL || map == NULL || map->tiles == NULL ||
        map->width <= 0 || map->height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_component_by_tile = NULL;
    *out_component_count = 0u;
    if ((size_t)map->width > (SIZE_MAX / (size_t)map->height)) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    cell_count = (size_t)map->width * (size_t)map->height;
    if (cell_count == 0u) {
        return DG_STATUS_OK;
    }

    component_by_tile = (size_t *)malloc(cell_count * sizeof(*component_by_tile));
    queue = (size_t *)malloc(cell_count * sizeof(*queue));
    if (component_by_tile == NULL || queue == NULL) {
        free(component_by_tile);
        free(queue);
        return DG_STATUS_ALLOCATION_FAILED;
    }
    for (i = 0; i < cell_count; ++i) {
        component_by_tile[i] = DG_MAP_EDGE_COMPONENT_UNKNOWN;
    }

    component_count = 0u;
    for (i = 0; i < cell_count; ++i) {
        size_t head;
        size_t tail;

        if (component_by_tile[i] != DG_MAP_EDGE_COMPONENT_UNKNOWN) {
            continue;
        }
        if (!dg_is_walkable_tile(map->tiles[i])) {
            continue;
        }

        component_by_tile[i] = component_count;
        queue[0] = i;
        head = 0u;
        tail = 1u;
        while (head < tail) {
            static const int k_dirs[4][2] = {
                {0, -1},
                {1, 0},
                {0, 1},
                {-1, 0}
            };
            size_t tile_index = queue[head++];
            int tx = (int)(tile_index % (size_t)map->width);
            int ty = (int)(tile_index / (size_t)map->width);
            int d;

            for (d = 0; d < 4; ++d) {
                int nx = tx + k_dirs[d][0];
                int ny = ty + k_dirs[d][1];
                size_t nindex;
                if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) {
                    continue;
                }

                nindex = dg_tile_index(map, nx, ny);
                if (component_by_tile[nindex] != DG_MAP_EDGE_COMPONENT_UNKNOWN) {
                    continue;
                }
                if (!dg_is_walkable_tile(map->tiles[nindex])) {
                    continue;
                }

                component_by_tile[nindex] = component_count;
                queue[tail++] = nindex;
            }
        }

        component_count += 1u;
    }

    free(queue);
    *out_component_by_tile = component_by_tile;
    *out_component_count = component_count;
    return DG_STATUS_OK;
}

static dg_status_t dg_scan_edge_openings_for_side(
    dg_map_t *map,
    dg_map_edge_side_t side,
    const size_t *component_by_tile,
    int coord_begin,
    int coord_end
)
{
    int coord;
    int run_start;
    int normal_x;
    int normal_y;

    if (map == NULL || map->tiles == NULL || component_by_tile == NULL || coord_end < coord_begin) {
        return DG_STATUS_OK;
    }

    dg_edge_side_normal(side, &normal_x, &normal_y);
    run_start = -1;
    for (coord = coord_begin; coord <= coord_end; ++coord) {
        int edge_x;
        int edge_y;
        bool walkable;
        bool should_flush;
        int run_end;
        int mid;
        int mid_x;
        int mid_y;
        size_t component_id;
        dg_status_t status;

        dg_edge_coord_to_point(map, side, coord, &edge_x, &edge_y);
        walkable = dg_map_in_bounds(map, edge_x, edge_y) &&
                   dg_is_walkable_tile(dg_map_get_tile(map, edge_x, edge_y));

        if (walkable && run_start < 0) {
            run_start = coord;
        }

        should_flush = (!walkable && run_start >= 0) || (walkable && run_start >= 0 && coord == coord_end);
        if (!should_flush) {
            continue;
        }

        run_end = walkable ? coord : (coord - 1);
        mid = run_start + ((run_end - run_start) / 2);
        dg_edge_coord_to_point(map, side, mid, &mid_x, &mid_y);

        component_id = DG_MAP_EDGE_COMPONENT_UNKNOWN;
        if (dg_map_in_bounds(map, mid_x + normal_x, mid_y + normal_y)) {
            size_t inward_index = dg_tile_index(map, mid_x + normal_x, mid_y + normal_y);
            if (dg_is_walkable_tile(map->tiles[inward_index])) {
                component_id = component_by_tile[inward_index];
            }
        }
        if (component_id == DG_MAP_EDGE_COMPONENT_UNKNOWN && dg_map_in_bounds(map, mid_x, mid_y)) {
            size_t edge_index = dg_tile_index(map, mid_x, mid_y);
            if (dg_is_walkable_tile(map->tiles[edge_index])) {
                component_id = component_by_tile[edge_index];
            }
        }

        status = dg_append_map_edge_opening(
            map,
            side,
            run_start,
            run_end,
            mid_x,
            mid_y,
            normal_x,
            normal_y,
            component_id
        );
        if (status != DG_STATUS_OK) {
            return status;
        }

        run_start = -1;
    }

    return DG_STATUS_OK;
}

static long long dg_edge_opening_distance(
    const dg_map_edge_opening_t *a,
    const dg_map_edge_opening_t *b
)
{
    long long dx;
    long long dy;

    if (a == NULL || b == NULL) {
        return -1;
    }

    dx = (long long)a->edge_tile.x - (long long)b->edge_tile.x;
    if (dx < 0) {
        dx = -dx;
    }
    dy = (long long)a->edge_tile.y - (long long)b->edge_tile.y;
    if (dy < 0) {
        dy = -dy;
    }

    return dx + dy;
}

static void dg_assign_primary_edge_openings(dg_map_t *map)
{
    size_t i;
    size_t j;
    size_t best_a;
    size_t best_b;
    long long best_distance;

    if (map == NULL || map->metadata.edge_openings == NULL) {
        return;
    }

    map->metadata.primary_entrance_opening_id = -1;
    map->metadata.primary_exit_opening_id = -1;
    for (i = 0; i < map->metadata.edge_opening_count; ++i) {
        map->metadata.edge_openings[i].role = DG_MAP_EDGE_OPENING_ROLE_NONE;
    }

    if (map->metadata.edge_opening_count == 0u) {
        return;
    }
    if (map->metadata.edge_opening_count == 1u) {
        map->metadata.edge_openings[0].role = DG_MAP_EDGE_OPENING_ROLE_ENTRANCE;
        map->metadata.primary_entrance_opening_id = map->metadata.edge_openings[0].id;
        return;
    }

    best_a = SIZE_MAX;
    best_b = SIZE_MAX;
    best_distance = -1;
    for (i = 0; i < map->metadata.edge_opening_count; ++i) {
        for (j = i + 1u; j < map->metadata.edge_opening_count; ++j) {
            long long distance;
            const dg_map_edge_opening_t *oa = &map->metadata.edge_openings[i];
            const dg_map_edge_opening_t *ob = &map->metadata.edge_openings[j];

            if (oa->component_id == DG_MAP_EDGE_COMPONENT_UNKNOWN ||
                ob->component_id == DG_MAP_EDGE_COMPONENT_UNKNOWN || oa->component_id != ob->component_id) {
                continue;
            }

            distance = dg_edge_opening_distance(oa, ob);
            if (distance > best_distance) {
                best_distance = distance;
                best_a = i;
                best_b = j;
            }
        }
    }

    if (best_a == SIZE_MAX || best_b == SIZE_MAX) {
        for (i = 0; i < map->metadata.edge_opening_count; ++i) {
            for (j = i + 1u; j < map->metadata.edge_opening_count; ++j) {
                long long distance = dg_edge_opening_distance(
                    &map->metadata.edge_openings[i],
                    &map->metadata.edge_openings[j]
                );
                if (distance > best_distance) {
                    best_distance = distance;
                    best_a = i;
                    best_b = j;
                }
            }
        }
    }

    if (best_a != SIZE_MAX && best_b != SIZE_MAX) {
        map->metadata.edge_openings[best_a].role = DG_MAP_EDGE_OPENING_ROLE_ENTRANCE;
        map->metadata.edge_openings[best_b].role = DG_MAP_EDGE_OPENING_ROLE_EXIT;
        map->metadata.primary_entrance_opening_id = map->metadata.edge_openings[best_a].id;
        map->metadata.primary_exit_opening_id = map->metadata.edge_openings[best_b].id;
    }
}

static dg_status_t dg_build_map_edge_opening_metadata(dg_map_t *map)
{
    size_t *component_by_tile;
    size_t component_count;
    dg_status_t status;

    if (map == NULL || map->tiles == NULL || map->width <= 0 || map->height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_clear_map_edge_opening_metadata(map);

    component_by_tile = NULL;
    component_count = 0u;
    status = dg_label_walkable_components(map, &component_by_tile, &component_count);
    if (status != DG_STATUS_OK) {
        free(component_by_tile);
        return status;
    }

    status = dg_scan_edge_openings_for_side(map, DG_MAP_EDGE_TOP, component_by_tile, 0, map->width - 1);
    if (status != DG_STATUS_OK) {
        free(component_by_tile);
        return status;
    }
    if (map->height > 1) {
        status = dg_scan_edge_openings_for_side(
            map,
            DG_MAP_EDGE_BOTTOM,
            component_by_tile,
            0,
            map->width - 1
        );
        if (status != DG_STATUS_OK) {
            free(component_by_tile);
            return status;
        }
    }
    if (map->height > 2 && map->width > 1) {
        status = dg_scan_edge_openings_for_side(
            map,
            DG_MAP_EDGE_LEFT,
            component_by_tile,
            1,
            map->height - 2
        );
        if (status != DG_STATUS_OK) {
            free(component_by_tile);
            return status;
        }
        status = dg_scan_edge_openings_for_side(
            map,
            DG_MAP_EDGE_RIGHT,
            component_by_tile,
            1,
            map->height - 2
        );
        if (status != DG_STATUS_OK) {
            free(component_by_tile);
            return status;
        }
    }

    (void)component_count;
    dg_assign_primary_edge_openings(map);
    free(component_by_tile);
    return DG_STATUS_OK;
}

static bool dg_corridor_endpoints_valid(const dg_map_t *map, const dg_corridor_metadata_t *corridor)
{
    if (map == NULL || corridor == NULL) {
        return false;
    }

    if (corridor->from_room_id < 0 || corridor->to_room_id < 0) {
        return false;
    }

    if ((size_t)corridor->from_room_id >= map->metadata.room_count) {
        return false;
    }

    if ((size_t)corridor->to_room_id >= map->metadata.room_count) {
        return false;
    }

    if (corridor->from_room_id == corridor->to_room_id) {
        return false;
    }

    return true;
}

static dg_status_t dg_build_room_graph_metadata(
    dg_map_t *map,
    size_t *out_leaf_room_count,
    size_t *out_corridor_total_length
)
{
    size_t i;
    size_t room_count;
    size_t valid_corridor_count;
    size_t neighbor_count;
    size_t running_index;
    size_t leaf_room_count;
    size_t corridor_total_length;
    int *degrees;
    size_t *write_cursor;
    dg_room_adjacency_span_t *room_adjacency;
    dg_room_neighbor_t *room_neighbors;

    if (map == NULL || out_leaf_room_count == NULL || out_corridor_total_length == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    room_count = map->metadata.room_count;
    *out_leaf_room_count = 0;
    *out_corridor_total_length = 0;

    free(map->metadata.room_adjacency);
    free(map->metadata.room_neighbors);
    map->metadata.room_adjacency = NULL;
    map->metadata.room_adjacency_count = 0;
    map->metadata.room_neighbors = NULL;
    map->metadata.room_neighbor_count = 0;

    if (room_count == 0) {
        return DG_STATUS_OK;
    }

    degrees = (int *)calloc(room_count, sizeof(int));
    if (degrees == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    corridor_total_length = 0;
    valid_corridor_count = 0;
    for (i = 0; i < map->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];
        if (corridor->length > 0) {
            corridor_total_length += (size_t)corridor->length;
        }

        if (!dg_corridor_endpoints_valid(map, corridor)) {
            continue;
        }

        degrees[corridor->from_room_id] += 1;
        degrees[corridor->to_room_id] += 1;
        valid_corridor_count += 1;
    }

    leaf_room_count = 0;
    for (i = 0; i < room_count; ++i) {
        if (degrees[i] == 1) {
            leaf_room_count += 1;
        }
    }

    room_adjacency = (dg_room_adjacency_span_t *)calloc(room_count, sizeof(dg_room_adjacency_span_t));
    if (room_adjacency == NULL) {
        free(degrees);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    neighbor_count = valid_corridor_count * 2;
    room_neighbors = NULL;
    if (neighbor_count > 0) {
        room_neighbors = (dg_room_neighbor_t *)malloc(neighbor_count * sizeof(dg_room_neighbor_t));
        if (room_neighbors == NULL) {
            free(room_adjacency);
            free(degrees);
            return DG_STATUS_ALLOCATION_FAILED;
        }
    }

    running_index = 0;
    for (i = 0; i < room_count; ++i) {
        room_adjacency[i].start_index = running_index;
        room_adjacency[i].count = (size_t)degrees[i];
        running_index += (size_t)degrees[i];
    }

    if (running_index != neighbor_count) {
        free(room_neighbors);
        free(room_adjacency);
        free(degrees);
        return DG_STATUS_GENERATION_FAILED;
    }

    write_cursor = (size_t *)malloc(room_count * sizeof(size_t));
    if (write_cursor == NULL) {
        free(room_neighbors);
        free(room_adjacency);
        free(degrees);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (i = 0; i < room_count; ++i) {
        write_cursor[i] = room_adjacency[i].start_index;
    }

    for (i = 0; i < map->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];
        size_t from_pos;
        size_t to_pos;

        if (!dg_corridor_endpoints_valid(map, corridor)) {
            continue;
        }

        from_pos = write_cursor[corridor->from_room_id]++;
        to_pos = write_cursor[corridor->to_room_id]++;

        room_neighbors[from_pos].room_id = corridor->to_room_id;
        room_neighbors[from_pos].corridor_index = (int)i;
        room_neighbors[to_pos].room_id = corridor->from_room_id;
        room_neighbors[to_pos].corridor_index = (int)i;
    }

    free(write_cursor);
    free(degrees);

    map->metadata.room_adjacency = room_adjacency;
    map->metadata.room_adjacency_count = room_count;
    map->metadata.room_neighbors = room_neighbors;
    map->metadata.room_neighbor_count = neighbor_count;

    *out_leaf_room_count = leaf_room_count;
    *out_corridor_total_length = corridor_total_length;
    return DG_STATUS_OK;
}

static void dg_clear_room_graph_metadata(dg_map_t *map)
{
    if (map == NULL) {
        return;
    }

    free(map->metadata.room_adjacency);
    free(map->metadata.room_neighbors);
    map->metadata.room_adjacency = NULL;
    map->metadata.room_adjacency_count = 0;
    map->metadata.room_neighbors = NULL;
    map->metadata.room_neighbor_count = 0;
}

static bool dg_edge_side_is_valid(dg_map_edge_side_t side)
{
    return side == DG_MAP_EDGE_TOP ||
           side == DG_MAP_EDGE_RIGHT ||
           side == DG_MAP_EDGE_BOTTOM ||
           side == DG_MAP_EDGE_LEFT;
}

static bool dg_edge_opening_role_is_valid(dg_map_edge_opening_role_t role)
{
    return role == DG_MAP_EDGE_OPENING_ROLE_NONE ||
           role == DG_MAP_EDGE_OPENING_ROLE_ENTRANCE ||
           role == DG_MAP_EDGE_OPENING_ROLE_EXIT;
}

static bool dg_explicit_edge_opening_spec_is_in_bounds(
    const dg_map_t *map,
    const dg_edge_opening_spec_t *opening
)
{
    int max_coord;

    if (map == NULL || opening == NULL || map->tiles == NULL || map->width <= 0 || map->height <= 0) {
        return false;
    }

    if (!dg_edge_side_is_valid(opening->side) || !dg_edge_opening_role_is_valid(opening->role)) {
        return false;
    }

    if (opening->start < 0 || opening->end < opening->start) {
        return false;
    }

    switch (opening->side) {
    case DG_MAP_EDGE_TOP:
    case DG_MAP_EDGE_BOTTOM:
        max_coord = map->width - 1;
        break;
    case DG_MAP_EDGE_LEFT:
    case DG_MAP_EDGE_RIGHT:
        max_coord = map->height - 1;
        break;
    default:
        return false;
    }

    if (max_coord < 0 || opening->start > max_coord || opening->end > max_coord) {
        return false;
    }

    return true;
}

dg_status_t dg_apply_explicit_edge_openings(
    const dg_generate_request_t *request,
    dg_map_t *map
)
{
    size_t i;

    if (request == NULL || map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (request->edge_openings.opening_count == 0u) {
        return DG_STATUS_OK;
    }
    if (request->edge_openings.openings == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (i = 0; i < request->edge_openings.opening_count; ++i) {
        const dg_edge_opening_spec_t *opening = &request->edge_openings.openings[i];
        int normal_x;
        int normal_y;
        int coord;

        if (!dg_explicit_edge_opening_spec_is_in_bounds(map, opening)) {
            return DG_STATUS_INVALID_ARGUMENT;
        }

        dg_edge_side_normal(opening->side, &normal_x, &normal_y);
        for (coord = opening->start; coord <= opening->end; ++coord) {
            int edge_x;
            int edge_y;
            int inward_x;
            int inward_y;

            dg_edge_coord_to_point(map, opening->side, coord, &edge_x, &edge_y);
            if (!dg_map_in_bounds(map, edge_x, edge_y)) {
                return DG_STATUS_INVALID_ARGUMENT;
            }

            map->tiles[dg_tile_index(map, edge_x, edge_y)] = DG_TILE_FLOOR;
            inward_x = edge_x + normal_x;
            inward_y = edge_y + normal_y;
            if (dg_map_in_bounds(map, inward_x, inward_y)) {
                map->tiles[dg_tile_index(map, inward_x, inward_y)] = DG_TILE_FLOOR;
            }
        }
    }

    return DG_STATUS_OK;
}

static size_t dg_find_best_matching_edge_opening_index(
    const dg_map_t *map,
    const dg_edge_opening_spec_t *spec
)
{
    size_t i;
    size_t best_index;
    int best_overlap;
    int best_center_delta;

    if (map == NULL || spec == NULL || map->metadata.edge_openings == NULL) {
        return SIZE_MAX;
    }

    best_index = SIZE_MAX;
    best_overlap = -1;
    best_center_delta = INT_MAX;

    for (i = 0; i < map->metadata.edge_opening_count; ++i) {
        const dg_map_edge_opening_t *opening = &map->metadata.edge_openings[i];
        int overlap_start;
        int overlap_end;
        int overlap;
        int opening_center;
        int spec_center;
        int center_delta;

        if (opening->side != spec->side) {
            continue;
        }

        overlap_start = dg_max_int(opening->start, spec->start);
        overlap_end = dg_min_int(opening->end, spec->end);
        if (overlap_end < overlap_start) {
            continue;
        }

        overlap = overlap_end - overlap_start + 1;
        opening_center = opening->start + ((opening->end - opening->start) / 2);
        spec_center = spec->start + ((spec->end - spec->start) / 2);
        center_delta = abs(opening_center - spec_center);

        if (overlap > best_overlap ||
            (overlap == best_overlap && center_delta < best_center_delta)) {
            best_overlap = overlap;
            best_center_delta = center_delta;
            best_index = i;
        }
    }

    if (best_index == SIZE_MAX) {
        int center_coord;
        int edge_x;
        int edge_y;

        center_coord = spec->start + ((spec->end - spec->start) / 2);
        dg_edge_coord_to_point(map, spec->side, center_coord, &edge_x, &edge_y);

        for (i = 0; i < map->metadata.edge_opening_count; ++i) {
            const dg_map_edge_opening_t *opening = &map->metadata.edge_openings[i];
            if (opening->edge_tile.x == edge_x && opening->edge_tile.y == edge_y) {
                return i;
            }
        }
    }

    return best_index;
}

dg_status_t dg_apply_explicit_edge_opening_roles(
    const dg_generate_request_t *request,
    dg_map_t *map
)
{
    size_t i;
    bool has_explicit_roles;

    if (request == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (request->edge_openings.opening_count == 0u ||
        request->edge_openings.openings == NULL ||
        map->metadata.edge_opening_count == 0u ||
        map->metadata.edge_openings == NULL) {
        return DG_STATUS_OK;
    }

    has_explicit_roles = false;
    for (i = 0; i < request->edge_openings.opening_count; ++i) {
        const dg_edge_opening_spec_t *opening = &request->edge_openings.openings[i];
        if (!dg_edge_opening_role_is_valid(opening->role)) {
            return DG_STATUS_INVALID_ARGUMENT;
        }
        if (opening->role != DG_MAP_EDGE_OPENING_ROLE_NONE) {
            has_explicit_roles = true;
        }
    }

    if (!has_explicit_roles) {
        return DG_STATUS_OK;
    }

    map->metadata.primary_entrance_opening_id = -1;
    map->metadata.primary_exit_opening_id = -1;
    for (i = 0; i < map->metadata.edge_opening_count; ++i) {
        map->metadata.edge_openings[i].role = DG_MAP_EDGE_OPENING_ROLE_NONE;
    }

    for (i = 0; i < request->edge_openings.opening_count; ++i) {
        const dg_edge_opening_spec_t *spec = &request->edge_openings.openings[i];
        size_t opening_index;
        dg_map_edge_opening_t *opening;

        if (spec->role == DG_MAP_EDGE_OPENING_ROLE_NONE) {
            continue;
        }

        opening_index = dg_find_best_matching_edge_opening_index(map, spec);
        if (opening_index == SIZE_MAX || opening_index >= map->metadata.edge_opening_count) {
            continue;
        }

        opening = &map->metadata.edge_openings[opening_index];
        opening->role = spec->role;
        if (spec->role == DG_MAP_EDGE_OPENING_ROLE_ENTRANCE &&
            map->metadata.primary_entrance_opening_id < 0) {
            map->metadata.primary_entrance_opening_id = opening->id;
        } else if (spec->role == DG_MAP_EDGE_OPENING_ROLE_EXIT &&
                   map->metadata.primary_exit_opening_id < 0) {
            map->metadata.primary_exit_opening_id = opening->id;
        }
    }

    return DG_STATUS_OK;
}

dg_status_t dg_populate_runtime_metadata(
    dg_map_t *map,
    uint64_t seed,
    int algorithm_id,
    dg_map_generation_class_t generation_class,
    size_t generation_attempts,
    bool reset_room_assignments
)
{
    size_t i;
    size_t cell_count;
    size_t walkable_tile_count;
    size_t wall_tile_count;
    size_t special_room_count;
    size_t leaf_room_count;
    size_t corridor_total_length;
    dg_connectivity_stats_t connectivity;
    dg_status_t status;

    if (map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_build_room_entrance_metadata(map, generation_class);
    if (status != DG_STATUS_OK) {
        return status;
    }
    status = dg_build_map_edge_opening_metadata(map);
    if (status != DG_STATUS_OK) {
        return status;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    walkable_tile_count = 0;
    wall_tile_count = 0;
    for (i = 0; i < cell_count; ++i) {
        if (dg_is_walkable_tile(map->tiles[i])) {
            walkable_tile_count += 1;
        }
        if (map->tiles[i] == DG_TILE_WALL) {
            wall_tile_count += 1;
        }
    }

    special_room_count = 0;
    for (i = 0; i < map->metadata.room_count; ++i) {
        if (reset_room_assignments) {
            map->metadata.rooms[i].role = DG_ROOM_ROLE_NONE;
            map->metadata.rooms[i].type_id = DG_ROOM_TYPE_UNASSIGNED;
        }
        if ((map->metadata.rooms[i].flags & DG_ROOM_FLAG_SPECIAL) != 0u) {
            special_room_count += 1;
        }
    }

    leaf_room_count = 0;
    corridor_total_length = 0;
    if (generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE) {
        status = dg_build_room_graph_metadata(map, &leaf_room_count, &corridor_total_length);
        if (status != DG_STATUS_OK) {
            return status;
        }
    } else {
        dg_clear_room_graph_metadata(map);
    }

    status = dg_analyze_connectivity(map, &connectivity);
    if (status != DG_STATUS_OK) {
        return status;
    }

    map->metadata.seed = seed;
    map->metadata.algorithm_id = algorithm_id;
    map->metadata.generation_class = generation_class;
    map->metadata.walkable_tile_count = walkable_tile_count;
    map->metadata.wall_tile_count = wall_tile_count;
    map->metadata.special_room_count = special_room_count;
    map->metadata.entrance_room_count = 0;
    map->metadata.exit_room_count = 0;
    map->metadata.boss_room_count = 0;
    map->metadata.treasure_room_count = 0;
    map->metadata.shop_room_count = 0;
    map->metadata.leaf_room_count = leaf_room_count;
    map->metadata.corridor_total_length = corridor_total_length;
    map->metadata.entrance_exit_distance = -1;
    map->metadata.connected_component_count = connectivity.component_count;
    map->metadata.largest_component_size = connectivity.largest_component_size;
    map->metadata.connected_floor = connectivity.connected_floor;
    map->metadata.generation_attempts = generation_attempts;

    return DG_STATUS_OK;
}

void dg_init_empty_map(dg_map_t *map)
{
    if (map == NULL) {
        return;
    }

    map->width = 0;
    map->height = 0;
    map->tiles = NULL;
    map->metadata.rooms = NULL;
    map->metadata.room_count = 0;
    map->metadata.room_capacity = 0;
    map->metadata.corridors = NULL;
    map->metadata.corridor_count = 0;
    map->metadata.corridor_capacity = 0;
    map->metadata.room_entrances = NULL;
    map->metadata.room_entrance_count = 0;
    map->metadata.room_entrance_capacity = 0;
    map->metadata.edge_openings = NULL;
    map->metadata.edge_opening_count = 0;
    map->metadata.edge_opening_capacity = 0;
    map->metadata.primary_entrance_opening_id = -1;
    map->metadata.primary_exit_opening_id = -1;
    map->metadata.room_adjacency = NULL;
    map->metadata.room_adjacency_count = 0;
    map->metadata.room_neighbors = NULL;
    map->metadata.room_neighbor_count = 0;
    map->metadata.seed = 0;
    map->metadata.algorithm_id = -1;
    map->metadata.generation_class = DG_MAP_GENERATION_CLASS_UNKNOWN;
    map->metadata.walkable_tile_count = 0;
    map->metadata.wall_tile_count = 0;
    map->metadata.special_room_count = 0;
    map->metadata.entrance_room_count = 0;
    map->metadata.exit_room_count = 0;
    map->metadata.boss_room_count = 0;
    map->metadata.treasure_room_count = 0;
    map->metadata.shop_room_count = 0;
    map->metadata.leaf_room_count = 0;
    map->metadata.corridor_total_length = 0;
    map->metadata.entrance_exit_distance = -1;
    map->metadata.connected_component_count = 0;
    map->metadata.largest_component_size = 0;
    map->metadata.connected_floor = false;
    map->metadata.generation_attempts = 0;
    map->metadata.diagnostics = (dg_generation_diagnostics_t){0};
    map->metadata.generation_request = (dg_generation_request_snapshot_t){0};
}
