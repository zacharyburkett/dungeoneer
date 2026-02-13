#include "internal.h"

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

static dg_status_t dg_validate_cellular_automata_config(
    const dg_cellular_automata_config_t *config
)
{
    if (config == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->initial_wall_percent < 0 || config->initial_wall_percent > 100) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->simulation_steps < 1 || config->simulation_steps > 12) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->wall_threshold < 0 || config->wall_threshold > 8) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_validate_value_noise_config(const dg_value_noise_config_t *config)
{
    if (config == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->feature_size < 2 || config->feature_size > 64) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->octaves < 1 || config->octaves > 6) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->persistence_percent < 10 || config->persistence_percent > 90) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->floor_threshold_percent < 0 || config->floor_threshold_percent > 100) {
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

static dg_status_t dg_validate_room_graph_config(const dg_room_graph_config_t *config)
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

    if (config->neighbor_candidates < 1 || config->neighbor_candidates > 8) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->extra_connection_chance_percent < 0 ||
        config->extra_connection_chance_percent > 100) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_validate_worm_caves_config(const dg_worm_caves_config_t *config)
{
    if (config == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->worm_count < 1 || config->worm_count > 128) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->wiggle_percent < 0 || config->wiggle_percent > 100) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->branch_chance_percent < 0 || config->branch_chance_percent > 100) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->target_floor_percent < 5 || config->target_floor_percent > 90) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->brush_radius < 0 || config->brush_radius > 3) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->max_steps_per_worm < 8 || config->max_steps_per_worm > 20000) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->ensure_connected != 0 && config->ensure_connected != 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_validate_simplex_noise_config(const dg_simplex_noise_config_t *config)
{
    if (config == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->feature_size < 2 || config->feature_size > 128) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->octaves < 1 || config->octaves > 8) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->persistence_percent < 10 || config->persistence_percent > 90) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->floor_threshold_percent < 0 || config->floor_threshold_percent > 100) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (config->ensure_connected != 0 && config->ensure_connected != 1) {
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

    if (config->enabled != 0 && config->enabled != 1) {
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
                method->params.room_shape.mode != DG_ROOM_SHAPE_ORGANIC &&
                method->params.room_shape.mode != DG_ROOM_SHAPE_CELLULAR &&
                method->params.room_shape.mode != DG_ROOM_SHAPE_CHAMFERED) {
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
            if ((method->params.path_smooth.inner_enabled != 0 &&
                 method->params.path_smooth.inner_enabled != 1) ||
                (method->params.path_smooth.outer_enabled != 0 &&
                 method->params.path_smooth.outer_enabled != 1)) {
                return DG_STATUS_INVALID_ARGUMENT;
            }
            break;
        case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
            if (method->params.corridor_roughen.strength < 0 ||
                method->params.corridor_roughen.strength > 100) {
                return DG_STATUS_INVALID_ARGUMENT;
            }
            if (method->params.corridor_roughen.max_depth < 1 ||
                method->params.corridor_roughen.max_depth > 32) {
                return DG_STATUS_INVALID_ARGUMENT;
            }
            if (method->params.corridor_roughen.mode != DG_CORRIDOR_ROUGHEN_UNIFORM &&
                method->params.corridor_roughen.mode != DG_CORRIDOR_ROUGHEN_ORGANIC) {
                return DG_STATUS_INVALID_ARGUMENT;
            }
            break;
        default:
            return DG_STATUS_INVALID_ARGUMENT;
        }
    }

    return DG_STATUS_OK;
}

dg_status_t dg_validate_generate_request(const dg_generate_request_t *request)
{
    dg_status_t status;

    if (request == NULL) {
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
        return dg_validate_bsp_config(&request->params.bsp);
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        return dg_validate_rooms_and_mazes_config(&request->params.rooms_and_mazes);
    case DG_ALGORITHM_DRUNKARDS_WALK:
        return dg_validate_drunkards_walk_config(&request->params.drunkards_walk);
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        return dg_validate_cellular_automata_config(&request->params.cellular_automata);
    case DG_ALGORITHM_VALUE_NOISE:
        return dg_validate_value_noise_config(&request->params.value_noise);
    case DG_ALGORITHM_ROOM_GRAPH:
        return dg_validate_room_graph_config(&request->params.room_graph);
    case DG_ALGORITHM_WORM_CAVES:
        return dg_validate_worm_caves_config(&request->params.worm_caves);
    case DG_ALGORITHM_SIMPLEX_NOISE:
        return dg_validate_simplex_noise_config(&request->params.simplex_noise);
    default:
        return DG_STATUS_INVALID_ARGUMENT;
    }
}
