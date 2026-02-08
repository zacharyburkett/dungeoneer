#include "dungeoneer/generator.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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

static bool dg_rects_overlap_with_padding(const dg_rect_t *a, const dg_rect_t *b, int padding)
{
    int a_left;
    int a_top;
    int a_right;
    int a_bottom;
    int b_left;
    int b_top;
    int b_right;
    int b_bottom;

    a_left = a->x - padding;
    a_top = a->y - padding;
    a_right = a->x + a->width + padding;
    a_bottom = a->y + a->height + padding;
    b_left = b->x - padding;
    b_top = b->y - padding;
    b_right = b->x + b->width + padding;
    b_bottom = b->y + b->height + padding;

    if (a_right <= b_left || b_right <= a_left) {
        return false;
    }

    if (a_bottom <= b_top || b_bottom <= a_top) {
        return false;
    }

    return true;
}

static bool dg_can_place_room(const dg_map_t *map, const dg_rect_t *candidate, int spacing)
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
        if (!dg_can_place_room(map, &candidate, 1)) {
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

    if (algorithm == DG_ALGORITHM_ORGANIC_CAVE) {
        dg_default_organic_cave_config(&request->params.organic);
    } else {
        dg_default_rooms_corridors_config(&request->params.rooms);
    }
}

dg_status_t dg_generate(const dg_generate_request_t *request, dg_map_t *out_map)
{
    dg_map_t generated;
    dg_rng_t rng;
    dg_status_t status;

    if (request == NULL || out_map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (request->width < 5 || request->height < 5) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    generated.width = 0;
    generated.height = 0;
    generated.tiles = NULL;
    generated.metadata.rooms = NULL;
    generated.metadata.room_count = 0;
    generated.metadata.room_capacity = 0;

    status = dg_map_init(&generated, request->width, request->height, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }

    dg_rng_seed(&rng, request->seed);
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
        return status;
    }

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
        return DG_STATUS_GENERATION_FAILED;
    }

    *out_map = generated;
    return DG_STATUS_OK;
}
