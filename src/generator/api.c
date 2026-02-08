#include "internal.h"

#include <string.h>

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

        dg_init_empty_map(&generated);

        status = dg_map_init(&generated, request->width, request->height, DG_TILE_WALL);
        if (status != DG_STATUS_OK) {
            return status;
        }

        attempt_seed = request->seed + (uint64_t)attempt;
        dg_rng_seed(&rng, attempt_seed);

        switch (request->algorithm) {
        case DG_ALGORITHM_ROOMS_AND_CORRIDORS:
            status = dg_generate_rooms_and_corridors_impl(request, &generated, &rng);
            break;
        case DG_ALGORITHM_ORGANIC_CAVE:
            status = dg_generate_organic_cave_impl(request, &generated, &rng);
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
