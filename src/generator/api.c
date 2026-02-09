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

void dg_default_generate_request(
    dg_generate_request_t *request,
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
    dg_default_bsp_config(&request->bsp);
}

dg_status_t dg_generate(const dg_generate_request_t *request, dg_map_t *out_map)
{
    dg_status_t status;
    dg_map_t generated;
    dg_rng_t rng;

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

    status = dg_validate_bsp_config(&request->bsp);
    if (status != DG_STATUS_OK) {
        return status;
    }

    generated = (dg_map_t){0};
    status = dg_map_init(&generated, request->width, request->height, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }

    dg_rng_seed(&rng, request->seed);

    status = dg_generate_bsp_tree_impl(request, &generated, &rng);
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
        (int)DG_ALGORITHM_BSP_TREE,
        1u
    );
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    *out_map = generated;
    return DG_STATUS_OK;
}
