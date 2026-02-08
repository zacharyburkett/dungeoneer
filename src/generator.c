#include "dungeoneer/generator.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct dg_connectivity_stats {
    size_t walkable_count;
    size_t component_count;
    size_t largest_component_size;
    bool connected_floor;
} dg_connectivity_stats_t;

static int dg_min_int(int a, int b)
{
    return a < b ? a : b;
}

static int dg_max_int(int a, int b)
{
    return a > b ? a : b;
}

static int dg_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static bool dg_is_walkable_tile(dg_tile_t tile)
{
    return tile == DG_TILE_FLOOR || tile == DG_TILE_DOOR;
}

static size_t dg_tile_index(const dg_map_t *map, int x, int y)
{
    return ((size_t)y * (size_t)map->width) + (size_t)x;
}

static bool dg_rect_is_valid(const dg_rect_t *rect)
{
    return rect != NULL && rect->width > 0 && rect->height > 0;
}

static bool dg_rects_overlap(const dg_rect_t *a, const dg_rect_t *b)
{
    long long a_left;
    long long a_top;
    long long a_right;
    long long a_bottom;
    long long b_left;
    long long b_top;
    long long b_right;
    long long b_bottom;

    a_left = (long long)a->x;
    a_top = (long long)a->y;
    a_right = (long long)a->x + (long long)a->width;
    a_bottom = (long long)a->y + (long long)a->height;
    b_left = (long long)b->x;
    b_top = (long long)b->y;
    b_right = (long long)b->x + (long long)b->width;
    b_bottom = (long long)b->y + (long long)b->height;

    if (a_right <= b_left || b_right <= a_left) {
        return false;
    }

    if (a_bottom <= b_top || b_bottom <= a_top) {
        return false;
    }

    return true;
}

static bool dg_rects_overlap_with_padding(const dg_rect_t *a, const dg_rect_t *b, int padding)
{
    dg_rect_t expanded;

    expanded.x = a->x - padding;
    expanded.y = a->y - padding;
    expanded.width = a->width + (padding * 2);
    expanded.height = a->height + (padding * 2);

    return dg_rects_overlap(&expanded, b);
}

static bool dg_clamp_region_to_map(
    const dg_map_t *map,
    const dg_rect_t *region,
    int *out_x0,
    int *out_y0,
    int *out_x1,
    int *out_y1
)
{
    long long x0_ll;
    long long y0_ll;
    long long x1_ll;
    long long y1_ll;
    int x0;
    int y0;
    int x1;
    int y1;

    if (map == NULL || map->tiles == NULL || !dg_rect_is_valid(region)) {
        return false;
    }

    x0_ll = (long long)region->x;
    y0_ll = (long long)region->y;
    x1_ll = (long long)region->x + (long long)region->width;
    y1_ll = (long long)region->y + (long long)region->height;

    if (x1_ll <= 0 || y1_ll <= 0) {
        return false;
    }

    if (x0_ll >= (long long)map->width || y0_ll >= (long long)map->height) {
        return false;
    }

    x0 = dg_max_int((int)x0_ll, 0);
    y0 = dg_max_int((int)y0_ll, 0);
    x1 = dg_min_int((int)x1_ll, map->width);
    y1 = dg_min_int((int)y1_ll, map->height);

    if (x0 >= x1 || y0 >= y1) {
        return false;
    }

    *out_x0 = x0;
    *out_y0 = y0;
    *out_x1 = x1;
    *out_y1 = y1;
    return true;
}

static bool dg_room_overlaps_forbidden_regions(
    const dg_generation_constraints_t *constraints,
    const dg_rect_t *room
)
{
    size_t i;

    if (constraints == NULL || constraints->forbidden_regions == NULL) {
        return false;
    }

    for (i = 0; i < constraints->forbidden_region_count; ++i) {
        const dg_rect_t *region = &constraints->forbidden_regions[i];
        if (!dg_rect_is_valid(region)) {
            continue;
        }
        if (dg_rects_overlap(room, region)) {
            return true;
        }
    }

    return false;
}

static void dg_paint_outer_walls(dg_map_t *map)
{
    int x;
    int y;

    if (map == NULL || map->tiles == NULL) {
        return;
    }

    for (x = 0; x < map->width; ++x) {
        (void)dg_map_set_tile(map, x, 0, DG_TILE_WALL);
        (void)dg_map_set_tile(map, x, map->height - 1, DG_TILE_WALL);
    }

    for (y = 0; y < map->height; ++y) {
        (void)dg_map_set_tile(map, 0, y, DG_TILE_WALL);
        (void)dg_map_set_tile(map, map->width - 1, y, DG_TILE_WALL);
    }
}

