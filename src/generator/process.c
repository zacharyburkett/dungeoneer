#include "internal.h"

#include <limits.h>
#include <stdlib.h>

static void dg_clear_process_step_diagnostics(dg_map_t *map)
{
    if (map == NULL) {
        return;
    }

    free(map->metadata.diagnostics.process_steps);
    map->metadata.diagnostics.process_steps = NULL;
    map->metadata.diagnostics.process_step_count = 0;
}

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

static bool dg_point_in_any_room_bounds(const dg_map_t *map, int x, int y)
{
    size_t i;

    if (map == NULL || map->metadata.rooms == NULL) {
        return false;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_rect_t *room = &map->metadata.rooms[i].bounds;

        if (x >= room->x &&
            y >= room->y &&
            x < room->x + room->width &&
            y < room->y + room->height) {
            return true;
        }
    }

    return false;
}

static bool dg_is_corridor_floor(const dg_map_t *map, int x, int y)
{
    if (map == NULL || !dg_map_in_bounds(map, x, y)) {
        return false;
    }

    return dg_is_walkable_tile(dg_map_get_tile(map, x, y)) &&
           !dg_point_in_any_room_bounds(map, x, y);
}

static bool dg_is_corridor_border_wall_candidate(const dg_map_t *map, int x, int y)
{
    static const int k_dirs[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    int d;

    if (map == NULL || !dg_map_in_bounds(map, x, y)) {
        return false;
    }

    if (dg_map_get_tile(map, x, y) != DG_TILE_WALL) {
        return false;
    }

    if (dg_point_in_any_room_bounds(map, x, y)) {
        return false;
    }

    for (d = 0; d < 4; ++d) {
        int nx = x + k_dirs[d][0];
        int ny = y + k_dirs[d][1];

        if (dg_is_corridor_floor(map, nx, ny)) {
            return true;
        }
    }

    return false;
}

static dg_status_t dg_apply_corridor_roughen_pass(
    dg_map_t *map,
    int strength,
    dg_corridor_roughen_mode_t mode,
    dg_rng_t *rng,
    size_t *out_carved_count
)
{
    unsigned char *candidate_mask;
    unsigned char *random_field;
    size_t carved_count;
    size_t candidate_count;
    size_t cell_count;
    int x;
    int y;

    if (map == NULL || map->tiles == NULL || rng == NULL || out_carved_count == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_carved_count = 0;
    if (strength < 0 || strength > 100) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (mode != DG_CORRIDOR_ROUGHEN_UNIFORM && mode != DG_CORRIDOR_ROUGHEN_ORGANIC) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (strength == 0 || map->width < 3 || map->height < 3) {
        return DG_STATUS_OK;
    }

    carved_count = 0;
    cell_count = (size_t)map->width * (size_t)map->height;
    candidate_mask = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
    if (candidate_mask == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    candidate_count = 0;
    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            size_t index;

            if (!dg_is_corridor_border_wall_candidate(map, x, y)) {
                continue;
            }

            index = dg_tile_index(map, x, y);
            candidate_mask[index] = 1u;
            candidate_count += 1;
        }
    }

    if (candidate_count == 0) {
        free(candidate_mask);
        return DG_STATUS_OK;
    }

    if (mode == DG_CORRIDOR_ROUGHEN_UNIFORM) {
        for (y = 1; y < map->height - 1; ++y) {
            for (x = 1; x < map->width - 1; ++x) {
                size_t index = dg_tile_index(map, x, y);

                if (candidate_mask[index] == 0u) {
                    continue;
                }

                if (dg_rng_range(rng, 0, 99) < strength) {
                    map->tiles[index] = DG_TILE_FLOOR;
                    carved_count += 1;
                }
            }
        }

        free(candidate_mask);
        *out_carved_count = carved_count;
        return DG_STATUS_OK;
    }

    random_field = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
    if (random_field == NULL) {
        free(candidate_mask);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            size_t index = dg_tile_index(map, x, y);

            if (candidate_mask[index] == 0u) {
                continue;
            }

            random_field[index] = (unsigned char)dg_rng_range(rng, 0, 100);
        }
    }

    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            static const int k_dirs[4][2] = {
                {1, 0},
                {-1, 0},
                {0, 1},
                {0, -1}
            };
            size_t index = dg_tile_index(map, x, y);
            int dy;
            int dx;
            int sum;
            int weight;
            int corridor_neighbors;
            int threshold;
            int averaged;
            int d;

            if (candidate_mask[index] == 0u) {
                continue;
            }

            sum = (int)random_field[index] * 3;
            weight = 3;

            for (dy = -1; dy <= 1; ++dy) {
                for (dx = -1; dx <= 1; ++dx) {
                    size_t nindex;

                    if (dx == 0 && dy == 0) {
                        continue;
                    }

                    nindex = dg_tile_index(map, x + dx, y + dy);
                    if (candidate_mask[nindex] != 0u) {
                        sum += (int)random_field[nindex];
                        weight += 1;
                    }
                }
            }

            corridor_neighbors = 0;
            for (d = 0; d < 4; ++d) {
                int nx = x + k_dirs[d][0];
                int ny = y + k_dirs[d][1];

                if (dg_is_corridor_floor(map, nx, ny)) {
                    corridor_neighbors += 1;
                }
            }

            averaged = sum / weight;
            threshold = strength + (corridor_neighbors * 8) + dg_rng_range(rng, -8, 8);
            threshold = dg_clamp_int(threshold, 0, 100);

            if (averaged <= threshold) {
                map->tiles[index] = DG_TILE_FLOOR;
                carved_count += 1;
            }
        }
    }

    free(random_field);
    free(candidate_mask);
    *out_carved_count = carved_count;
    return DG_STATUS_OK;
}

