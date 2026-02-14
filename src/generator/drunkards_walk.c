#include "internal.h"

#include <stdlib.h>

static bool dg_walk_in_bounds(const dg_map_t *map, int x, int y)
{
    return dg_map_in_bounds(map, x, y);
}

dg_status_t dg_generate_drunkards_walk_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    const dg_drunkards_walk_config_t *config;
    size_t total_cells;
    size_t target_floor_tiles;
    size_t max_steps;
    size_t steps;
    size_t carved_count;
    int x;
    int y;
    int dir_index;
    dg_status_t status;

    if (request == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    config = &request->params.drunkards_walk;
    total_cells = (size_t)map->width * (size_t)map->height;

    /* Keep base coverage fixed for the minimal algorithm surface. */
    target_floor_tiles = (total_cells * 33u) / 100u;
    if (target_floor_tiles < 16u) {
        target_floor_tiles = 16u;
    }

    max_steps = total_cells * 24u;
    if (max_steps < target_floor_tiles) {
        max_steps = target_floor_tiles;
    }

    x = dg_rng_range(rng, 0, map->width - 1);
    y = dg_rng_range(rng, 0, map->height - 1);
    dir_index = dg_rng_range(rng, 0, 3);

    carved_count = 0;
    if (dg_map_get_tile(map, x, y) != DG_TILE_FLOOR) {
        (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
        carved_count += 1;
    }

    for (steps = 0; steps < max_steps && carved_count < target_floor_tiles; ++steps) {
        int nx;
        int ny;

        if (dg_rng_range(rng, 0, 99) < config->wiggle_percent) {
            dir_index = dg_rng_range(rng, 0, 3);
        }

        nx = x + directions[dir_index][0];
        ny = y + directions[dir_index][1];

        if (!dg_walk_in_bounds(map, nx, ny)) {
            dir_index = dg_rng_range(rng, 0, 3);
            continue;
        }

        x = nx;
        y = ny;
        if (dg_map_get_tile(map, x, y) != DG_TILE_FLOOR) {
            (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
            carved_count += 1;
        }
    }

    if (carved_count == 0) {
        return DG_STATUS_GENERATION_FAILED;
    }

    return DG_STATUS_OK;
}
