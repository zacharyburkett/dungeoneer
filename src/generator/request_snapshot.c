#include "internal.h"

#include <stdlib.h>

static dg_status_t dg_copy_process_methods_to_snapshot(
    dg_snapshot_process_method_t **out_methods,
    size_t method_count,
    const dg_process_method_t *source_methods
)
{
    size_t i;
    dg_snapshot_process_method_t *methods;

    if (out_methods == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_methods = NULL;
    if (method_count == 0) {
        return DG_STATUS_OK;
    }

    if (source_methods == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (method_count > (SIZE_MAX / sizeof(*methods))) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    methods = (dg_snapshot_process_method_t *)malloc(method_count * sizeof(*methods));
    if (methods == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (i = 0; i < method_count; ++i) {
        methods[i].type = (int)source_methods[i].type;
        switch (source_methods[i].type) {
        case DG_PROCESS_METHOD_SCALE:
            methods[i].params.scale.factor = source_methods[i].params.scale.factor;
            break;
        case DG_PROCESS_METHOD_PATH_SMOOTH:
            methods[i].params.path_smooth.strength = source_methods[i].params.path_smooth.strength;
            methods[i].params.path_smooth.inner_enabled =
                source_methods[i].params.path_smooth.inner_enabled;
            methods[i].params.path_smooth.outer_enabled =
                source_methods[i].params.path_smooth.outer_enabled;
            break;
        case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
            methods[i].params.corridor_roughen.strength =
                source_methods[i].params.corridor_roughen.strength;
            methods[i].params.corridor_roughen.max_depth =
                source_methods[i].params.corridor_roughen.max_depth;
            methods[i].params.corridor_roughen.mode =
                (int)source_methods[i].params.corridor_roughen.mode;
            break;
        default:
            free(methods);
            return DG_STATUS_INVALID_ARGUMENT;
        }
    }

    *out_methods = methods;
    return DG_STATUS_OK;
}

static dg_status_t dg_copy_room_type_definitions_to_snapshot(
    dg_snapshot_room_type_definition_t **out_definitions,
    size_t definition_count,
    const dg_room_type_definition_t *source_definitions
)
{
    size_t i;
    dg_snapshot_room_type_definition_t *definitions;

    if (out_definitions == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_definitions = NULL;
    if (definition_count == 0) {
        return DG_STATUS_OK;
    }

    if (source_definitions == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (definition_count > (SIZE_MAX / sizeof(*definitions))) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    definitions = (dg_snapshot_room_type_definition_t *)malloc(
        definition_count * sizeof(*definitions)
    );
    if (definitions == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (i = 0; i < definition_count; ++i) {
        definitions[i].type_id = source_definitions[i].type_id;
        definitions[i].enabled = source_definitions[i].enabled;
        definitions[i].min_count = source_definitions[i].min_count;
        definitions[i].max_count = source_definitions[i].max_count;
        definitions[i].target_count = source_definitions[i].target_count;
        definitions[i].constraints.area_min = source_definitions[i].constraints.area_min;
        definitions[i].constraints.area_max = source_definitions[i].constraints.area_max;
        definitions[i].constraints.degree_min = source_definitions[i].constraints.degree_min;
        definitions[i].constraints.degree_max = source_definitions[i].constraints.degree_max;
        definitions[i].constraints.border_distance_min =
            source_definitions[i].constraints.border_distance_min;
        definitions[i].constraints.border_distance_max =
            source_definitions[i].constraints.border_distance_max;
        definitions[i].constraints.graph_depth_min = source_definitions[i].constraints.graph_depth_min;
        definitions[i].constraints.graph_depth_max = source_definitions[i].constraints.graph_depth_max;
        definitions[i].preferences.weight = source_definitions[i].preferences.weight;
        definitions[i].preferences.larger_room_bias =
            source_definitions[i].preferences.larger_room_bias;
        definitions[i].preferences.higher_degree_bias =
            source_definitions[i].preferences.higher_degree_bias;
        definitions[i].preferences.border_distance_bias =
            source_definitions[i].preferences.border_distance_bias;
    }

    *out_definitions = definitions;
    return DG_STATUS_OK;
}

dg_status_t dg_snapshot_generation_request(
    const dg_generate_request_t *request,
    dg_map_t *map
)
{
    dg_generation_request_snapshot_t snapshot;
    dg_status_t status;

    if (request == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    snapshot = (dg_generation_request_snapshot_t){0};
    snapshot.width = request->width;
    snapshot.height = request->height;
    snapshot.seed = request->seed;
    snapshot.algorithm_id = (int)request->algorithm;
    snapshot.process.enabled = request->process.enabled;
    snapshot.room_types.policy.strict_mode = request->room_types.policy.strict_mode;
    snapshot.room_types.policy.allow_untyped_rooms = request->room_types.policy.allow_untyped_rooms;
    snapshot.room_types.policy.default_type_id = request->room_types.policy.default_type_id;

    switch (request->algorithm) {
    case DG_ALGORITHM_BSP_TREE:
        snapshot.params.bsp.min_rooms = request->params.bsp.min_rooms;
        snapshot.params.bsp.max_rooms = request->params.bsp.max_rooms;
        snapshot.params.bsp.room_min_size = request->params.bsp.room_min_size;
        snapshot.params.bsp.room_max_size = request->params.bsp.room_max_size;
        break;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        snapshot.params.drunkards_walk.wiggle_percent = request->params.drunkards_walk.wiggle_percent;
        break;
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        snapshot.params.cellular_automata.initial_wall_percent =
            request->params.cellular_automata.initial_wall_percent;
        snapshot.params.cellular_automata.simulation_steps =
            request->params.cellular_automata.simulation_steps;
        snapshot.params.cellular_automata.wall_threshold =
            request->params.cellular_automata.wall_threshold;
        break;
    case DG_ALGORITHM_VALUE_NOISE:
        snapshot.params.value_noise.feature_size = request->params.value_noise.feature_size;
        snapshot.params.value_noise.octaves = request->params.value_noise.octaves;
        snapshot.params.value_noise.persistence_percent =
            request->params.value_noise.persistence_percent;
        snapshot.params.value_noise.floor_threshold_percent =
            request->params.value_noise.floor_threshold_percent;
        break;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        snapshot.params.rooms_and_mazes.min_rooms = request->params.rooms_and_mazes.min_rooms;
        snapshot.params.rooms_and_mazes.max_rooms = request->params.rooms_and_mazes.max_rooms;
        snapshot.params.rooms_and_mazes.room_min_size = request->params.rooms_and_mazes.room_min_size;
        snapshot.params.rooms_and_mazes.room_max_size = request->params.rooms_and_mazes.room_max_size;
        snapshot.params.rooms_and_mazes.maze_wiggle_percent =
            request->params.rooms_and_mazes.maze_wiggle_percent;
        snapshot.params.rooms_and_mazes.min_room_connections =
            request->params.rooms_and_mazes.min_room_connections;
        snapshot.params.rooms_and_mazes.max_room_connections =
            request->params.rooms_and_mazes.max_room_connections;
        snapshot.params.rooms_and_mazes.ensure_full_connectivity =
            request->params.rooms_and_mazes.ensure_full_connectivity;
        snapshot.params.rooms_and_mazes.dead_end_prune_steps =
            request->params.rooms_and_mazes.dead_end_prune_steps;
        break;
    case DG_ALGORITHM_ROOM_GRAPH:
        snapshot.params.room_graph.min_rooms = request->params.room_graph.min_rooms;
        snapshot.params.room_graph.max_rooms = request->params.room_graph.max_rooms;
        snapshot.params.room_graph.room_min_size = request->params.room_graph.room_min_size;
        snapshot.params.room_graph.room_max_size = request->params.room_graph.room_max_size;
        snapshot.params.room_graph.neighbor_candidates =
            request->params.room_graph.neighbor_candidates;
        snapshot.params.room_graph.extra_connection_chance_percent =
            request->params.room_graph.extra_connection_chance_percent;
        break;
    case DG_ALGORITHM_WORM_CAVES:
        snapshot.params.worm_caves.worm_count = request->params.worm_caves.worm_count;
        snapshot.params.worm_caves.wiggle_percent = request->params.worm_caves.wiggle_percent;
        snapshot.params.worm_caves.branch_chance_percent =
            request->params.worm_caves.branch_chance_percent;
        snapshot.params.worm_caves.target_floor_percent =
            request->params.worm_caves.target_floor_percent;
        snapshot.params.worm_caves.brush_radius = request->params.worm_caves.brush_radius;
        snapshot.params.worm_caves.max_steps_per_worm =
            request->params.worm_caves.max_steps_per_worm;
        snapshot.params.worm_caves.ensure_connected = request->params.worm_caves.ensure_connected;
        break;
    case DG_ALGORITHM_SIMPLEX_NOISE:
        snapshot.params.simplex_noise.feature_size = request->params.simplex_noise.feature_size;
        snapshot.params.simplex_noise.octaves = request->params.simplex_noise.octaves;
        snapshot.params.simplex_noise.persistence_percent =
            request->params.simplex_noise.persistence_percent;
        snapshot.params.simplex_noise.floor_threshold_percent =
            request->params.simplex_noise.floor_threshold_percent;
        snapshot.params.simplex_noise.ensure_connected =
            request->params.simplex_noise.ensure_connected;
        break;
    default:
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_copy_room_type_definitions_to_snapshot(
        &snapshot.room_types.definitions,
        request->room_types.definition_count,
        request->room_types.definitions
    );
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_copy_process_methods_to_snapshot(
        &snapshot.process.methods,
        request->process.method_count,
        request->process.methods
    );
    if (status != DG_STATUS_OK) {
        free(snapshot.room_types.definitions);
        return status;
    }

    snapshot.process.method_count = request->process.method_count;
    snapshot.room_types.definition_count = request->room_types.definition_count;
    snapshot.present = 1;

    free(map->metadata.generation_request.process.methods);
    free(map->metadata.generation_request.room_types.definitions);
    map->metadata.generation_request = snapshot;
    return DG_STATUS_OK;
}
