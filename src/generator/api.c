#include "internal.h"

#include <stdlib.h>
#include <string.h>

static bool dg_nonnegative_range_is_valid(int min_value, int max_value)
{
    if (min_value < 0) {
        return false;
    }

    if (max_value == -1) {
        return true;
    }

    return max_value >= min_value;
}

static bool dg_bias_is_valid(int value)
{
    return value >= -100 && value <= 100;
}

static dg_status_t dg_validate_room_type_definition(const dg_room_type_definition_t *definition)
{
    if (definition == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (definition->enabled != 0 && definition->enabled != 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (definition->min_count < 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (definition->max_count != -1 && definition->max_count < definition->min_count) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (definition->target_count != -1) {
        if (definition->target_count < definition->min_count) {
            return DG_STATUS_INVALID_ARGUMENT;
        }
        if (definition->max_count != -1 && definition->target_count > definition->max_count) {
            return DG_STATUS_INVALID_ARGUMENT;
        }
    }

    if (!dg_nonnegative_range_is_valid(
            definition->constraints.area_min,
            definition->constraints.area_max
        )) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (!dg_nonnegative_range_is_valid(
            definition->constraints.degree_min,
            definition->constraints.degree_max
        )) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (!dg_nonnegative_range_is_valid(
            definition->constraints.border_distance_min,
            definition->constraints.border_distance_max
        )) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (!dg_nonnegative_range_is_valid(
            definition->constraints.graph_depth_min,
            definition->constraints.graph_depth_max
        )) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (definition->preferences.weight < 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (!dg_bias_is_valid(definition->preferences.larger_room_bias) ||
        !dg_bias_is_valid(definition->preferences.higher_degree_bias) ||
        !dg_bias_is_valid(definition->preferences.border_distance_bias)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_validate_room_type_assignment_config(
    const dg_room_type_assignment_config_t *config
)
{
    size_t i;
    size_t j;
    size_t enabled_count;
    bool has_default_type;

    if (config == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->definitions == NULL && config->definition_count > 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->policy.strict_mode != 0 && config->policy.strict_mode != 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->policy.allow_untyped_rooms != 0 && config->policy.allow_untyped_rooms != 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    enabled_count = 0;
    has_default_type = false;
    for (i = 0; i < config->definition_count; ++i) {
        dg_status_t status;
        const dg_room_type_definition_t *current = &config->definitions[i];

        status = dg_validate_room_type_definition(current);
        if (status != DG_STATUS_OK) {
            return status;
        }

        if (current->enabled == 1) {
            enabled_count += 1;
            if (current->type_id == config->policy.default_type_id) {
                has_default_type = true;
            }
        }

        for (j = i + 1; j < config->definition_count; ++j) {
            if (current->type_id == config->definitions[j].type_id) {
                return DG_STATUS_INVALID_ARGUMENT;
            }
        }
    }

    if (config->policy.allow_untyped_rooms == 0) {
        if (enabled_count == 0) {
            return DG_STATUS_INVALID_ARGUMENT;
        }
        if (!has_default_type) {
            return DG_STATUS_INVALID_ARGUMENT;
        }
    }

    return DG_STATUS_OK;
}

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

static dg_status_t dg_validate_process_config(const dg_process_config_t *config)
{
    size_t i;

    if (config == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->method_count > 0 && config->methods == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (i = 0; i < config->method_count; ++i) {
        const dg_process_method_t *method = &config->methods[i];

        switch (method->type) {
        case DG_PROCESS_METHOD_SCALE:
            if (method->params.scale.factor < 1) {
                return DG_STATUS_INVALID_ARGUMENT;
            }
            break;
        case DG_PROCESS_METHOD_ROOM_SHAPE:
            if (method->params.room_shape.mode != DG_ROOM_SHAPE_RECTANGULAR &&
                method->params.room_shape.mode != DG_ROOM_SHAPE_ORGANIC) {
                return DG_STATUS_INVALID_ARGUMENT;
            }
            if (method->params.room_shape.organicity < 0 ||
                method->params.room_shape.organicity > 100) {
                return DG_STATUS_INVALID_ARGUMENT;
            }
            break;
        case DG_PROCESS_METHOD_PATH_SMOOTH:
            if (method->params.path_smooth.strength < 0 ||
                method->params.path_smooth.strength > 12) {
                return DG_STATUS_INVALID_ARGUMENT;
            }
            break;
        default:
            return DG_STATUS_INVALID_ARGUMENT;
        }
    }

    return DG_STATUS_OK;
}

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
        case DG_PROCESS_METHOD_ROOM_SHAPE:
            methods[i].params.room_shape.mode = (int)source_methods[i].params.room_shape.mode;
            methods[i].params.room_shape.organicity = source_methods[i].params.room_shape.organicity;
            break;
        case DG_PROCESS_METHOD_PATH_SMOOTH:
            methods[i].params.path_smooth.strength = source_methods[i].params.path_smooth.strength;
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

static dg_status_t dg_snapshot_generation_request(
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
    config->maze_wiggle_percent = 40;
    config->min_room_connections = 1;
    config->max_room_connections = 1;
    config->ensure_full_connectivity = 1;
    config->dead_end_prune_steps = -1;
}

void dg_default_room_type_constraints(dg_room_type_constraints_t *constraints)
{
    if (constraints == NULL) {
        return;
    }

    constraints->area_min = 0;
    constraints->area_max = -1;
    constraints->degree_min = 0;
    constraints->degree_max = -1;
    constraints->border_distance_min = 0;
    constraints->border_distance_max = -1;
    constraints->graph_depth_min = 0;
    constraints->graph_depth_max = -1;
}

void dg_default_room_type_preferences(dg_room_type_preferences_t *preferences)
{
    if (preferences == NULL) {
        return;
    }

    preferences->weight = 1;
    preferences->larger_room_bias = 0;
    preferences->higher_degree_bias = 0;
    preferences->border_distance_bias = 0;
}

void dg_default_room_type_definition(dg_room_type_definition_t *definition, uint32_t type_id)
{
    if (definition == NULL) {
        return;
    }

    memset(definition, 0, sizeof(*definition));
    definition->type_id = type_id;
    definition->enabled = 1;
    definition->min_count = 0;
    definition->max_count = -1;
    definition->target_count = -1;
    dg_default_room_type_constraints(&definition->constraints);
    dg_default_room_type_preferences(&definition->preferences);
}

void dg_default_room_type_assignment_policy(dg_room_type_assignment_policy_t *policy)
{
    if (policy == NULL) {
        return;
    }

    policy->strict_mode = 0;
    policy->allow_untyped_rooms = 1;
    policy->default_type_id = 0u;
}

void dg_default_room_type_assignment_config(dg_room_type_assignment_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->definitions = NULL;
    config->definition_count = 0;
    dg_default_room_type_assignment_policy(&config->policy);
}

void dg_default_process_method(dg_process_method_t *method, dg_process_method_type_t type)
{
    if (method == NULL) {
        return;
    }

    memset(method, 0, sizeof(*method));
    method->type = type;
    switch (type) {
    case DG_PROCESS_METHOD_SCALE:
        method->params.scale.factor = 2;
        break;
    case DG_PROCESS_METHOD_ROOM_SHAPE:
        method->params.room_shape.mode = DG_ROOM_SHAPE_ORGANIC;
        method->params.room_shape.organicity = 45;
        break;
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        method->params.path_smooth.strength = 2;
        break;
    default:
        method->type = DG_PROCESS_METHOD_SCALE;
        method->params.scale.factor = 2;
        break;
    }
}

void dg_default_process_config(dg_process_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->methods = NULL;
    config->method_count = 0;
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
    dg_default_process_config(&request->process);
    dg_default_room_type_assignment_config(&request->room_types);

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

    status = dg_validate_room_type_assignment_config(&request->room_types);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_validate_process_config(&request->process);
    if (status != DG_STATUS_OK) {
        return status;
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

    status = dg_apply_post_processes(request, &generated, &rng);
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

    status = dg_apply_room_type_assignment(request, &generated, &rng);
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
