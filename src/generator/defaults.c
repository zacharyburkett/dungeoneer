#include "internal.h"

#include <string.h>

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

void dg_default_cellular_automata_config(dg_cellular_automata_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->initial_wall_percent = 47;
    config->simulation_steps = 5;
    config->wall_threshold = 5;
}

void dg_default_value_noise_config(dg_value_noise_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->feature_size = 12;
    config->octaves = 3;
    config->persistence_percent = 55;
    config->floor_threshold_percent = 48;
}

void dg_default_room_graph_config(dg_room_graph_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->min_rooms = 10;
    config->max_rooms = 20;
    config->room_min_size = 4;
    config->room_max_size = 11;
    config->neighbor_candidates = 3;
    config->extra_connection_chance_percent = 20;
}

void dg_default_worm_caves_config(dg_worm_caves_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->worm_count = 6;
    config->wiggle_percent = 55;
    config->branch_chance_percent = 7;
    config->target_floor_percent = 34;
    config->brush_radius = 0;
    config->max_steps_per_worm = 900;
    config->ensure_connected = 1;
}

void dg_default_simplex_noise_config(dg_simplex_noise_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->feature_size = 14;
    config->octaves = 4;
    config->persistence_percent = 55;
    config->floor_threshold_percent = 50;
    config->ensure_connected = 1;
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
    dg_default_map_edge_opening_query(&definition->template_opening_query);
    definition->template_required_opening_matches = 0;
    dg_default_room_type_constraints(&definition->constraints);
    dg_default_room_type_preferences(&definition->preferences);
}

void dg_default_edge_opening_config(dg_edge_opening_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->openings = NULL;
    config->opening_count = 0;
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
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        method->params.path_smooth.strength = 2;
        method->params.path_smooth.inner_enabled = 1;
        method->params.path_smooth.outer_enabled = 1;
        break;
    case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
        method->params.corridor_roughen.strength = 40;
        method->params.corridor_roughen.max_depth = 1;
        method->params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_ORGANIC;
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

    config->enabled = 1;
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
    dg_default_edge_opening_config(&request->edge_openings);
    dg_default_process_config(&request->process);
    dg_default_room_type_assignment_config(&request->room_types);

    switch (algorithm) {
    case DG_ALGORITHM_VALUE_NOISE:
        dg_default_value_noise_config(&request->params.value_noise);
        break;
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        dg_default_cellular_automata_config(&request->params.cellular_automata);
        break;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        dg_default_rooms_and_mazes_config(&request->params.rooms_and_mazes);
        break;
    case DG_ALGORITHM_ROOM_GRAPH:
        dg_default_room_graph_config(&request->params.room_graph);
        break;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        dg_default_drunkards_walk_config(&request->params.drunkards_walk);
        break;
    case DG_ALGORITHM_WORM_CAVES:
        dg_default_worm_caves_config(&request->params.worm_caves);
        break;
    case DG_ALGORITHM_SIMPLEX_NOISE:
        dg_default_simplex_noise_config(&request->params.simplex_noise);
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
    case DG_ALGORITHM_ROOM_GRAPH:
        return DG_MAP_GENERATION_CLASS_ROOM_LIKE;
    case DG_ALGORITHM_DRUNKARDS_WALK:
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
    case DG_ALGORITHM_VALUE_NOISE:
    case DG_ALGORITHM_WORM_CAVES:
    case DG_ALGORITHM_SIMPLEX_NOISE:
        return DG_MAP_GENERATION_CLASS_CAVE_LIKE;
    default:
        return DG_MAP_GENERATION_CLASS_UNKNOWN;
    }
}
