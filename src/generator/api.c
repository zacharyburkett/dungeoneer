#include "internal.h"

static dg_status_t dg_generate_impl(
    const dg_generate_request_t *request,
    dg_map_t *out_map,
    int enforce_public_min_dimensions
)
{
    dg_status_t status;
    dg_map_t generated;
    dg_rng_t rng;
    dg_map_generation_class_t generation_class;

    if (request == NULL || out_map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (enforce_public_min_dimensions != 0 &&
        (request->width < 8 || request->height < 8)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (
        out_map->tiles != NULL ||
        out_map->metadata.rooms != NULL ||
        out_map->metadata.corridors != NULL ||
        out_map->metadata.room_entrances != NULL ||
        out_map->metadata.edge_openings != NULL ||
        out_map->metadata.room_adjacency != NULL ||
        out_map->metadata.room_neighbors != NULL ||
        out_map->metadata.diagnostics.process_steps != NULL ||
        out_map->metadata.diagnostics.room_type_quotas != NULL ||
        out_map->metadata.generation_request.process.methods != NULL ||
        out_map->metadata.generation_request.room_types.definitions != NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_validate_generate_request(request);
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
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        status = dg_generate_cellular_automata_impl(request, &generated, &rng);
        break;
    case DG_ALGORITHM_VALUE_NOISE:
        status = dg_generate_value_noise_impl(request, &generated, &rng);
        break;
    case DG_ALGORITHM_ROOM_GRAPH:
        status = dg_generate_room_graph_impl(request, &generated, &rng);
        break;
    case DG_ALGORITHM_WORM_CAVES:
        status = dg_generate_worm_caves_impl(request, &generated, &rng);
        break;
    case DG_ALGORITHM_SIMPLEX_NOISE:
        status = dg_generate_simplex_noise_impl(request, &generated, &rng);
        break;
    default:
        status = DG_STATUS_INVALID_ARGUMENT;
        break;
    }

    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    status = dg_populate_runtime_metadata(
        &generated,
        request->seed,
        (int)request->algorithm,
        generation_class,
        1u,
        true
    );
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    status = dg_apply_room_type_assignment(request, &generated, &rng);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    status = dg_apply_room_type_templates(request, &generated);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    status = dg_apply_post_processes(request, &generated, &rng);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    dg_paint_outer_walls(&generated);
    status = dg_apply_explicit_edge_openings(request, &generated);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    if (dg_count_walkable_tiles(&generated) == 0) {
        dg_map_destroy(&generated);
        return DG_STATUS_GENERATION_FAILED;
    }

    status = dg_populate_runtime_metadata(
        &generated,
        request->seed,
        (int)request->algorithm,
        generation_class,
        1u,
        false
    );
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }
    status = dg_apply_explicit_edge_opening_roles(request, &generated);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    status = dg_snapshot_generation_request(request, &generated);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&generated);
        return status;
    }

    *out_map = generated;
    return DG_STATUS_OK;
}

dg_status_t dg_generate(const dg_generate_request_t *request, dg_map_t *out_map)
{
    return dg_generate_impl(request, out_map, 1);
}

dg_status_t dg_generate_internal_allow_small(
    const dg_generate_request_t *request,
    dg_map_t *out_map
)
{
    return dg_generate_impl(request, out_map, 0);
}
