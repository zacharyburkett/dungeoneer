#include "internal.h"

#include <stdlib.h>

typedef struct dg_worm_state {
    int x;
    int y;
    int dir;
    int steps;
    int alive;
} dg_worm_state_t;

static int dg_worm_in_bounds(const dg_map_t *map, int x, int y)
{
    return dg_map_in_bounds(map, x, y) ? 1 : 0;
}

static size_t dg_worm_carve_brush_count(dg_map_t *map, int cx, int cy, int radius)
{
    size_t carved;
    int dy;
    int dx;
    int radius_sq;

    carved = 0u;
    if (map == NULL || map->tiles == NULL) {
        return 0u;
    }

    if (radius < 0) {
        radius = 0;
    }
    radius_sq = radius * radius;

    for (dy = -radius; dy <= radius; ++dy) {
        for (dx = -radius; dx <= radius; ++dx) {
            int nx;
            int ny;
            int dist_sq;

            dist_sq = dx * dx + dy * dy;
            if (dist_sq > radius_sq) {
                continue;
            }

            nx = cx + dx;
            ny = cy + dy;
            if (!dg_worm_in_bounds(map, nx, ny)) {
                continue;
            }

            if (dg_map_get_tile(map, nx, ny) != DG_TILE_FLOOR) {
                (void)dg_map_set_tile(map, nx, ny, DG_TILE_FLOOR);
                carved += 1u;
            }
        }
    }

    return carved;
}

static int dg_worm_find_free_slot(dg_worm_state_t *worms, int worm_capacity)
{
    int i;

    for (i = 0; i < worm_capacity; ++i) {
        if (worms[i].alive == 0) {
            return i;
        }
    }

    return -1;
}

dg_status_t dg_generate_worm_caves_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    static const int k_dirs[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    const dg_worm_caves_config_t *config;
    dg_worm_state_t *worms;
    int worm_capacity;
    int active_count;
    int i;
    size_t carved;
    size_t target_floor;
    size_t interior_cells;
    size_t max_iterations;
    size_t iteration;
    dg_status_t status;

    if (request == NULL || map == NULL || map->tiles == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    config = &request->params.worm_caves;
    if (map->width <= 2 || map->height <= 2) {
        return DG_STATUS_GENERATION_FAILED;
    }

    interior_cells = (size_t)(map->width - 2) * (size_t)(map->height - 2);
    target_floor = (interior_cells * (size_t)config->target_floor_percent) / 100u;
    if (target_floor < 16u) {
        target_floor = 16u;
    }
    if (target_floor > interior_cells) {
        target_floor = interior_cells;
    }

    worm_capacity = config->worm_count * 8;
    if (worm_capacity < config->worm_count) {
        worm_capacity = config->worm_count;
    }
    if (worm_capacity > 512) {
        worm_capacity = 512;
    }

    worms = (dg_worm_state_t *)calloc((size_t)worm_capacity, sizeof(*worms));
    if (worms == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    active_count = config->worm_count;
    for (i = 0; i < config->worm_count; ++i) {
        worms[i].x = dg_rng_range(rng, 0, map->width - 1);
        worms[i].y = dg_rng_range(rng, 0, map->height - 1);
        worms[i].dir = dg_rng_range(rng, 0, 3);
        worms[i].steps = 0;
        worms[i].alive = 1;
    }

    carved = 0u;
    for (i = 0; i < config->worm_count; ++i) {
        carved += dg_worm_carve_brush_count(map, worms[i].x, worms[i].y, config->brush_radius);
    }

    max_iterations = interior_cells * 64u;
    if (max_iterations < 4000u) {
        max_iterations = 4000u;
    }

    for (iteration = 0u;
         iteration < max_iterations && carved < target_floor && active_count > 0;
         ++iteration) {
        for (i = 0; i < worm_capacity && carved < target_floor; ++i) {
            int nx;
            int ny;

            if (worms[i].alive == 0) {
                continue;
            }

            if (dg_rng_range(rng, 0, 99) < config->wiggle_percent) {
                worms[i].dir = dg_rng_range(rng, 0, 3);
            }

            if (dg_rng_range(rng, 0, 99) < config->branch_chance_percent &&
                active_count < worm_capacity) {
                int slot = dg_worm_find_free_slot(worms, worm_capacity);
                if (slot >= 0) {
                    worms[slot] = worms[i];
                    worms[slot].dir = dg_rng_range(rng, 0, 3);
                    worms[slot].steps = 0;
                    worms[slot].alive = 1;
                    active_count += 1;
                }
            }

            nx = worms[i].x + k_dirs[worms[i].dir][0];
            ny = worms[i].y + k_dirs[worms[i].dir][1];
            if (!dg_worm_in_bounds(map, nx, ny)) {
                worms[i].dir = dg_rng_range(rng, 0, 3);
                continue;
            }

            worms[i].x = nx;
            worms[i].y = ny;
            worms[i].steps += 1;
            carved += dg_worm_carve_brush_count(map, nx, ny, config->brush_radius);

            if (worms[i].steps >= config->max_steps_per_worm) {
                if (i < config->worm_count) {
                    worms[i].x = dg_rng_range(rng, 0, map->width - 1);
                    worms[i].y = dg_rng_range(rng, 0, map->height - 1);
                    worms[i].dir = dg_rng_range(rng, 0, 3);
                    worms[i].steps = 0;
                } else {
                    worms[i].alive = 0;
                    active_count -= 1;
                }
            }
        }
    }

    free(worms);

    if (carved == 0u) {
        return DG_STATUS_GENERATION_FAILED;
    }

    if (config->ensure_connected != 0) {
        status = dg_enforce_single_connected_region(map);
        if (status != DG_STATUS_OK) {
            return status;
        }
    }

    if (dg_count_walkable_tiles(map) == 0u) {
        return DG_STATUS_GENERATION_FAILED;
    }

    return DG_STATUS_OK;
}