static dg_status_t dg_apply_corridor_roughen(
    dg_map_t *map,
    int strength,
    int max_depth,
    dg_corridor_roughen_mode_t mode,
    dg_rng_t *rng
)
{
    int depth;

    if (map == NULL || map->tiles == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (max_depth < 1 || max_depth > 32) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (depth = 0; depth < max_depth; ++depth) {
        size_t carved_count;
        dg_status_t status;

        status = dg_apply_corridor_roughen_pass(
            map,
            strength,
            mode,
            rng,
            &carved_count
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        if (carved_count == 0) {
            break;
        }
    }

    return DG_STATUS_OK;
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

static dg_status_t dg_apply_process_method(
    const dg_process_method_t *method,
    dg_map_t *map,
    dg_rng_t *rng,
    dg_map_generation_class_t generation_class
)
{
    (void)generation_class;

    if (method == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    switch (method->type) {
    case DG_PROCESS_METHOD_SCALE:
        return dg_scale_map(map, method->params.scale.factor);
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        return dg_smooth_walkable_regions(
            map,
            method->params.path_smooth.strength,
            method->params.path_smooth.inner_enabled,
            method->params.path_smooth.outer_enabled
        );
    case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
        return dg_apply_corridor_roughen(
            map,
            method->params.corridor_roughen.strength,
            method->params.corridor_roughen.max_depth,
            method->params.corridor_roughen.mode,
            rng
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
    dg_process_step_diagnostics_t *process_steps;
    size_t i;

    if (request == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_clear_process_step_diagnostics(map);
    if (request->process.enabled == 0 || request->process.method_count == 0) {
        return DG_STATUS_OK;
    }

    if (request->process.method_count > (SIZE_MAX / sizeof(*process_steps))) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    process_steps = (dg_process_step_diagnostics_t *)calloc(
        request->process.method_count,
        sizeof(*process_steps)
    );
    if (process_steps == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    map->metadata.diagnostics.process_steps = process_steps;
    map->metadata.diagnostics.process_step_count = request->process.method_count;

    generation_class = dg_algorithm_generation_class(request->algorithm);
    for (i = 0; i < request->process.method_count; ++i) {
        dg_connectivity_stats_t before_stats;
        dg_connectivity_stats_t after_stats;
        size_t walkable_before;
        size_t walkable_after;
        dg_process_step_diagnostics_t *step;
        dg_status_t status;
        step = &process_steps[i];
        step->method_type = (int)request->process.methods[i].type;

        status = dg_analyze_connectivity(map, &before_stats);
        if (status != DG_STATUS_OK) {
            dg_clear_process_step_diagnostics(map);
            return status;
        }
        walkable_before = dg_count_walkable_tiles(map);

        status = dg_apply_process_method(
            &request->process.methods[i],
            map,
            rng,
            generation_class
        );
        if (status != DG_STATUS_OK) {
            dg_clear_process_step_diagnostics(map);
            return status;
        }

        status = dg_analyze_connectivity(map, &after_stats);
        if (status != DG_STATUS_OK) {
            dg_clear_process_step_diagnostics(map);
            return status;
        }
        walkable_after = dg_count_walkable_tiles(map);

        step->walkable_before = walkable_before;
        step->walkable_after = walkable_after;
        if (walkable_after >= walkable_before) {
            step->walkable_delta = (int64_t)(walkable_after - walkable_before);
        } else {
            step->walkable_delta = -((int64_t)(walkable_before - walkable_after));
        }

        step->components_before = before_stats.component_count;
        step->components_after = after_stats.component_count;
        if (after_stats.component_count >= before_stats.component_count) {
            step->components_delta =
                (int64_t)(after_stats.component_count - before_stats.component_count);
        } else {
            step->components_delta =
                -((int64_t)(before_stats.component_count - after_stats.component_count));
        }
        step->connected_before = before_stats.connected_floor ? 1 : 0;
        step->connected_after = after_stats.connected_floor ? 1 : 0;
    }

    return DG_STATUS_OK;
}
