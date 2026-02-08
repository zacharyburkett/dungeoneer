#include "internal.h"

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

dg_status_t dg_generate_organic_cave_impl(
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
