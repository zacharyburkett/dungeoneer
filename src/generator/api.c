#include "internal.h"

#include <string.h>

static dg_status_t dg_validate_bsp_config(const dg_bsp_config_t *config)
{
    if (config == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->min_rooms < 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->max_rooms < config->min_rooms) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->room_min_size < 3) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->room_max_size < config->room_min_size) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_validate_drunkards_walk_config(const dg_drunkards_walk_config_t *config)
{
    if (config == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->wiggle_percent < 0 || config->wiggle_percent > 100) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_validate_rooms_and_mazes_config(const dg_rooms_and_mazes_config_t *config)
{
    if (config == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->min_rooms < 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->max_rooms < config->min_rooms) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->room_min_size < 3) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->room_max_size < config->room_min_size) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->maze_wiggle_percent < 0 || config->maze_wiggle_percent > 100) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->min_room_connections < 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->max_room_connections < config->min_room_connections) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->ensure_full_connectivity != 0 && config->ensure_full_connectivity != 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->dead_end_prune_steps < -1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

void dg_default_bsp_config(dg_bsp_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->min_rooms = 8;
    config->max_rooms = 16;
    config->room_min_size = 4;
    config->room_max_size = 12;
}

void dg_default_drunkards_walk_config(dg_drunkards_walk_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->wiggle_percent = 65;
}

void dg_default_rooms_and_mazes_config(dg_rooms_and_mazes_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->min_rooms = 10;
    config->max_rooms = 24;
    config->room_min_size = 4;
    config->room_max_size = 10;
    config->maze_wiggle_percent = 100;
    config->min_room_connections = 1;
    config->max_room_connections = 1;
    config->ensure_full_connectivity = 1;
    config->dead_end_prune_steps = -1;
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

    switch (algorithm) {
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        dg_default_rooms_and_mazes_config(&request->params.rooms_and_mazes);
        break;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        dg_default_drunkards_walk_config(&request->params.drunkards_walk);
        break;
    case DG_ALGORITHM_BSP_TREE:
    default:
        dg_default_bsp_config(&request->params.bsp);
        break;
    }
}

dg_map_generation_class_t dg_algorithm_generation_class(dg_algorithm_t algorithm)
{
    switch (algorithm) {
    case DG_ALGORITHM_BSP_TREE:
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        return DG_MAP_GENERATION_CLASS_ROOM_LIKE;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        return DG_MAP_GENERATION_CLASS_CAVE_LIKE;
    default:
        return DG_MAP_GENERATION_CLASS_UNKNOWN;
    }
}

dg_status_t dg_generate(const dg_generate_request_t *request, dg_map_t *out_map)
{
    dg_status_t status;
    dg_map_t generated;
    dg_rng_t rng;
    dg_map_generation_class_t generation_class;

    if (request == NULL || out_map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (request->width < 8 || request->height < 8) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (
        out_map->tiles != NULL ||
        out_map->metadata.rooms != NULL ||
        out_map->metadata.corridors != NULL ||
        out_map->metadata.room_adjacency != NULL ||
        out_map->metadata.room_neighbors != NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    switch (request->algorithm) {
    case DG_ALGORITHM_BSP_TREE:
        status = dg_validate_bsp_config(&request->params.bsp);
        break;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        status = dg_validate_rooms_and_mazes_config(&request->params.rooms_and_mazes);
        break;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        status = dg_validate_drunkards_walk_config(&request->params.drunkards_walk);
        break;
    default:
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (status != DG_STATUS_OK) {
        return status;
    }

    generation_class = dg_algorithm_generation_class(request->algorithm);
    if (generation_class == DG_MAP_GENERATION_CLASS_UNKNOWN) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    generated = (dg_map_t){0};
    status = dg_map_init(&generated, request->width, request->height, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }

    dg_rng_seed(&rng, request->seed);

    switch (request->algorithm) {
    case DG_ALGORITHM_BSP_TREE:
        status = dg_generate_bsp_tree_impl(request, &generated, &rng);
        break;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        status = dg_generate_rooms_and_mazes_impl(request, &generated, &rng);
        break;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        status = dg_generate_drunkards_walk_impl(request, &generated, &rng);
        break;
    default:
        status = DG_STATUS_INVALID_ARGUMENT;
        break;
    }

    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    dg_paint_outer_walls(&generated);

    if (dg_count_walkable_tiles(&generated) == 0) {
        dg_map_destroy(&generated);
        return DG_STATUS_GENERATION_FAILED;
    }

    status = dg_populate_runtime_metadata(
        &generated,
        request->seed,
        (int)request->algorithm,
        generation_class,
        1u
    );
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    *out_map = generated;
    return DG_STATUS_OK;
}
