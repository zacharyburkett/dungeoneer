#include "internal.h"

#include <stdlib.h>
#include <string.h>

static int dg_count_neighbor_walls(const dg_map_t *map, int x, int y)
{
    int wall_count;
    int ny;
    int nx;

    wall_count = 0;
    for (ny = y - 1; ny <= y + 1; ++ny) {
        for (nx = x - 1; nx <= x + 1; ++nx) {
            if (nx == x && ny == y) {
                continue;
            }

            if (!dg_map_in_bounds(map, nx, ny)) {
                wall_count += 1;
            } else if (dg_map_get_tile(map, nx, ny) == DG_TILE_WALL) {
                wall_count += 1;
            }
        }
    }

    return wall_count;
}

dg_status_t dg_generate_cellular_automata_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    const dg_cellular_automata_config_t *config;
    dg_status_t status;
    size_t cell_count;
    dg_tile_t *scratch;
    int x;
    int y;
    int step;

    if (request == NULL || map == NULL || map->tiles == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    config = &request->params.cellular_automata;
    cell_count = (size_t)map->width * (size_t)map->height;

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            if (dg_rng_range(rng, 0, 99) >= config->initial_wall_percent) {
                (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
            }
        }
    }

    scratch = (dg_tile_t *)malloc(cell_count * sizeof(*scratch));
    if (scratch == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (step = 0; step < config->simulation_steps; ++step) {
        for (y = 0; y < map->height; ++y) {
            for (x = 0; x < map->width; ++x) {
                size_t index = dg_tile_index(map, x, y);
                int wall_neighbors;

                if (x == 0 || y == 0 || x == map->width - 1 || y == map->height - 1) {
                    scratch[index] = DG_TILE_WALL;
                    continue;
                }

                wall_neighbors = dg_count_neighbor_walls(map, x, y);
                scratch[index] = (wall_neighbors >= config->wall_threshold)
                                     ? DG_TILE_WALL
                                     : DG_TILE_FLOOR;
            }
        }

        memcpy(map->tiles, scratch, cell_count * sizeof(*scratch));
    }

    free(scratch);

    if (dg_count_walkable_tiles(map) == 0u) {
        int cx = map->width / 2;
        int cy = map->height / 2;
        (void)dg_map_set_tile(map, cx, cy, DG_TILE_FLOOR);
    }

    status = dg_enforce_single_connected_region(map);
    if (status != DG_STATUS_OK) {
        return status;
    }

    if (dg_count_walkable_tiles(map) == 0u) {
        return DG_STATUS_GENERATION_FAILED;
    }

    return DG_STATUS_OK;
}