static bool dg_has_outer_walls(const dg_map_t *map)
{
    int x;
    int y;

    if (map == NULL || map->tiles == NULL) {
        return false;
    }

    for (x = 0; x < map->width; ++x) {
        if (dg_map_get_tile(map, x, 0) != DG_TILE_WALL) {
            return false;
        }
        if (dg_map_get_tile(map, x, map->height - 1) != DG_TILE_WALL) {
            return false;
        }
    }

    for (y = 0; y < map->height; ++y) {
        if (dg_map_get_tile(map, 0, y) != DG_TILE_WALL) {
            return false;
        }
        if (dg_map_get_tile(map, map->width - 1, y) != DG_TILE_WALL) {
            return false;
        }
    }

    return true;
}

static void dg_carve_brush(dg_map_t *map, int cx, int cy, int radius, dg_tile_t tile)
{
    int dx;
    int dy;
    int radius_sq;

    if (map == NULL || map->tiles == NULL) {
        return;
    }

    if (radius < 0) {
        radius = 0;
    }

    radius_sq = radius * radius;
    for (dy = -radius; dy <= radius; ++dy) {
        for (dx = -radius; dx <= radius; ++dx) {
            int nx = cx + dx;
            int ny = cy + dy;
            int distance_sq = (dx * dx) + (dy * dy);

            if (distance_sq > radius_sq) {
                continue;
            }

            if (!dg_map_in_bounds(map, nx, ny)) {
                continue;
            }

            (void)dg_map_set_tile(map, nx, ny, tile);
        }
    }
}

