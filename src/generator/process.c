#include "internal.h"

#include <limits.h>
#include <stdlib.h>

static bool dg_mul_size_checked(size_t a, size_t b, size_t *out)
{
    if (out == NULL) {
        return false;
    }

    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }

    if (a > (SIZE_MAX / b)) {
        return false;
    }

    *out = a * b;
    return true;
}

static bool dg_mul_int_checked(int value, int factor, int *out)
{
    if (out == NULL || value < 0 || factor < 0) {
        return false;
    }

    if (factor == 0) {
        *out = 0;
        return true;
    }

    if (value > (INT_MAX / factor)) {
        return false;
    }

    *out = value * factor;
    return true;
}

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

static dg_status_t dg_scale_map_tiles(dg_map_t *map, int factor)
{
    int new_width;
    int new_height;
    size_t old_cell_count;
    size_t new_cell_count;
    dg_tile_t *scaled_tiles;
    int x;
    int y;

    if (map == NULL || map->tiles == NULL || factor < 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (factor == 1) {
        return DG_STATUS_OK;
    }

    if (!dg_mul_int_checked(map->width, factor, &new_width) ||
        !dg_mul_int_checked(map->height, factor, &new_height)) {
        return DG_STATUS_GENERATION_FAILED;
    }

    if (!dg_mul_size_checked((size_t)new_width, (size_t)new_height, &new_cell_count)) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    if (new_cell_count > (SIZE_MAX / sizeof(dg_tile_t))) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    scaled_tiles = (dg_tile_t *)malloc(new_cell_count * sizeof(dg_tile_t));
    if (scaled_tiles == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    old_cell_count = (size_t)map->width * (size_t)map->height;
    for (y = 0; y < map->height; ++y) {
        for (x = 0; x < map->width; ++x) {
            size_t src_index;
            dg_tile_t tile;
            int dx;
            int dy;

            src_index = (size_t)y * (size_t)map->width + (size_t)x;
            if (src_index >= old_cell_count) {
                free(scaled_tiles);
                return DG_STATUS_GENERATION_FAILED;
            }

            tile = map->tiles[src_index];
            for (dy = 0; dy < factor; ++dy) {
                int scaled_y = y * factor + dy;
                for (dx = 0; dx < factor; ++dx) {
                    int scaled_x = x * factor + dx;
                    size_t dst_index = (size_t)scaled_y * (size_t)new_width + (size_t)scaled_x;
                    scaled_tiles[dst_index] = tile;
                }
            }
        }
    }

    free(map->tiles);
    map->tiles = scaled_tiles;
    map->width = new_width;
    map->height = new_height;
    return DG_STATUS_OK;
}

static dg_status_t dg_scale_map_metadata(dg_map_t *map, int factor)
{
    size_t i;

    if (map == NULL || factor < 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (factor == 1) {
        return DG_STATUS_OK;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        dg_room_metadata_t *room = &map->metadata.rooms[i];

        if (!dg_mul_int_checked(room->bounds.x, factor, &room->bounds.x) ||
            !dg_mul_int_checked(room->bounds.y, factor, &room->bounds.y) ||
            !dg_mul_int_checked(room->bounds.width, factor, &room->bounds.width) ||
            !dg_mul_int_checked(room->bounds.height, factor, &room->bounds.height)) {
            return DG_STATUS_GENERATION_FAILED;
        }
    }

    for (i = 0; i < map->metadata.corridor_count; ++i) {
        dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];

        if (!dg_mul_int_checked(corridor->width, factor, &corridor->width) ||
            !dg_mul_int_checked(corridor->length, factor, &corridor->length)) {
            return DG_STATUS_GENERATION_FAILED;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_scale_map(dg_map_t *map, int factor)
{
    dg_status_t status;

    if (map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_scale_map_metadata(map, factor);
    if (status != DG_STATUS_OK) {
        return status;
    }

    return dg_scale_map_tiles(map, factor);
}

static size_t dg_collect_room_entrances(
    const dg_map_t *map,
    const dg_rect_t *room,
    dg_point_t *out_points,
    size_t out_capacity
)
{
    static const int k_dirs[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    size_t count;
    int y;
    int x;

    if (map == NULL || room == NULL || out_points == NULL) {
        return 0;
    }

    count = 0;
    for (y = room->y; y < room->y + room->height; ++y) {
        for (x = room->x; x < room->x + room->width; ++x) {
            int d;
            bool is_entrance;

            if (!dg_is_walkable_tile(dg_map_get_tile(map, x, y))) {
                continue;
            }

            is_entrance = false;
            for (d = 0; d < 4; ++d) {
                int nx = x + k_dirs[d][0];
                int ny = y + k_dirs[d][1];
                bool neighbor_in_room = dg_point_in_rect(room, nx, ny);

                if (neighbor_in_room || !dg_map_in_bounds(map, nx, ny)) {
                    continue;
                }

                if (dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
                    is_entrance = true;
                    break;
                }
            }

            if (is_entrance && count < out_capacity) {
                out_points[count++] = (dg_point_t){x, y};
            }
        }
    }

    return count;
}

static dg_status_t dg_carve_organic_room(
    dg_map_t *map,
    const dg_rect_t *room,
    int organicity,
    dg_rng_t *rng
)
{
    size_t room_area;
    unsigned char *keep_mask;
    dg_point_t *entrances;
    size_t entrance_count;
    double cx;
    double cy;
    double rx;
    double ry;
    double roughness;
    int x;
    int y;
    dg_point_t anchor;

    if (map == NULL || room == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }
    if (room->width <= 0 || room->height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (!dg_mul_size_checked((size_t)room->width, (size_t)room->height, &room_area)) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    keep_mask = (unsigned char *)calloc(room_area, sizeof(unsigned char));
    entrances = (dg_point_t *)malloc(room_area * sizeof(dg_point_t));
    if (keep_mask == NULL || entrances == NULL) {
        free(keep_mask);
        free(entrances);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    entrance_count = dg_collect_room_entrances(map, room, entrances, room_area);
    cx = ((double)room->width - 1.0) * 0.5;
    cy = ((double)room->height - 1.0) * 0.5;
    rx = dg_max_int(room->width - 1, 1) * 0.5;
    ry = dg_max_int(room->height - 1, 1) * 0.5;
    roughness = ((double)dg_clamp_int(organicity, 0, 100) / 100.0) * 0.55;

    for (y = 0; y < room->height; ++y) {
        for (x = 0; x < room->width; ++x) {
            size_t index = (size_t)y * (size_t)room->width + (size_t)x;
            double nx = ((double)x - cx) / rx;
            double ny = ((double)y - cy) / ry;
            double score = nx * nx + ny * ny;
            double noise = ((double)dg_rng_range(rng, -100, 100) / 100.0) * roughness;
            double threshold = 1.0 + noise;

            if (score <= threshold) {
                keep_mask[index] = 1u;
            }
        }
    }

    for (y = room->y; y < room->y + room->height; ++y) {
        for (x = room->x; x < room->x + room->width; ++x) {
            map->tiles[dg_tile_index(map, x, y)] = DG_TILE_WALL;
        }
    }

    for (y = 0; y < room->height; ++y) {
        for (x = 0; x < room->width; ++x) {
            size_t index = (size_t)y * (size_t)room->width + (size_t)x;
            if (keep_mask[index] != 0u) {
                int tx = room->x + x;
                int ty = room->y + y;
                map->tiles[dg_tile_index(map, tx, ty)] = DG_TILE_FLOOR;
            }
        }
    }

    anchor.x = room->x + room->width / 2;
    anchor.y = room->y + room->height / 2;
    if (!dg_point_in_rect(room, anchor.x, anchor.y)) {
        anchor.x = room->x;
        anchor.y = room->y;
    }
    map->tiles[dg_tile_index(map, anchor.x, anchor.y)] = DG_TILE_FLOOR;

    for (x = 0; (size_t)x < entrance_count; ++x) {
        int sx;
        int sy;

        map->tiles[dg_tile_index(map, entrances[x].x, entrances[x].y)] = DG_TILE_FLOOR;

        sx = entrances[x].x;
        sy = entrances[x].y;
        while (sx != anchor.x) {
            sx += (anchor.x > sx) ? 1 : -1;
            if (dg_point_in_rect(room, sx, sy)) {
                map->tiles[dg_tile_index(map, sx, sy)] = DG_TILE_FLOOR;
            }
        }
        while (sy != anchor.y) {
            sy += (anchor.y > sy) ? 1 : -1;
            if (dg_point_in_rect(room, sx, sy)) {
                map->tiles[dg_tile_index(map, sx, sy)] = DG_TILE_FLOOR;
            }
        }
    }

    free(keep_mask);
    free(entrances);
    return DG_STATUS_OK;
}

static dg_status_t dg_apply_organic_room_shapes(
    dg_map_t *map,
    int organicity,
    dg_rng_t *rng
)
{
    size_t i;

    if (map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.room_count == 0) {
        return DG_STATUS_OK;
    }

    if (map->metadata.rooms == NULL) {
        return DG_STATUS_GENERATION_FAILED;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        dg_status_t status = dg_carve_organic_room(
            map,
            &map->metadata.rooms[i].bounds,
            organicity,
            rng
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_apply_process_method(
    const dg_process_method_t *method,
    dg_map_t *map,
    dg_rng_t *rng,
    dg_map_generation_class_t generation_class
)
{
    if (method == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    switch (method->type) {
    case DG_PROCESS_METHOD_SCALE:
        return dg_scale_map(map, method->params.scale.factor);
    case DG_PROCESS_METHOD_ROOM_SHAPE:
        if (method->params.room_shape.mode == DG_ROOM_SHAPE_ORGANIC &&
            generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE) {
            return dg_apply_organic_room_shapes(map, method->params.room_shape.organicity, rng);
        }
        return DG_STATUS_OK;
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        return dg_smooth_walkable_regions(
            map,
            method->params.path_smooth.strength,
            method->params.path_smooth.inner_enabled,
            method->params.path_smooth.outer_enabled
        );
    default:
        return DG_STATUS_INVALID_ARGUMENT;
    }
}

dg_status_t dg_apply_post_processes(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    dg_map_generation_class_t generation_class;
    size_t i;

    if (request == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    generation_class = dg_algorithm_generation_class(request->algorithm);
    for (i = 0; i < request->process.method_count; ++i) {
        dg_status_t status = dg_apply_process_method(
            &request->process.methods[i],
            map,
            rng,
            generation_class
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
    }

    return DG_STATUS_OK;
}