static void dg_carve_room(dg_map_t *map, const dg_rect_t *room)
{
    int x;
    int y;

    for (y = room->y; y < room->y + room->height; ++y) {
        for (x = room->x; x < room->x + room->width; ++x) {
            (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
        }
    }
}

static bool dg_can_place_room(
    const dg_map_t *map,
    const dg_rect_t *candidate,
    int spacing,
    const dg_generation_constraints_t *constraints
)
{
    size_t i;

    if (candidate->x < 1 || candidate->y < 1) {
        return false;
    }

    if ((long long)candidate->x + (long long)candidate->width > (long long)map->width - 1) {
        return false;
    }

    if ((long long)candidate->y + (long long)candidate->height > (long long)map->height - 1) {
        return false;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        if (dg_rects_overlap_with_padding(candidate, &map->metadata.rooms[i].bounds, spacing)) {
            return false;
        }
    }

    if (dg_room_overlaps_forbidden_regions(constraints, candidate)) {
        return false;
    }

    return true;
}

static dg_point_t dg_room_center(const dg_rect_t *room)
{
    dg_point_t center;
    center.x = room->x + room->width / 2;
    center.y = room->y + room->height / 2;
    return center;
}

static void dg_carve_horizontal_path(dg_map_t *map, int x0, int x1, int y, int corridor_width)
{
    int x;
    int start;
    int end;
    int radius;

    start = dg_min_int(x0, x1);
    end = dg_max_int(x0, x1);
    radius = corridor_width / 2;

    for (x = start; x <= end; ++x) {
        dg_carve_brush(map, x, y, radius, DG_TILE_FLOOR);
    }
}

static void dg_carve_vertical_path(dg_map_t *map, int x, int y0, int y1, int corridor_width)
{
    int y;
    int start;
    int end;
    int radius;

    start = dg_min_int(y0, y1);
    end = dg_max_int(y0, y1);
    radius = corridor_width / 2;

    for (y = start; y <= end; ++y) {
        dg_carve_brush(map, x, y, radius, DG_TILE_FLOOR);
    }
}

static void dg_connect_points(dg_map_t *map, dg_rng_t *rng, dg_point_t a, dg_point_t b, int corridor_width)
{
    bool horizontal_first;

    horizontal_first = (dg_rng_next_u32(rng) & 1u) != 0u;
    if (horizontal_first) {
        dg_carve_horizontal_path(map, a.x, b.x, a.y, corridor_width);
        dg_carve_vertical_path(map, b.x, a.y, b.y, corridor_width);
    } else {
        dg_carve_vertical_path(map, a.x, a.y, b.y, corridor_width);
        dg_carve_horizontal_path(map, a.x, b.x, b.y, corridor_width);
    }
}

static size_t dg_count_walkable_tiles(const dg_map_t *map)
{
    size_t i;
    size_t walkable_count;
    size_t cell_count;

    walkable_count = 0;
    cell_count = (size_t)map->width * (size_t)map->height;
    for (i = 0; i < cell_count; ++i) {
        if (dg_is_walkable_tile(map->tiles[i])) {
            walkable_count += 1;
        }
    }

    return walkable_count;
}

static dg_status_t dg_enforce_single_connected_region(dg_map_t *map)
{
    size_t cell_count;
    unsigned char *visited;
    size_t *queue;
    size_t head;
    size_t tail;
    size_t start_index;
    size_t i;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    if (map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    visited = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
    queue = (size_t *)malloc(cell_count * sizeof(size_t));
    if (visited == NULL || queue == NULL) {
        free(visited);
        free(queue);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    start_index = cell_count;
    for (i = 0; i < cell_count; ++i) {
        if (dg_is_walkable_tile(map->tiles[i])) {
            start_index = i;
            break;
        }
    }

    if (start_index == cell_count) {
        free(visited);
        free(queue);
        return DG_STATUS_OK;
    }

    head = 0;
    tail = 0;
    queue[tail++] = start_index;
    visited[start_index] = 1;

    while (head < tail) {
        size_t current = queue[head++];
        int x = (int)(current % (size_t)map->width);
        int y = (int)(current / (size_t)map->width);
        int d;

        for (d = 0; d < 4; ++d) {
            int nx = x + directions[d][0];
            int ny = y + directions[d][1];
            size_t neighbor_index;

            if (!dg_map_in_bounds(map, nx, ny)) {
                continue;
            }

            neighbor_index = dg_tile_index(map, nx, ny);
            if (visited[neighbor_index] != 0) {
                continue;
            }

            if (!dg_is_walkable_tile(map->tiles[neighbor_index])) {
                continue;
            }

            visited[neighbor_index] = 1;
            queue[tail++] = neighbor_index;
        }
    }

    for (i = 0; i < cell_count; ++i) {
        if (dg_is_walkable_tile(map->tiles[i]) && visited[i] == 0) {
            map->tiles[i] = DG_TILE_WALL;
        }
    }

    free(visited);
    free(queue);
    return DG_STATUS_OK;
}

static dg_status_t dg_analyze_connectivity(const dg_map_t *map, dg_connectivity_stats_t *out_stats)
{
    size_t cell_count;
    unsigned char *visited;
    size_t *queue;
    size_t i;
    size_t head;
    size_t tail;
    size_t walkable_count;
    size_t component_count;
    size_t largest_component_size;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    if (map == NULL || map->tiles == NULL || out_stats == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    visited = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
    queue = (size_t *)malloc(cell_count * sizeof(size_t));
    if (visited == NULL || queue == NULL) {
        free(visited);
        free(queue);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    walkable_count = 0;
    component_count = 0;
    largest_component_size = 0;

    for (i = 0; i < cell_count; ++i) {
        size_t component_size;

        if (visited[i] != 0 || !dg_is_walkable_tile(map->tiles[i])) {
            continue;
        }

        component_count += 1;
        component_size = 0;

        head = 0;
        tail = 0;
        visited[i] = 1;
        queue[tail++] = i;

        while (head < tail) {
            size_t current = queue[head++];
            int x = (int)(current % (size_t)map->width);
            int y = (int)(current / (size_t)map->width);
            int d;

            component_size += 1;

            for (d = 0; d < 4; ++d) {
                int nx = x + directions[d][0];
                int ny = y + directions[d][1];
                size_t neighbor_index;

                if (!dg_map_in_bounds(map, nx, ny)) {
                    continue;
                }

                neighbor_index = dg_tile_index(map, nx, ny);
                if (visited[neighbor_index] != 0 || !dg_is_walkable_tile(map->tiles[neighbor_index])) {
                    continue;
                }

                visited[neighbor_index] = 1;
                queue[tail++] = neighbor_index;
            }
        }

        walkable_count += component_size;
        if (component_size > largest_component_size) {
            largest_component_size = component_size;
        }
    }

    out_stats->walkable_count = walkable_count;
    out_stats->component_count = component_count;
    out_stats->largest_component_size = largest_component_size;
    out_stats->connected_floor = (walkable_count > 0 && component_count == 1);

    free(visited);
    free(queue);
    return DG_STATUS_OK;
}

static dg_status_t dg_smooth_walkable_regions(dg_map_t *map, int smoothing_passes)
{
    size_t cell_count;
    dg_tile_t *buffer;
    int pass;
    int x;
    int y;

    if (smoothing_passes <= 0) {
        return DG_STATUS_OK;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    buffer = (dg_tile_t *)malloc(cell_count * sizeof(dg_tile_t));
    if (buffer == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (pass = 0; pass < smoothing_passes; ++pass) {
        memcpy(buffer, map->tiles, cell_count * sizeof(dg_tile_t));

        for (y = 1; y < map->height - 1; ++y) {
            for (x = 1; x < map->width - 1; ++x) {
                int dx;
                int dy;
                int neighbors = 0;
                size_t index = dg_tile_index(map, x, y);

                for (dy = -1; dy <= 1; ++dy) {
                    for (dx = -1; dx <= 1; ++dx) {
                        int nx;
                        int ny;
                        size_t neighbor_index;

                        if (dx == 0 && dy == 0) {
                            continue;
                        }

                        nx = x + dx;
                        ny = y + dy;
                        neighbor_index = dg_tile_index(map, nx, ny);
                        if (dg_is_walkable_tile(map->tiles[neighbor_index])) {
                            neighbors += 1;
                        }
                    }
                }

                if (neighbors >= 5) {
                    buffer[index] = DG_TILE_FLOOR;
                } else if (neighbors <= 2) {
                    buffer[index] = DG_TILE_WALL;
                }
            }
        }

        memcpy(map->tiles, buffer, cell_count * sizeof(dg_tile_t));
    }

    free(buffer);
    return DG_STATUS_OK;
}

static void dg_apply_forbidden_regions(const dg_generation_constraints_t *constraints, dg_map_t *map)
{
    size_t i;

    if (constraints == NULL || map == NULL || map->tiles == NULL) {
        return;
    }

    for (i = 0; i < constraints->forbidden_region_count; ++i) {
        const dg_rect_t *region = &constraints->forbidden_regions[i];
        int x0;
        int y0;
        int x1;
        int y1;
        int x;
        int y;

        if (!dg_clamp_region_to_map(map, region, &x0, &y0, &x1, &y1)) {
            continue;
        }

        for (y = y0; y < y1; ++y) {
            for (x = x0; x < x1; ++x) {
                (void)dg_map_set_tile(map, x, y, DG_TILE_WALL);
            }
        }
    }
}

static bool dg_forbidden_regions_are_clear(const dg_map_t *map, const dg_generation_constraints_t *constraints)
{
    size_t i;

    if (constraints == NULL || map == NULL || map->tiles == NULL) {
        return false;
    }

    for (i = 0; i < constraints->forbidden_region_count; ++i) {
        const dg_rect_t *region = &constraints->forbidden_regions[i];
        int x0;
        int y0;
        int x1;
        int y1;
        int x;
        int y;

        if (!dg_clamp_region_to_map(map, region, &x0, &y0, &x1, &y1)) {
            continue;
        }

        for (y = y0; y < y1; ++y) {
            for (x = x0; x < x1; ++x) {
                if (dg_is_walkable_tile(dg_map_get_tile(map, x, y))) {
                    return false;
                }
            }
        }
    }

    return true;
}

static dg_status_t dg_populate_runtime_metadata(
    dg_map_t *map,
    uint64_t seed,
    int algorithm_id,
    size_t generation_attempts
)
{
    size_t i;
    size_t cell_count;
    size_t walkable_tile_count;
    size_t wall_tile_count;
    size_t special_room_count;
    dg_connectivity_stats_t connectivity;
    dg_status_t status;

    if (map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
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
        if ((map->metadata.rooms[i].flags & DG_ROOM_FLAG_SPECIAL) != 0u) {
            special_room_count += 1;
        }
    }

    status = dg_analyze_connectivity(map, &connectivity);
    if (status != DG_STATUS_OK) {
        return status;
    }

    map->metadata.seed = seed;
    map->metadata.algorithm_id = algorithm_id;
    map->metadata.walkable_tile_count = walkable_tile_count;
    map->metadata.wall_tile_count = wall_tile_count;
    map->metadata.special_room_count = special_room_count;
    map->metadata.connected_component_count = connectivity.component_count;
    map->metadata.largest_component_size = connectivity.largest_component_size;
    map->metadata.connected_floor = connectivity.connected_floor;
    map->metadata.generation_attempts = generation_attempts;

    return DG_STATUS_OK;
}

static dg_status_t dg_validate_constraints(const dg_generation_constraints_t *constraints)
{
    if (constraints == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->min_floor_coverage < 0.0f || constraints->min_floor_coverage > 1.0f) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->max_floor_coverage < 0.0f || constraints->max_floor_coverage > 1.0f) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->max_floor_coverage < constraints->min_floor_coverage) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->min_room_count < 0 || constraints->max_room_count < 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->min_special_rooms < 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (
        constraints->max_room_count > 0 &&
        constraints->min_room_count > 0 &&
        constraints->max_room_count < constraints->min_room_count
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->forbidden_region_count > 0 && constraints->forbidden_regions == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->max_generation_attempts < 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

static bool dg_constraints_satisfied(const dg_generate_request_t *request, const dg_map_t *map)
{
    const dg_generation_constraints_t *constraints;
    size_t total_cells;
    float floor_coverage;
    const float epsilon = 0.0001f;

    if (request == NULL || map == NULL || map->tiles == NULL) {
        return false;
    }

    constraints = &request->constraints;
    total_cells = (size_t)map->width * (size_t)map->height;
    if (total_cells == 0) {
        return false;
    }

    floor_coverage = (float)map->metadata.walkable_tile_count / (float)total_cells;

    if ((floor_coverage + epsilon) < constraints->min_floor_coverage) {
        return false;
    }

    if ((floor_coverage - epsilon) > constraints->max_floor_coverage) {
        return false;
    }

    if (constraints->min_room_count > 0 && (int)map->metadata.room_count < constraints->min_room_count) {
        return false;
    }

    if (constraints->max_room_count > 0 && (int)map->metadata.room_count > constraints->max_room_count) {
        return false;
    }

    if (constraints->min_special_rooms > 0 &&
        (int)map->metadata.special_room_count < constraints->min_special_rooms) {
        return false;
    }

    if (constraints->require_connected_floor && !map->metadata.connected_floor) {
        return false;
    }

    if (constraints->enforce_outer_walls && !dg_has_outer_walls(map)) {
        return false;
    }

    if (!dg_forbidden_regions_are_clear(map, constraints)) {
        return false;
    }

    return true;
}

static dg_status_t dg_generate_rooms_and_corridors(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    dg_rooms_corridors_config_t config;
    int min_rooms;
    int max_rooms;
    int room_min_size;
    int room_max_size;
    int max_attempts;
    int corridor_width;
    int target_rooms;
    int attempt;
    int max_room_extent;
    dg_status_t status;

    if (request == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    config = request->params.rooms;
    min_rooms = dg_max_int(config.min_rooms, 1);
    max_rooms = dg_max_int(config.max_rooms, min_rooms);
    room_min_size = dg_max_int(config.room_min_size, 3);
    room_max_size = dg_max_int(config.room_max_size, room_min_size);
    max_attempts = dg_max_int(config.max_placement_attempts, max_rooms * 8);
    corridor_width = dg_clamp_int(config.corridor_width, 1, 9);
    max_room_extent = dg_min_int(map->width - 2, map->height - 2);

    if (max_room_extent < room_min_size) {
        return DG_STATUS_GENERATION_FAILED;
    }

    room_max_size = dg_clamp_int(room_max_size, room_min_size, max_room_extent);
    target_rooms = dg_rng_range(rng, min_rooms, max_rooms);

    for (attempt = 0; attempt < max_attempts && (int)map->metadata.room_count < target_rooms; ++attempt) {
        dg_rect_t candidate;
        int max_x;
        int max_y;
        dg_room_flags_t flags;

        candidate.width = dg_rng_range(rng, room_min_size, room_max_size);
        candidate.height = dg_rng_range(rng, room_min_size, room_max_size);
        max_x = map->width - candidate.width - 1;
        max_y = map->height - candidate.height - 1;
        if (max_x < 1 || max_y < 1) {
            continue;
        }

        candidate.x = dg_rng_range(rng, 1, max_x);
        candidate.y = dg_rng_range(rng, 1, max_y);
        if (!dg_can_place_room(map, &candidate, 1, &request->constraints)) {
            continue;
        }

        dg_carve_room(map, &candidate);
        flags = DG_ROOM_FLAG_NONE;
        if (config.classify_room != NULL) {
            flags = config.classify_room(
                (int)map->metadata.room_count,
                &candidate,
                config.classify_room_user_data
            );
        }

        status = dg_map_add_room(map, &candidate, flags);
        if (status != DG_STATUS_OK) {
            return status;
        }
    }

    if (map->metadata.room_count == 0) {
        return DG_STATUS_GENERATION_FAILED;
    }

    if (map->metadata.room_count > 1) {
        size_t i;
        for (i = 1; i < map->metadata.room_count; ++i) {
            dg_point_t a;
            dg_point_t b;

            a = dg_room_center(&map->metadata.rooms[i - 1].bounds);
            b = dg_room_center(&map->metadata.rooms[i].bounds);
            dg_connect_points(map, rng, a, b, corridor_width);

            status = dg_map_add_corridor(
                map,
                map->metadata.rooms[i - 1].id,
                map->metadata.rooms[i].id,
                corridor_width
            );
            if (status != DG_STATUS_OK) {
                return status;
            }
        }
    }

    return DG_STATUS_OK;
}

static void dg_random_step(
    dg_rng_t *rng,
    int *x,
    int *y,
    int min_x,
    int max_x,
    int min_y,
    int max_y
)
{
    int direction;

    direction = dg_rng_range(rng, 0, 3);
    switch (direction) {
    case 0:
        *x += 1;
        break;
    case 1:
        *x -= 1;
        break;
    case 2:
        *y += 1;
        break;
    default:
        *y -= 1;
        break;
    }

    *x = dg_clamp_int(*x, min_x, max_x);
    *y = dg_clamp_int(*y, min_y, max_y);
}

static dg_status_t dg_generate_organic_cave(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    dg_organic_cave_config_t config;
    int walk_steps;
    int brush_radius;
    int smoothing_passes;
    float target_floor_coverage;
    int x;
    int y;
    int i;
    dg_status_t status;

    if (request == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    config = request->params.organic;
    walk_steps = config.walk_steps;
    if (walk_steps <= 0) {
        walk_steps = map->width * map->height;
    }
    brush_radius = dg_clamp_int(config.brush_radius, 0, 6);
    smoothing_passes = dg_clamp_int(config.smoothing_passes, 0, 8);
    target_floor_coverage = config.target_floor_coverage;
    if (target_floor_coverage < 0.0f) {
        target_floor_coverage = 0.0f;
    }
    if (target_floor_coverage > 0.9f) {
        target_floor_coverage = 0.9f;
    }

    x = map->width / 2;
    y = map->height / 2;

    for (i = 0; i < walk_steps; ++i) {
        dg_carve_brush(map, x, y, brush_radius, DG_TILE_FLOOR);
        dg_random_step(rng, &x, &y, 1, map->width - 2, 1, map->height - 2);
        if ((dg_rng_next_u32(rng) % 19u) == 0u) {
            x = dg_rng_range(rng, 1, map->width - 2);
            y = dg_rng_range(rng, 1, map->height - 2);
        }
    }

    if (target_floor_coverage > 0.0f) {
        size_t total_cells = (size_t)map->width * (size_t)map->height;
        size_t target_walkable = (size_t)((float)total_cells * target_floor_coverage);
        size_t current_walkable = dg_count_walkable_tiles(map);
        int safety_steps = map->width * map->height * 10;

        while (current_walkable < target_walkable && safety_steps > 0) {
            dg_carve_brush(map, x, y, brush_radius, DG_TILE_FLOOR);
            dg_random_step(rng, &x, &y, 1, map->width - 2, 1, map->height - 2);
            current_walkable = dg_count_walkable_tiles(map);
            safety_steps -= 1;
        }
    }

    status = dg_smooth_walkable_regions(map, smoothing_passes);
    if (status != DG_STATUS_OK) {
        return status;
    }

    return DG_STATUS_OK;
}

void dg_default_rooms_corridors_config(dg_rooms_corridors_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->min_rooms = 6;
    config->max_rooms = 12;
    config->room_min_size = 4;
    config->room_max_size = 10;
    config->max_placement_attempts = 500;
    config->corridor_width = 1;
    config->classify_room = NULL;
    config->classify_room_user_data = NULL;
}

void dg_default_organic_cave_config(dg_organic_cave_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->walk_steps = 2000;
    config->brush_radius = 1;
    config->smoothing_passes = 2;
    config->target_floor_coverage = 0.30f;
}

void dg_default_generate_request(
    dg_generate_request_t *request,
    dg_algorithm_t algorithm,
    int width,
    int height,
    uint64_t seed
)
{
    if (request == NULL) {
        return;
    }

    memset(request, 0, sizeof(*request));
    request->width = width;
    request->height = height;
    request->seed = seed;
    request->algorithm = algorithm;
    request->constraints.require_connected_floor = true;
    request->constraints.enforce_outer_walls = true;
    request->constraints.min_floor_coverage = 0.0f;
    request->constraints.max_floor_coverage = 1.0f;
    request->constraints.min_room_count = 0;
    request->constraints.max_room_count = 0;
    request->constraints.min_special_rooms = 0;
    request->constraints.forbidden_regions = NULL;
    request->constraints.forbidden_region_count = 0;
    request->constraints.max_generation_attempts = 1;

    if (algorithm == DG_ALGORITHM_ORGANIC_CAVE) {
        dg_default_organic_cave_config(&request->params.organic);
    } else {
        dg_default_rooms_corridors_config(&request->params.rooms);
    }
}

dg_status_t dg_generate(const dg_generate_request_t *request, dg_map_t *out_map)
{
    int attempt;
    int max_attempts;
    dg_status_t status;

    if (request == NULL || out_map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (request->width < 5 || request->height < 5) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (
        out_map->tiles != NULL ||
        out_map->metadata.rooms != NULL ||
        out_map->metadata.corridors != NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_validate_constraints(&request->constraints);
    if (status != DG_STATUS_OK) {
        return status;
    }

    max_attempts = request->constraints.max_generation_attempts;
    for (attempt = 0; attempt < max_attempts; ++attempt) {
        dg_map_t generated;
        dg_rng_t rng;
        uint64_t attempt_seed;

        generated.width = 0;
        generated.height = 0;
        generated.tiles = NULL;
        generated.metadata.rooms = NULL;
        generated.metadata.room_count = 0;
        generated.metadata.room_capacity = 0;
        generated.metadata.corridors = NULL;
        generated.metadata.corridor_count = 0;
        generated.metadata.corridor_capacity = 0;
        generated.metadata.seed = 0;
        generated.metadata.algorithm_id = -1;
        generated.metadata.walkable_tile_count = 0;
        generated.metadata.wall_tile_count = 0;
        generated.metadata.special_room_count = 0;
        generated.metadata.connected_component_count = 0;
        generated.metadata.largest_component_size = 0;
        generated.metadata.connected_floor = false;
        generated.metadata.generation_attempts = 0;

        status = dg_map_init(&generated, request->width, request->height, DG_TILE_WALL);
        if (status != DG_STATUS_OK) {
            return status;
        }

        attempt_seed = request->seed + (uint64_t)attempt;
        dg_rng_seed(&rng, attempt_seed);

        switch (request->algorithm) {
        case DG_ALGORITHM_ROOMS_AND_CORRIDORS:
            status = dg_generate_rooms_and_corridors(request, &generated, &rng);
            break;
        case DG_ALGORITHM_ORGANIC_CAVE:
            status = dg_generate_organic_cave(request, &generated, &rng);
            break;
        default:
            status = DG_STATUS_INVALID_ARGUMENT;
            break;
        }

        if (status != DG_STATUS_OK) {
            dg_map_destroy(&generated);
            if (status == DG_STATUS_GENERATION_FAILED) {
                continue;
            }
            return status;
        }

        dg_apply_forbidden_regions(&request->constraints, &generated);

        if (request->constraints.require_connected_floor) {
            status = dg_enforce_single_connected_region(&generated);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&generated);
                return status;
            }
        }

        if (request->constraints.enforce_outer_walls) {
            dg_paint_outer_walls(&generated);
        }

        if (dg_count_walkable_tiles(&generated) == 0) {
            dg_map_destroy(&generated);
            continue;
        }

        status = dg_populate_runtime_metadata(
            &generated,
            attempt_seed,
            (int)request->algorithm,
            (size_t)(attempt + 1)
        );
        if (status != DG_STATUS_OK) {
            dg_map_destroy(&generated);
            return status;
        }

        if (!dg_constraints_satisfied(request, &generated)) {
            dg_map_destroy(&generated);
            continue;
        }

        *out_map = generated;
        return DG_STATUS_OK;
    }

    return DG_STATUS_GENERATION_FAILED;
}
