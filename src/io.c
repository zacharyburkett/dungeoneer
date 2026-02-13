#include "dungeoneer/io.h"
#include "dungeoneer/generator.h"

#include <limits.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char DG_CONFIG_MAGIC[4] = {'D', 'G', 'C', 'F'};

static bool dg_mul_size_would_overflow(size_t a, size_t b, size_t *out)
{
    if (out == NULL) {
        return true;
    }

    if (a == 0 || b == 0) {
        *out = 0;
        return false;
    }

    if (a > (SIZE_MAX / b)) {
        return true;
    }

    *out = a * b;
    return false;
}

static bool dg_u64_to_size_checked(uint64_t value, size_t *out_value)
{
    if (out_value == NULL) {
        return false;
    }

    if (value > (uint64_t)SIZE_MAX) {
        return false;
    }

    *out_value = (size_t)value;
    return true;
}

static bool dg_i32_to_int_checked(int32_t value, int *out_value)
{
    if (out_value == NULL) {
        return false;
    }

    if (value > INT_MAX || value < INT_MIN) {
        return false;
    }

    *out_value = (int)value;
    return true;
}

static bool dg_map_is_empty(const dg_map_t *map)
{
    if (map == NULL) {
        return false;
    }

    return map->tiles == NULL &&
           map->metadata.rooms == NULL &&
           map->metadata.corridors == NULL &&
           map->metadata.room_adjacency == NULL &&
           map->metadata.room_neighbors == NULL &&
           map->metadata.diagnostics.process_steps == NULL &&
           map->metadata.diagnostics.room_type_quotas == NULL &&
           map->metadata.generation_request.process.methods == NULL &&
           map->metadata.generation_request.room_types.definitions == NULL;
}

static dg_status_t dg_write_exact(FILE *file, const void *data, size_t byte_count)
{
    if (file == NULL || (byte_count > 0 && data == NULL)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (byte_count == 0) {
        return DG_STATUS_OK;
    }

    if (fwrite(data, 1, byte_count, file) != byte_count) {
        return DG_STATUS_IO_ERROR;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_read_exact(FILE *file, void *data, size_t byte_count)
{
    if (file == NULL || (byte_count > 0 && data == NULL)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (byte_count == 0) {
        return DG_STATUS_OK;
    }

    if (fread(data, 1, byte_count, file) != byte_count) {
        return DG_STATUS_IO_ERROR;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24) & 0xFFu);
    return dg_write_exact(file, bytes, sizeof(bytes));
}

static dg_status_t dg_write_i32(FILE *file, int32_t value)
{
    return dg_write_u32(file, (uint32_t)value);
}

static dg_status_t dg_write_u64(FILE *file, uint64_t value)
{
    unsigned char bytes[8];

    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24) & 0xFFu);
    bytes[4] = (unsigned char)((value >> 32) & 0xFFu);
    bytes[5] = (unsigned char)((value >> 40) & 0xFFu);
    bytes[6] = (unsigned char)((value >> 48) & 0xFFu);
    bytes[7] = (unsigned char)((value >> 56) & 0xFFu);
    return dg_write_exact(file, bytes, sizeof(bytes));
}

static dg_status_t dg_write_size(FILE *file, size_t value)
{
    return dg_write_u64(file, (uint64_t)value);
}

static dg_status_t dg_read_u32(FILE *file, uint32_t *out_value)
{
    unsigned char bytes[4];
    dg_status_t status;

    if (out_value == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_read_exact(file, bytes, sizeof(bytes));
    if (status != DG_STATUS_OK) {
        return status;
    }

    *out_value = ((uint32_t)bytes[0]) |
                 (((uint32_t)bytes[1]) << 8) |
                 (((uint32_t)bytes[2]) << 16) |
                 (((uint32_t)bytes[3]) << 24);
    return DG_STATUS_OK;
}

static dg_status_t dg_read_i32(FILE *file, int32_t *out_value)
{
    uint32_t raw;
    dg_status_t status;

    if (out_value == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_read_u32(file, &raw);
    if (status != DG_STATUS_OK) {
        return status;
    }

    *out_value = (int32_t)raw;
    return DG_STATUS_OK;
}

static dg_status_t dg_read_u64(FILE *file, uint64_t *out_value)
{
    unsigned char bytes[8];
    dg_status_t status;

    if (out_value == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_read_exact(file, bytes, sizeof(bytes));
    if (status != DG_STATUS_OK) {
        return status;
    }

    *out_value = ((uint64_t)bytes[0]) |
                 (((uint64_t)bytes[1]) << 8) |
                 (((uint64_t)bytes[2]) << 16) |
                 (((uint64_t)bytes[3]) << 24) |
                 (((uint64_t)bytes[4]) << 32) |
                 (((uint64_t)bytes[5]) << 40) |
                 (((uint64_t)bytes[6]) << 48) |
                 (((uint64_t)bytes[7]) << 56);
    return DG_STATUS_OK;
}

static dg_status_t dg_read_size(FILE *file, size_t *out_value)
{
    uint64_t raw;
    dg_status_t status;

    if (out_value == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_read_u64(file, &raw);
    if (status != DG_STATUS_OK) {
        return status;
    }

    if (!dg_u64_to_size_checked(raw, out_value)) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    return DG_STATUS_OK;
}

static bool dg_algorithm_id_value_is_valid(int32_t value)
{
    return value == (int32_t)DG_ALGORITHM_BSP_TREE ||
           value == (int32_t)DG_ALGORITHM_DRUNKARDS_WALK ||
           value == (int32_t)DG_ALGORITHM_ROOMS_AND_MAZES ||
           value == (int32_t)DG_ALGORITHM_CELLULAR_AUTOMATA ||
           value == (int32_t)DG_ALGORITHM_VALUE_NOISE ||
           value == (int32_t)DG_ALGORITHM_ROOM_GRAPH ||
           value == (int32_t)DG_ALGORITHM_WORM_CAVES ||
           value == (int32_t)DG_ALGORITHM_SIMPLEX_NOISE;
}

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

static bool dg_snapshot_process_method_is_valid(const dg_snapshot_process_method_t *method)
{
    if (method == NULL) {
        return false;
    }

    switch ((dg_process_method_type_t)method->type) {
    case DG_PROCESS_METHOD_SCALE:
        return method->params.scale.factor >= 1;
    case DG_PROCESS_METHOD_ROOM_SHAPE:
        return (method->params.room_shape.mode == (int)DG_ROOM_SHAPE_RECTANGULAR ||
                method->params.room_shape.mode == (int)DG_ROOM_SHAPE_ORGANIC ||
                method->params.room_shape.mode == (int)DG_ROOM_SHAPE_CELLULAR ||
                method->params.room_shape.mode == (int)DG_ROOM_SHAPE_CHAMFERED) &&
               method->params.room_shape.organicity >= 0 &&
               method->params.room_shape.organicity <= 100;
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        return method->params.path_smooth.strength >= 0 &&
               method->params.path_smooth.strength <= 12 &&
               (method->params.path_smooth.inner_enabled == 0 ||
                method->params.path_smooth.inner_enabled == 1) &&
               (method->params.path_smooth.outer_enabled == 0 ||
                method->params.path_smooth.outer_enabled == 1);
    case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
        return method->params.corridor_roughen.strength >= 0 &&
               method->params.corridor_roughen.strength <= 100 &&
               method->params.corridor_roughen.max_depth >= 1 &&
               method->params.corridor_roughen.max_depth <= 32 &&
               (method->params.corridor_roughen.mode == (int)DG_CORRIDOR_ROUGHEN_UNIFORM ||
                method->params.corridor_roughen.mode == (int)DG_CORRIDOR_ROUGHEN_ORGANIC);
    default:
        return false;
    }
}

static bool dg_snapshot_process_config_is_valid(const dg_snapshot_process_config_t *process)
{
    size_t i;

    if (process == NULL) {
        return false;
    }

    if (process->method_count > 0 && process->methods == NULL) {
        return false;
    }

    if (process->enabled != 0 && process->enabled != 1) {
        return false;
    }

    for (i = 0; i < process->method_count; ++i) {
        if (!dg_snapshot_process_method_is_valid(&process->methods[i])) {
            return false;
        }
    }

    return true;
}

static bool dg_snapshot_room_type_config_is_valid(
    const dg_snapshot_room_type_assignment_config_t *room_types
)
{
    size_t i;

    if (room_types == NULL) {
        return false;
    }

    if (room_types->definition_count > 0 && room_types->definitions == NULL) {
        return false;
    }

    if ((room_types->policy.strict_mode != 0 && room_types->policy.strict_mode != 1) ||
        (room_types->policy.allow_untyped_rooms != 0 &&
         room_types->policy.allow_untyped_rooms != 1)) {
        return false;
    }

    for (i = 0; i < room_types->definition_count; ++i) {
        const dg_snapshot_room_type_definition_t *definition = &room_types->definitions[i];

        if (definition->enabled != 0 && definition->enabled != 1) {
            return false;
        }

        if (definition->min_count < 0) {
            return false;
        }

        if (definition->max_count != -1 && definition->max_count < definition->min_count) {
            return false;
        }

        if (definition->target_count != -1) {
            if (definition->target_count < definition->min_count) {
                return false;
            }
            if (definition->max_count != -1 && definition->target_count > definition->max_count) {
                return false;
            }
        }

        if (!dg_nonnegative_range_is_valid(
                definition->constraints.area_min,
                definition->constraints.area_max
            ) ||
            !dg_nonnegative_range_is_valid(
                definition->constraints.degree_min,
                definition->constraints.degree_max
            ) ||
            !dg_nonnegative_range_is_valid(
                definition->constraints.border_distance_min,
                definition->constraints.border_distance_max
            ) ||
            !dg_nonnegative_range_is_valid(
                definition->constraints.graph_depth_min,
                definition->constraints.graph_depth_max
            )) {
            return false;
        }

        if (definition->preferences.weight < 0 ||
            !dg_bias_is_valid(definition->preferences.larger_room_bias) ||
            !dg_bias_is_valid(definition->preferences.higher_degree_bias) ||
            !dg_bias_is_valid(definition->preferences.border_distance_bias)) {
            return false;
        }
    }

    return true;
}

static bool dg_snapshot_algorithm_params_are_valid(const dg_generation_request_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }

    switch ((dg_algorithm_t)snapshot->algorithm_id) {
    case DG_ALGORITHM_BSP_TREE:
        return snapshot->params.bsp.min_rooms >= 1 &&
               snapshot->params.bsp.max_rooms >= snapshot->params.bsp.min_rooms &&
               snapshot->params.bsp.room_min_size >= 3 &&
               snapshot->params.bsp.room_max_size >= snapshot->params.bsp.room_min_size;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        return snapshot->params.drunkards_walk.wiggle_percent >= 0 &&
               snapshot->params.drunkards_walk.wiggle_percent <= 100;
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        return snapshot->params.cellular_automata.initial_wall_percent >= 0 &&
               snapshot->params.cellular_automata.initial_wall_percent <= 100 &&
               snapshot->params.cellular_automata.simulation_steps >= 1 &&
               snapshot->params.cellular_automata.simulation_steps <= 12 &&
               snapshot->params.cellular_automata.wall_threshold >= 0 &&
               snapshot->params.cellular_automata.wall_threshold <= 8;
    case DG_ALGORITHM_VALUE_NOISE:
        return snapshot->params.value_noise.feature_size >= 2 &&
               snapshot->params.value_noise.feature_size <= 64 &&
               snapshot->params.value_noise.octaves >= 1 &&
               snapshot->params.value_noise.octaves <= 6 &&
               snapshot->params.value_noise.persistence_percent >= 10 &&
               snapshot->params.value_noise.persistence_percent <= 90 &&
               snapshot->params.value_noise.floor_threshold_percent >= 0 &&
               snapshot->params.value_noise.floor_threshold_percent <= 100;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        return snapshot->params.rooms_and_mazes.min_rooms >= 1 &&
               snapshot->params.rooms_and_mazes.max_rooms >=
                   snapshot->params.rooms_and_mazes.min_rooms &&
               snapshot->params.rooms_and_mazes.room_min_size >= 3 &&
               snapshot->params.rooms_and_mazes.room_max_size >=
                   snapshot->params.rooms_and_mazes.room_min_size &&
               snapshot->params.rooms_and_mazes.maze_wiggle_percent >= 0 &&
               snapshot->params.rooms_and_mazes.maze_wiggle_percent <= 100 &&
               snapshot->params.rooms_and_mazes.min_room_connections >= 1 &&
               snapshot->params.rooms_and_mazes.max_room_connections >=
                   snapshot->params.rooms_and_mazes.min_room_connections &&
               (snapshot->params.rooms_and_mazes.ensure_full_connectivity == 0 ||
                snapshot->params.rooms_and_mazes.ensure_full_connectivity == 1) &&
               snapshot->params.rooms_and_mazes.dead_end_prune_steps >= -1;
    case DG_ALGORITHM_ROOM_GRAPH:
        return snapshot->params.room_graph.min_rooms >= 1 &&
               snapshot->params.room_graph.max_rooms >= snapshot->params.room_graph.min_rooms &&
               snapshot->params.room_graph.room_min_size >= 3 &&
               snapshot->params.room_graph.room_max_size >=
                   snapshot->params.room_graph.room_min_size &&
               snapshot->params.room_graph.neighbor_candidates >= 1 &&
               snapshot->params.room_graph.neighbor_candidates <= 8 &&
               snapshot->params.room_graph.extra_connection_chance_percent >= 0 &&
               snapshot->params.room_graph.extra_connection_chance_percent <= 100;
    case DG_ALGORITHM_WORM_CAVES:
        return snapshot->params.worm_caves.worm_count >= 1 &&
               snapshot->params.worm_caves.worm_count <= 128 &&
               snapshot->params.worm_caves.wiggle_percent >= 0 &&
               snapshot->params.worm_caves.wiggle_percent <= 100 &&
               snapshot->params.worm_caves.branch_chance_percent >= 0 &&
               snapshot->params.worm_caves.branch_chance_percent <= 100 &&
               snapshot->params.worm_caves.target_floor_percent >= 5 &&
               snapshot->params.worm_caves.target_floor_percent <= 90 &&
               snapshot->params.worm_caves.brush_radius >= 0 &&
               snapshot->params.worm_caves.brush_radius <= 3 &&
               snapshot->params.worm_caves.max_steps_per_worm >= 8 &&
               snapshot->params.worm_caves.max_steps_per_worm <= 20000 &&
               (snapshot->params.worm_caves.ensure_connected == 0 ||
                snapshot->params.worm_caves.ensure_connected == 1);
    case DG_ALGORITHM_SIMPLEX_NOISE:
        return snapshot->params.simplex_noise.feature_size >= 2 &&
               snapshot->params.simplex_noise.feature_size <= 128 &&
               snapshot->params.simplex_noise.octaves >= 1 &&
               snapshot->params.simplex_noise.octaves <= 8 &&
               snapshot->params.simplex_noise.persistence_percent >= 10 &&
               snapshot->params.simplex_noise.persistence_percent <= 90 &&
               snapshot->params.simplex_noise.floor_threshold_percent >= 0 &&
               snapshot->params.simplex_noise.floor_threshold_percent <= 100 &&
               (snapshot->params.simplex_noise.ensure_connected == 0 ||
                snapshot->params.simplex_noise.ensure_connected == 1);
    default:
        return false;
    }
}

static bool dg_snapshot_is_valid(const dg_generation_request_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }

    if (snapshot->present != 1) {
        return false;
    }

    if (snapshot->width < 1 || snapshot->height < 1) {
        return false;
    }

    if (!dg_algorithm_id_value_is_valid((int32_t)snapshot->algorithm_id)) {
        return false;
    }

    return dg_snapshot_algorithm_params_are_valid(snapshot) &&
           dg_snapshot_process_config_is_valid(&snapshot->process) &&
           dg_snapshot_room_type_config_is_valid(&snapshot->room_types);
}

static dg_status_t dg_allocate_array(void **out_ptr, size_t count, size_t element_size)
{
    size_t byte_count;
    void *allocated;

    if (out_ptr == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_ptr = NULL;
    if (count == 0) {
        return DG_STATUS_OK;
    }

    if (dg_mul_size_would_overflow(count, element_size, &byte_count)) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    allocated = malloc(byte_count);
    if (allocated == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    *out_ptr = allocated;
    return DG_STATUS_OK;
}

static void dg_snapshot_clear(dg_generation_request_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    free(snapshot->process.methods);
    free(snapshot->room_types.definitions);
    *snapshot = (dg_generation_request_snapshot_t){0};
}

static dg_status_t dg_write_snapshot_algorithm_params(
    FILE *file,
    const dg_generation_request_snapshot_t *snapshot
)
{
    dg_status_t status;

    if (file == NULL || snapshot == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    switch ((dg_algorithm_t)snapshot->algorithm_id) {
    case DG_ALGORITHM_BSP_TREE:
        status = dg_write_i32(file, (int32_t)snapshot->params.bsp.min_rooms);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.bsp.max_rooms);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.bsp.room_min_size);
        if (status != DG_STATUS_OK) {
            return status;
        }
        return dg_write_i32(file, (int32_t)snapshot->params.bsp.room_max_size);
    case DG_ALGORITHM_DRUNKARDS_WALK:
        return dg_write_i32(file, (int32_t)snapshot->params.drunkards_walk.wiggle_percent);
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.cellular_automata.initial_wall_percent
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.cellular_automata.simulation_steps
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        return dg_write_i32(file, (int32_t)snapshot->params.cellular_automata.wall_threshold);
    case DG_ALGORITHM_VALUE_NOISE:
        status = dg_write_i32(file, (int32_t)snapshot->params.value_noise.feature_size);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.value_noise.octaves);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.value_noise.persistence_percent);
        if (status != DG_STATUS_OK) {
            return status;
        }
        return dg_write_i32(
            file,
            (int32_t)snapshot->params.value_noise.floor_threshold_percent
        );
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        status = dg_write_i32(file, (int32_t)snapshot->params.rooms_and_mazes.min_rooms);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.rooms_and_mazes.max_rooms);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.rooms_and_mazes.room_min_size);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.rooms_and_mazes.room_max_size);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.rooms_and_mazes.maze_wiggle_percent
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.rooms_and_mazes.min_room_connections
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.rooms_and_mazes.max_room_connections
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.rooms_and_mazes.ensure_full_connectivity
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        return dg_write_i32(
            file,
            (int32_t)snapshot->params.rooms_and_mazes.dead_end_prune_steps
        );
    case DG_ALGORITHM_ROOM_GRAPH:
        status = dg_write_i32(file, (int32_t)snapshot->params.room_graph.min_rooms);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.room_graph.max_rooms);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.room_graph.room_min_size);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.room_graph.room_max_size);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.room_graph.neighbor_candidates);
        if (status != DG_STATUS_OK) {
            return status;
        }
        return dg_write_i32(
            file,
            (int32_t)snapshot->params.room_graph.extra_connection_chance_percent
        );
    case DG_ALGORITHM_WORM_CAVES:
        status = dg_write_i32(file, (int32_t)snapshot->params.worm_caves.worm_count);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.worm_caves.wiggle_percent);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.worm_caves.branch_chance_percent
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.worm_caves.target_floor_percent
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.worm_caves.brush_radius);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.worm_caves.max_steps_per_worm
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        return dg_write_i32(file, (int32_t)snapshot->params.worm_caves.ensure_connected);
    case DG_ALGORITHM_SIMPLEX_NOISE:
        status = dg_write_i32(file, (int32_t)snapshot->params.simplex_noise.feature_size);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)snapshot->params.simplex_noise.octaves);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.simplex_noise.persistence_percent
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(
            file,
            (int32_t)snapshot->params.simplex_noise.floor_threshold_percent
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        return dg_write_i32(file, (int32_t)snapshot->params.simplex_noise.ensure_connected);
    default:
        return DG_STATUS_INVALID_ARGUMENT;
    }
}

static dg_status_t dg_write_snapshot(FILE *file, const dg_generation_request_snapshot_t *snapshot)
{
    dg_status_t status;
    size_t i;

    if (file == NULL || snapshot == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_write_exact(file, DG_CONFIG_MAGIC, sizeof(DG_CONFIG_MAGIC));
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_write_i32(file, (int32_t)snapshot->width);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_write_i32(file, (int32_t)snapshot->height);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_write_u64(file, snapshot->seed);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_write_i32(file, (int32_t)snapshot->algorithm_id);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_write_snapshot_algorithm_params(file, snapshot);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_write_i32(file, (int32_t)snapshot->process.enabled);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_write_size(file, snapshot->process.method_count);
    if (status != DG_STATUS_OK) {
        return status;
    }

    for (i = 0; i < snapshot->process.method_count; ++i) {
        const dg_snapshot_process_method_t *method = &snapshot->process.methods[i];

        status = dg_write_i32(file, (int32_t)method->type);
        if (status != DG_STATUS_OK) {
            return status;
        }

        switch ((dg_process_method_type_t)method->type) {
        case DG_PROCESS_METHOD_SCALE:
            status = dg_write_i32(file, (int32_t)method->params.scale.factor);
            break;
        case DG_PROCESS_METHOD_ROOM_SHAPE:
            status = dg_write_i32(file, (int32_t)method->params.room_shape.mode);
            if (status != DG_STATUS_OK) {
                return status;
            }
            status = dg_write_i32(file, (int32_t)method->params.room_shape.organicity);
            break;
        case DG_PROCESS_METHOD_PATH_SMOOTH:
            status = dg_write_i32(file, (int32_t)method->params.path_smooth.strength);
            if (status != DG_STATUS_OK) {
                return status;
            }
            status = dg_write_i32(file, (int32_t)method->params.path_smooth.inner_enabled);
            if (status != DG_STATUS_OK) {
                return status;
            }
            status = dg_write_i32(file, (int32_t)method->params.path_smooth.outer_enabled);
            break;
        case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
            status = dg_write_i32(file, (int32_t)method->params.corridor_roughen.strength);
            if (status != DG_STATUS_OK) {
                return status;
            }
            status = dg_write_i32(file, (int32_t)method->params.corridor_roughen.max_depth);
            if (status != DG_STATUS_OK) {
                return status;
            }
            status = dg_write_i32(file, (int32_t)method->params.corridor_roughen.mode);
            break;
        default:
            return DG_STATUS_INVALID_ARGUMENT;
        }

        if (status != DG_STATUS_OK) {
            return status;
        }
    }

    status = dg_write_size(file, snapshot->room_types.definition_count);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_write_i32(file, (int32_t)snapshot->room_types.policy.strict_mode);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_write_i32(file, (int32_t)snapshot->room_types.policy.allow_untyped_rooms);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_write_u32(file, snapshot->room_types.policy.default_type_id);
    if (status != DG_STATUS_OK) {
        return status;
    }

    for (i = 0; i < snapshot->room_types.definition_count; ++i) {
        const dg_snapshot_room_type_definition_t *definition = &snapshot->room_types.definitions[i];

        status = dg_write_u32(file, definition->type_id);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->enabled);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->min_count);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->max_count);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->target_count);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->constraints.area_min);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->constraints.area_max);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->constraints.degree_min);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->constraints.degree_max);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->constraints.border_distance_min);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->constraints.border_distance_max);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->constraints.graph_depth_min);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->constraints.graph_depth_max);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->preferences.weight);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->preferences.larger_room_bias);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->preferences.higher_degree_bias);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_write_i32(file, (int32_t)definition->preferences.border_distance_bias);
        if (status != DG_STATUS_OK) {
            return status;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_read_snapshot_algorithm_params(
    FILE *file,
    dg_generation_request_snapshot_t *snapshot
)
{
    int32_t value;
    dg_status_t status;

    if (file == NULL || snapshot == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    switch ((dg_algorithm_t)snapshot->algorithm_id) {
    case DG_ALGORITHM_BSP_TREE:
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(value, &snapshot->params.bsp.min_rooms)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(value, &snapshot->params.bsp.max_rooms)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.bsp.room_min_size)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.bsp.room_max_size)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        return DG_STATUS_OK;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.drunkards_walk.wiggle_percent)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        return DG_STATUS_OK;
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.cellular_automata.initial_wall_percent
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.cellular_automata.simulation_steps
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.cellular_automata.wall_threshold
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        return DG_STATUS_OK;
    case DG_ALGORITHM_VALUE_NOISE:
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.value_noise.feature_size)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.value_noise.octaves)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.value_noise.persistence_percent
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.value_noise.floor_threshold_percent
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        return DG_STATUS_OK;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.rooms_and_mazes.min_rooms
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.rooms_and_mazes.max_rooms
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.rooms_and_mazes.room_min_size
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.rooms_and_mazes.room_max_size
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.rooms_and_mazes.maze_wiggle_percent
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.rooms_and_mazes.min_room_connections
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.rooms_and_mazes.max_room_connections
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.rooms_and_mazes.ensure_full_connectivity
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.rooms_and_mazes.dead_end_prune_steps
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        return DG_STATUS_OK;
    case DG_ALGORITHM_ROOM_GRAPH:
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.room_graph.min_rooms)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.room_graph.max_rooms)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.room_graph.room_min_size)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.room_graph.room_max_size)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.room_graph.neighbor_candidates)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.room_graph.extra_connection_chance_percent
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        return DG_STATUS_OK;
    case DG_ALGORITHM_WORM_CAVES:
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.worm_caves.worm_count)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.worm_caves.wiggle_percent)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.worm_caves.branch_chance_percent
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.worm_caves.target_floor_percent
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.worm_caves.brush_radius)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.worm_caves.max_steps_per_worm
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.worm_caves.ensure_connected
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        return DG_STATUS_OK;
    case DG_ALGORITHM_SIMPLEX_NOISE:
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.simplex_noise.feature_size)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value, &snapshot->params.simplex_noise.octaves)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.simplex_noise.persistence_percent
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.simplex_noise.floor_threshold_percent
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value,
                &snapshot->params.simplex_noise.ensure_connected
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        return DG_STATUS_OK;
    default:
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }
}

static dg_status_t dg_read_snapshot(FILE *file, dg_generation_request_snapshot_t *snapshot)
{
    unsigned char magic[sizeof(DG_CONFIG_MAGIC)];
    int32_t value_i32;
    dg_status_t status;
    size_t i;

    if (file == NULL || snapshot == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *snapshot = (dg_generation_request_snapshot_t){0};
    snapshot->present = 1;

    status = dg_read_exact(file, magic, sizeof(magic));
    if (status != DG_STATUS_OK) {
        return status;
    }

    if (memcmp(magic, DG_CONFIG_MAGIC, sizeof(DG_CONFIG_MAGIC)) != 0) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    status = dg_read_i32(file, &value_i32);
    if (status != DG_STATUS_OK || !dg_i32_to_int_checked(value_i32, &snapshot->width)) {
        return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
    }

    status = dg_read_i32(file, &value_i32);
    if (status != DG_STATUS_OK || !dg_i32_to_int_checked(value_i32, &snapshot->height)) {
        return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
    }

    status = dg_read_u64(file, &snapshot->seed);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_read_i32(file, &value_i32);
    if (status != DG_STATUS_OK || !dg_i32_to_int_checked(value_i32, &snapshot->algorithm_id)) {
        return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
    }

    status = dg_read_snapshot_algorithm_params(file, snapshot);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_read_i32(file, &value_i32);
    if (status != DG_STATUS_OK ||
        !dg_i32_to_int_checked(value_i32, &snapshot->process.enabled)) {
        return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
    }

    status = dg_read_size(file, &snapshot->process.method_count);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_allocate_array(
        (void **)&snapshot->process.methods,
        snapshot->process.method_count,
        sizeof(dg_snapshot_process_method_t)
    );
    if (status != DG_STATUS_OK) {
        return status;
    }

    for (i = 0; i < snapshot->process.method_count; ++i) {
        dg_snapshot_process_method_t *method = &snapshot->process.methods[i];

        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(value_i32, &method->type)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }

        switch ((dg_process_method_type_t)method->type) {
        case DG_PROCESS_METHOD_SCALE:
            status = dg_read_i32(file, &value_i32);
            if (status != DG_STATUS_OK ||
                !dg_i32_to_int_checked(value_i32, &method->params.scale.factor)) {
                return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
            }
            break;
        case DG_PROCESS_METHOD_ROOM_SHAPE:
            status = dg_read_i32(file, &value_i32);
            if (status != DG_STATUS_OK ||
                !dg_i32_to_int_checked(value_i32, &method->params.room_shape.mode)) {
                return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
            }
            status = dg_read_i32(file, &value_i32);
            if (status != DG_STATUS_OK ||
                !dg_i32_to_int_checked(value_i32, &method->params.room_shape.organicity)) {
                return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
            }
            break;
        case DG_PROCESS_METHOD_PATH_SMOOTH:
            status = dg_read_i32(file, &value_i32);
            if (status != DG_STATUS_OK ||
                !dg_i32_to_int_checked(value_i32, &method->params.path_smooth.strength)) {
                return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
            }
            status = dg_read_i32(file, &value_i32);
            if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                    value_i32,
                    &method->params.path_smooth.inner_enabled
                )) {
                return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
            }
            status = dg_read_i32(file, &value_i32);
            if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                    value_i32,
                    &method->params.path_smooth.outer_enabled
                )) {
                return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
            }
            break;
        case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
            status = dg_read_i32(file, &value_i32);
            if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                    value_i32,
                    &method->params.corridor_roughen.strength
                )) {
                return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
            }
            status = dg_read_i32(file, &value_i32);
            if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                    value_i32,
                    &method->params.corridor_roughen.max_depth
                )) {
                return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
            }
            status = dg_read_i32(file, &value_i32);
            if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                    value_i32,
                    &method->params.corridor_roughen.mode
                )) {
                return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
            }
            break;
        default:
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
    }

    status = dg_read_size(file, &snapshot->room_types.definition_count);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_read_i32(file, &value_i32);
    if (status != DG_STATUS_OK ||
        !dg_i32_to_int_checked(value_i32, &snapshot->room_types.policy.strict_mode)) {
        return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
    }

    status = dg_read_i32(file, &value_i32);
    if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
            value_i32,
            &snapshot->room_types.policy.allow_untyped_rooms
        )) {
        return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
    }

    status = dg_read_u32(file, &snapshot->room_types.policy.default_type_id);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_allocate_array(
        (void **)&snapshot->room_types.definitions,
        snapshot->room_types.definition_count,
        sizeof(dg_snapshot_room_type_definition_t)
    );
    if (status != DG_STATUS_OK) {
        return status;
    }

    for (i = 0; i < snapshot->room_types.definition_count; ++i) {
        dg_snapshot_room_type_definition_t *definition = &snapshot->room_types.definitions[i];

        status = dg_read_u32(file, &definition->type_id);
        if (status != DG_STATUS_OK) {
            return status;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(value_i32, &definition->enabled)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(value_i32, &definition->min_count)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(value_i32, &definition->max_count)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(value_i32, &definition->target_count)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->constraints.area_min
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->constraints.area_max
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->constraints.degree_min
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->constraints.degree_max
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->constraints.border_distance_min
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->constraints.border_distance_max
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->constraints.graph_depth_min
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->constraints.graph_depth_max
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK ||
            !dg_i32_to_int_checked(value_i32, &definition->preferences.weight)) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->preferences.larger_room_bias
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->preferences.higher_degree_bias
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
        status = dg_read_i32(file, &value_i32);
        if (status != DG_STATUS_OK || !dg_i32_to_int_checked(
                value_i32,
                &definition->preferences.border_distance_bias
            )) {
            return (status != DG_STATUS_OK) ? status : DG_STATUS_UNSUPPORTED_FORMAT;
        }
    }

    if (!dg_snapshot_is_valid(snapshot)) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_validate_map_for_save(const dg_map_t *map)
{
    if (map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.generation_request.present != 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (!dg_snapshot_is_valid(&map->metadata.generation_request)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_build_request_from_snapshot(
    const dg_generation_request_snapshot_t *snapshot,
    dg_generate_request_t *out_request,
    dg_process_method_t **out_process_methods,
    dg_room_type_definition_t **out_room_type_definitions
)
{
    dg_generate_request_t request;
    dg_process_method_t *process_methods;
    dg_room_type_definition_t *room_type_definitions;
    dg_status_t status;
    size_t i;

    if (snapshot == NULL || out_request == NULL ||
        out_process_methods == NULL || out_room_type_definitions == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_process_methods = NULL;
    *out_room_type_definitions = NULL;

    if (!dg_snapshot_is_valid(snapshot)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_default_generate_request(
        &request,
        (dg_algorithm_t)snapshot->algorithm_id,
        snapshot->width,
        snapshot->height,
        snapshot->seed
    );

    switch ((dg_algorithm_t)snapshot->algorithm_id) {
    case DG_ALGORITHM_BSP_TREE:
        request.params.bsp.min_rooms = snapshot->params.bsp.min_rooms;
        request.params.bsp.max_rooms = snapshot->params.bsp.max_rooms;
        request.params.bsp.room_min_size = snapshot->params.bsp.room_min_size;
        request.params.bsp.room_max_size = snapshot->params.bsp.room_max_size;
        break;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        request.params.drunkards_walk.wiggle_percent = snapshot->params.drunkards_walk.wiggle_percent;
        break;
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        request.params.cellular_automata.initial_wall_percent =
            snapshot->params.cellular_automata.initial_wall_percent;
        request.params.cellular_automata.simulation_steps =
            snapshot->params.cellular_automata.simulation_steps;
        request.params.cellular_automata.wall_threshold =
            snapshot->params.cellular_automata.wall_threshold;
        break;
    case DG_ALGORITHM_VALUE_NOISE:
        request.params.value_noise.feature_size = snapshot->params.value_noise.feature_size;
        request.params.value_noise.octaves = snapshot->params.value_noise.octaves;
        request.params.value_noise.persistence_percent =
            snapshot->params.value_noise.persistence_percent;
        request.params.value_noise.floor_threshold_percent =
            snapshot->params.value_noise.floor_threshold_percent;
        break;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        request.params.rooms_and_mazes.min_rooms = snapshot->params.rooms_and_mazes.min_rooms;
        request.params.rooms_and_mazes.max_rooms = snapshot->params.rooms_and_mazes.max_rooms;
        request.params.rooms_and_mazes.room_min_size = snapshot->params.rooms_and_mazes.room_min_size;
        request.params.rooms_and_mazes.room_max_size = snapshot->params.rooms_and_mazes.room_max_size;
        request.params.rooms_and_mazes.maze_wiggle_percent =
            snapshot->params.rooms_and_mazes.maze_wiggle_percent;
        request.params.rooms_and_mazes.min_room_connections =
            snapshot->params.rooms_and_mazes.min_room_connections;
        request.params.rooms_and_mazes.max_room_connections =
            snapshot->params.rooms_and_mazes.max_room_connections;
        request.params.rooms_and_mazes.ensure_full_connectivity =
            snapshot->params.rooms_and_mazes.ensure_full_connectivity;
        request.params.rooms_and_mazes.dead_end_prune_steps =
            snapshot->params.rooms_and_mazes.dead_end_prune_steps;
        break;
    case DG_ALGORITHM_ROOM_GRAPH:
        request.params.room_graph.min_rooms = snapshot->params.room_graph.min_rooms;
        request.params.room_graph.max_rooms = snapshot->params.room_graph.max_rooms;
        request.params.room_graph.room_min_size = snapshot->params.room_graph.room_min_size;
        request.params.room_graph.room_max_size = snapshot->params.room_graph.room_max_size;
        request.params.room_graph.neighbor_candidates =
            snapshot->params.room_graph.neighbor_candidates;
        request.params.room_graph.extra_connection_chance_percent =
            snapshot->params.room_graph.extra_connection_chance_percent;
        break;
    case DG_ALGORITHM_WORM_CAVES:
        request.params.worm_caves.worm_count = snapshot->params.worm_caves.worm_count;
        request.params.worm_caves.wiggle_percent = snapshot->params.worm_caves.wiggle_percent;
        request.params.worm_caves.branch_chance_percent =
            snapshot->params.worm_caves.branch_chance_percent;
        request.params.worm_caves.target_floor_percent =
            snapshot->params.worm_caves.target_floor_percent;
        request.params.worm_caves.brush_radius = snapshot->params.worm_caves.brush_radius;
        request.params.worm_caves.max_steps_per_worm =
            snapshot->params.worm_caves.max_steps_per_worm;
        request.params.worm_caves.ensure_connected = snapshot->params.worm_caves.ensure_connected;
        break;
    case DG_ALGORITHM_SIMPLEX_NOISE:
        request.params.simplex_noise.feature_size = snapshot->params.simplex_noise.feature_size;
        request.params.simplex_noise.octaves = snapshot->params.simplex_noise.octaves;
        request.params.simplex_noise.persistence_percent =
            snapshot->params.simplex_noise.persistence_percent;
        request.params.simplex_noise.floor_threshold_percent =
            snapshot->params.simplex_noise.floor_threshold_percent;
        request.params.simplex_noise.ensure_connected =
            snapshot->params.simplex_noise.ensure_connected;
        break;
    default:
        return DG_STATUS_INVALID_ARGUMENT;
    }

    process_methods = NULL;
    status = dg_allocate_array(
        (void **)&process_methods,
        snapshot->process.method_count,
        sizeof(dg_process_method_t)
    );
    if (status != DG_STATUS_OK) {
        return status;
    }

    for (i = 0; i < snapshot->process.method_count; ++i) {
        process_methods[i].type = (dg_process_method_type_t)snapshot->process.methods[i].type;
        switch (process_methods[i].type) {
        case DG_PROCESS_METHOD_SCALE:
            process_methods[i].params.scale.factor = snapshot->process.methods[i].params.scale.factor;
            break;
        case DG_PROCESS_METHOD_ROOM_SHAPE:
            process_methods[i].params.room_shape.mode =
                (dg_room_shape_mode_t)snapshot->process.methods[i].params.room_shape.mode;
            process_methods[i].params.room_shape.organicity =
                snapshot->process.methods[i].params.room_shape.organicity;
            break;
        case DG_PROCESS_METHOD_PATH_SMOOTH:
            process_methods[i].params.path_smooth.strength =
                snapshot->process.methods[i].params.path_smooth.strength;
            process_methods[i].params.path_smooth.inner_enabled =
                snapshot->process.methods[i].params.path_smooth.inner_enabled;
            process_methods[i].params.path_smooth.outer_enabled =
                snapshot->process.methods[i].params.path_smooth.outer_enabled;
            break;
        case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
            process_methods[i].params.corridor_roughen.strength =
                snapshot->process.methods[i].params.corridor_roughen.strength;
            process_methods[i].params.corridor_roughen.max_depth =
                snapshot->process.methods[i].params.corridor_roughen.max_depth;
            process_methods[i].params.corridor_roughen.mode =
                (dg_corridor_roughen_mode_t)snapshot->process.methods[i].params.corridor_roughen.mode;
            break;
        default:
            free(process_methods);
            return DG_STATUS_INVALID_ARGUMENT;
        }
    }

    request.process.methods = process_methods;
    request.process.enabled = snapshot->process.enabled;
    request.process.method_count = snapshot->process.method_count;

    room_type_definitions = NULL;
    status = dg_allocate_array(
        (void **)&room_type_definitions,
        snapshot->room_types.definition_count,
        sizeof(dg_room_type_definition_t)
    );
    if (status != DG_STATUS_OK) {
        free(process_methods);
        return status;
    }

    for (i = 0; i < snapshot->room_types.definition_count; ++i) {
        room_type_definitions[i].type_id = snapshot->room_types.definitions[i].type_id;
        room_type_definitions[i].enabled = snapshot->room_types.definitions[i].enabled;
        room_type_definitions[i].min_count = snapshot->room_types.definitions[i].min_count;
        room_type_definitions[i].max_count = snapshot->room_types.definitions[i].max_count;
        room_type_definitions[i].target_count = snapshot->room_types.definitions[i].target_count;

        room_type_definitions[i].constraints.area_min =
            snapshot->room_types.definitions[i].constraints.area_min;
        room_type_definitions[i].constraints.area_max =
            snapshot->room_types.definitions[i].constraints.area_max;
        room_type_definitions[i].constraints.degree_min =
            snapshot->room_types.definitions[i].constraints.degree_min;
        room_type_definitions[i].constraints.degree_max =
            snapshot->room_types.definitions[i].constraints.degree_max;
        room_type_definitions[i].constraints.border_distance_min =
            snapshot->room_types.definitions[i].constraints.border_distance_min;
        room_type_definitions[i].constraints.border_distance_max =
            snapshot->room_types.definitions[i].constraints.border_distance_max;
        room_type_definitions[i].constraints.graph_depth_min =
            snapshot->room_types.definitions[i].constraints.graph_depth_min;
        room_type_definitions[i].constraints.graph_depth_max =
            snapshot->room_types.definitions[i].constraints.graph_depth_max;

        room_type_definitions[i].preferences.weight =
            snapshot->room_types.definitions[i].preferences.weight;
        room_type_definitions[i].preferences.larger_room_bias =
            snapshot->room_types.definitions[i].preferences.larger_room_bias;
        room_type_definitions[i].preferences.higher_degree_bias =
            snapshot->room_types.definitions[i].preferences.higher_degree_bias;
        room_type_definitions[i].preferences.border_distance_bias =
            snapshot->room_types.definitions[i].preferences.border_distance_bias;
    }

    request.room_types.definitions = room_type_definitions;
    request.room_types.definition_count = snapshot->room_types.definition_count;
    request.room_types.policy.strict_mode = snapshot->room_types.policy.strict_mode;
    request.room_types.policy.allow_untyped_rooms = snapshot->room_types.policy.allow_untyped_rooms;
    request.room_types.policy.default_type_id = snapshot->room_types.policy.default_type_id;

    *out_request = request;
    *out_process_methods = process_methods;
    *out_room_type_definitions = room_type_definitions;
    return DG_STATUS_OK;
}

dg_status_t dg_map_save_file(const dg_map_t *map, const char *path)
{
    FILE *file;
    dg_status_t status;

    if (path == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_validate_map_for_save(map);
    if (status != DG_STATUS_OK) {
        return status;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        return DG_STATUS_IO_ERROR;
    }

    status = dg_write_snapshot(file, &map->metadata.generation_request);
    if (status != DG_STATUS_OK) {
        (void)fclose(file);
        return status;
    }

    if (fclose(file) != 0) {
        return DG_STATUS_IO_ERROR;
    }

    return DG_STATUS_OK;
}

dg_status_t dg_map_load_file(const char *path, dg_map_t *out_map)
{
    FILE *file;
    dg_generation_request_snapshot_t snapshot;
    dg_generate_request_t request;
    dg_process_method_t *process_methods;
    dg_room_type_definition_t *room_type_definitions;
    dg_status_t status;

    if (path == NULL || out_map == NULL || !dg_map_is_empty(out_map)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return DG_STATUS_IO_ERROR;
    }

    snapshot = (dg_generation_request_snapshot_t){0};
    status = dg_read_snapshot(file, &snapshot);
    if (fclose(file) != 0 && status == DG_STATUS_OK) {
        dg_snapshot_clear(&snapshot);
        return DG_STATUS_IO_ERROR;
    }
    if (status != DG_STATUS_OK) {
        dg_snapshot_clear(&snapshot);
        return status;
    }

    process_methods = NULL;
    room_type_definitions = NULL;
    status = dg_build_request_from_snapshot(
        &snapshot,
        &request,
        &process_methods,
        &room_type_definitions
    );
    dg_snapshot_clear(&snapshot);
    if (status != DG_STATUS_OK) {
        free(process_methods);
        free(room_type_definitions);
        return status;
    }

    status = dg_generate(&request, out_map);
    free(process_methods);
    free(room_type_definitions);
    return status;
}

typedef struct dg_export_tile_legend_entry {
    dg_tile_t tile;
    const char *name;
    unsigned char rgba[4];
} dg_export_tile_legend_entry_t;

static const dg_export_tile_legend_entry_t DG_EXPORT_TILE_LEGEND[] = {
    {DG_TILE_VOID, "void", {0u, 0u, 0u, 0u}},
    {DG_TILE_WALL, "wall", {24u, 28u, 34u, 255u}},
    {DG_TILE_FLOOR, "floor", {232u, 232u, 228u, 255u}},
    {DG_TILE_DOOR, "door", {208u, 156u, 66u, 255u}}
};

typedef struct dg_export_room_type_palette_entry {
    uint32_t type_id;
    size_t room_count;
    size_t tile_count;
    unsigned char rgba[4];
} dg_export_room_type_palette_entry_t;

static void dg_export_color_for_room_type(uint32_t type_id, unsigned char rgba[4])
{
    uint32_t hash;

    if (rgba == NULL) {
        return;
    }

    hash = type_id * 2654435761u;
    rgba[0] = (unsigned char)(80u + ((hash >> 0) & 0x5Fu));
    rgba[1] = (unsigned char)(95u + ((hash >> 8) & 0x5Fu));
    rgba[2] = (unsigned char)(105u + ((hash >> 16) & 0x5Fu));
    rgba[3] = 255u;
}

static size_t dg_export_find_room_type_palette_entry(
    const dg_export_room_type_palette_entry_t *entries,
    size_t entry_count,
    uint32_t type_id
)
{
    size_t i;

    if (entries == NULL) {
        return SIZE_MAX;
    }

    for (i = 0; i < entry_count; ++i) {
        if (entries[i].type_id == type_id) {
            return i;
        }
    }

    return SIZE_MAX;
}

static void dg_export_base_tile_to_rgba(dg_tile_t tile, unsigned char rgba[4])
{
    size_t i;

    if (rgba == NULL) {
        return;
    }

    for (i = 0; i < sizeof(DG_EXPORT_TILE_LEGEND) / sizeof(DG_EXPORT_TILE_LEGEND[0]); ++i) {
        if (DG_EXPORT_TILE_LEGEND[i].tile == tile) {
            rgba[0] = DG_EXPORT_TILE_LEGEND[i].rgba[0];
            rgba[1] = DG_EXPORT_TILE_LEGEND[i].rgba[1];
            rgba[2] = DG_EXPORT_TILE_LEGEND[i].rgba[2];
            rgba[3] = DG_EXPORT_TILE_LEGEND[i].rgba[3];
            return;
        }
    }

    rgba[0] = 255u;
    rgba[1] = 0u;
    rgba[2] = 255u;
    rgba[3] = 255u;
}

static dg_status_t dg_export_build_room_type_overlay(
    const dg_map_t *map,
    int **out_room_index_by_tile,
    dg_export_room_type_palette_entry_t **out_palette_entries,
    size_t *out_palette_count
)
{
    size_t cell_count;
    int *room_index_by_tile;
    dg_export_room_type_palette_entry_t *palette_entries;
    size_t palette_count;
    size_t i;

    if (map == NULL || out_room_index_by_tile == NULL || out_palette_entries == NULL ||
        out_palette_count == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_room_index_by_tile = NULL;
    *out_palette_entries = NULL;
    *out_palette_count = 0u;

    if (map->width <= 0 || map->height <= 0 || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.generation_class != DG_MAP_GENERATION_CLASS_ROOM_LIKE ||
        map->metadata.room_count == 0 || map->metadata.rooms == NULL) {
        return DG_STATUS_OK;
    }

    if (dg_mul_size_would_overflow((size_t)map->width, (size_t)map->height, &cell_count)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    room_index_by_tile = (int *)malloc(cell_count * sizeof(*room_index_by_tile));
    if (room_index_by_tile == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    for (i = 0; i < cell_count; ++i) {
        room_index_by_tile[i] = -1;
    }

    palette_entries = NULL;
    palette_count = 0u;
    if (map->metadata.room_count > 0) {
        if (map->metadata.room_count > (SIZE_MAX / sizeof(*palette_entries))) {
            free(room_index_by_tile);
            return DG_STATUS_ALLOCATION_FAILED;
        }
        palette_entries = (dg_export_room_type_palette_entry_t *)calloc(
            map->metadata.room_count,
            sizeof(*palette_entries)
        );
        if (palette_entries == NULL) {
            free(room_index_by_tile);
            return DG_STATUS_ALLOCATION_FAILED;
        }
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map->metadata.rooms[i];
        size_t entry_index;

        if (room->type_id == DG_ROOM_TYPE_UNASSIGNED) {
            continue;
        }

        entry_index = dg_export_find_room_type_palette_entry(
            palette_entries,
            palette_count,
            room->type_id
        );
        if (entry_index == SIZE_MAX) {
            entry_index = palette_count;
            palette_entries[entry_index].type_id = room->type_id;
            dg_export_color_for_room_type(room->type_id, palette_entries[entry_index].rgba);
            palette_count += 1u;
        }
        palette_entries[entry_index].room_count += 1u;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map->metadata.rooms[i];
        int x0;
        int y0;
        int x1;
        int y1;
        int x;
        int y;

        if (room->type_id == DG_ROOM_TYPE_UNASSIGNED) {
            continue;
        }

        x0 = room->bounds.x;
        y0 = room->bounds.y;
        x1 = room->bounds.x + room->bounds.width;
        y1 = room->bounds.y + room->bounds.height;
        if (x0 < 0) {
            x0 = 0;
        }
        if (y0 < 0) {
            y0 = 0;
        }
        if (x1 > map->width) {
            x1 = map->width;
        }
        if (y1 > map->height) {
            y1 = map->height;
        }
        if (x0 >= x1 || y0 >= y1) {
            continue;
        }

        for (y = y0; y < y1; ++y) {
            for (x = x0; x < x1; ++x) {
                size_t tile_index = (size_t)y * (size_t)map->width + (size_t)x;
                if (map->tiles[tile_index] != DG_TILE_FLOOR) {
                    continue;
                }
                if (room_index_by_tile[tile_index] < 0) {
                    room_index_by_tile[tile_index] = (int)i;
                }
            }
        }
    }

    for (i = 0; i < cell_count; ++i) {
        int room_index = room_index_by_tile[i];
        size_t entry_index;
        uint32_t type_id;
        if (room_index < 0 || (size_t)room_index >= map->metadata.room_count) {
            continue;
        }

        type_id = map->metadata.rooms[room_index].type_id;
        if (type_id == DG_ROOM_TYPE_UNASSIGNED) {
            continue;
        }

        entry_index = dg_export_find_room_type_palette_entry(palette_entries, palette_count, type_id);
        if (entry_index != SIZE_MAX) {
            palette_entries[entry_index].tile_count += 1u;
        }
    }

    if (palette_count > 1u) {
        size_t left;
        for (left = 0; left + 1u < palette_count; ++left) {
            size_t right;
            for (right = left + 1u; right < palette_count; ++right) {
                if (palette_entries[right].type_id < palette_entries[left].type_id) {
                    dg_export_room_type_palette_entry_t tmp = palette_entries[left];
                    palette_entries[left] = palette_entries[right];
                    palette_entries[right] = tmp;
                }
            }
        }
    }

    if (palette_count == 0u) {
        free(palette_entries);
        palette_entries = NULL;
    }

    *out_room_index_by_tile = room_index_by_tile;
    *out_palette_entries = palette_entries;
    *out_palette_count = palette_count;
    return DG_STATUS_OK;
}

static void dg_export_tile_to_rgba(
    const dg_map_t *map,
    size_t tile_index,
    const int *room_index_by_tile,
    unsigned char rgba[4]
)
{
    dg_tile_t tile;

    if (map == NULL || rgba == NULL || map->tiles == NULL) {
        return;
    }

    tile = map->tiles[tile_index];
    if (tile == DG_TILE_FLOOR && room_index_by_tile != NULL) {
        int room_index = room_index_by_tile[tile_index];
        if (room_index >= 0 && (size_t)room_index < map->metadata.room_count) {
            uint32_t room_type_id = map->metadata.rooms[room_index].type_id;
            if (room_type_id != DG_ROOM_TYPE_UNASSIGNED) {
                dg_export_color_for_room_type(room_type_id, rgba);
                return;
            }
        }
    }

    dg_export_base_tile_to_rgba(tile, rgba);
}

static dg_status_t dg_export_build_png_pixels(
    const dg_map_t *map,
    const int *room_index_by_tile,
    unsigned char **out_pixels,
    size_t *out_size
)
{
    size_t row_bytes;
    size_t pixel_size;
    unsigned char *pixels;
    int y;
    int x;

    if (map == NULL || out_pixels == NULL || out_size == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_pixels = NULL;
    *out_size = 0u;
    if (map->width <= 0 || map->height <= 0 || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (dg_mul_size_would_overflow((size_t)map->width, 4u, &row_bytes) ||
        dg_mul_size_would_overflow((size_t)map->height, row_bytes, &pixel_size)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    pixels = (unsigned char *)malloc(pixel_size);
    if (pixels == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (y = 0; y < map->height; ++y) {
        for (x = 0; x < map->width; ++x) {
            size_t tile_index = (size_t)y * (size_t)map->width + (size_t)x;
            size_t pixel_offset = tile_index * 4u;
            unsigned char rgba[4];

            dg_export_tile_to_rgba(map, tile_index, room_index_by_tile, rgba);
            pixels[pixel_offset + 0u] = rgba[0];
            pixels[pixel_offset + 1u] = rgba[1];
            pixels[pixel_offset + 2u] = rgba[2];
            pixels[pixel_offset + 3u] = rgba[3];
        }
    }

    *out_pixels = pixels;
    *out_size = pixel_size;
    return DG_STATUS_OK;
}

static dg_status_t dg_export_write_png_file(
    const dg_map_t *map,
    const char *png_path,
    const int *room_index_by_tile
)
{
    FILE *file;
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *rows;
    unsigned char *pixels;
    size_t row_bytes;
    size_t pixel_size;
    dg_status_t status;
    int y;

    if (map == NULL || png_path == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }
    if (map->width <= 0 || map->height <= 0 || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }
    if (map->width > INT32_MAX || map->height > INT32_MAX) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_export_build_png_pixels(map, room_index_by_tile, &pixels, &pixel_size);
    if (status != DG_STATUS_OK) {
        return status;
    }

    if (dg_mul_size_would_overflow((size_t)map->width, 4u, &row_bytes)) {
        free(pixels);
        return DG_STATUS_INVALID_ARGUMENT;
    }
    {
        size_t expected_size;
        if (dg_mul_size_would_overflow((size_t)map->height, row_bytes, &expected_size) ||
            expected_size != pixel_size) {
            free(pixels);
            return DG_STATUS_INVALID_ARGUMENT;
        }
    }

    rows = (png_bytep *)malloc((size_t)map->height * sizeof(*rows));
    if (rows == NULL) {
        free(pixels);
        return DG_STATUS_ALLOCATION_FAILED;
    }
    for (y = 0; y < map->height; ++y) {
        rows[y] = (png_bytep)(pixels + ((size_t)y * row_bytes));
    }

    file = fopen(png_path, "wb");
    if (file == NULL) {
        free(rows);
        free(pixels);
        return DG_STATUS_IO_ERROR;
    }

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        (void)fclose(file);
        free(rows);
        free(pixels);
        return DG_STATUS_IO_ERROR;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_write_struct(&png_ptr, NULL);
        (void)fclose(file);
        free(rows);
        free(pixels);
        return DG_STATUS_IO_ERROR;
    }

    status = DG_STATUS_OK;
    if (setjmp(png_jmpbuf(png_ptr)) != 0) {
        status = DG_STATUS_IO_ERROR;
    } else {
        png_init_io(png_ptr, file);
        png_set_IHDR(
            png_ptr,
            info_ptr,
            (png_uint_32)map->width,
            (png_uint_32)map->height,
            8,
            PNG_COLOR_TYPE_RGBA,
            PNG_INTERLACE_NONE,
            PNG_COMPRESSION_TYPE_BASE,
            PNG_FILTER_TYPE_BASE
        );
        png_set_rows(png_ptr, info_ptr, rows);
        png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
    }

    png_destroy_write_struct(&png_ptr, &info_ptr);
    free(rows);
    free(pixels);
    if (fclose(file) != 0 && status == DG_STATUS_OK) {
        return DG_STATUS_IO_ERROR;
    }

    return status;
}

static const char *dg_export_algorithm_name(int algorithm_id)
{
    switch ((dg_algorithm_t)algorithm_id) {
    case DG_ALGORITHM_BSP_TREE:
        return "bsp_tree";
    case DG_ALGORITHM_DRUNKARDS_WALK:
        return "drunkards_walk";
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        return "rooms_and_mazes";
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        return "cellular_automata";
    case DG_ALGORITHM_VALUE_NOISE:
        return "value_noise";
    case DG_ALGORITHM_ROOM_GRAPH:
        return "room_graph";
    case DG_ALGORITHM_WORM_CAVES:
        return "worm_caves";
    case DG_ALGORITHM_SIMPLEX_NOISE:
        return "simplex_noise";
    default:
        return "unknown";
    }
}

static const char *dg_export_generation_class_name(dg_map_generation_class_t generation_class)
{
    switch (generation_class) {
    case DG_MAP_GENERATION_CLASS_ROOM_LIKE:
        return "room_like";
    case DG_MAP_GENERATION_CLASS_CAVE_LIKE:
        return "cave_like";
    default:
        return "unknown";
    }
}

static const char *dg_export_room_role_name(dg_room_role_t role)
{
    switch (role) {
    case DG_ROOM_ROLE_NONE:
        return "none";
    case DG_ROOM_ROLE_ENTRANCE:
        return "entrance";
    case DG_ROOM_ROLE_EXIT:
        return "exit";
    case DG_ROOM_ROLE_BOSS:
        return "boss";
    case DG_ROOM_ROLE_TREASURE:
        return "treasure";
    case DG_ROOM_ROLE_SHOP:
        return "shop";
    default:
        return "unknown";
    }
}

static dg_status_t dg_export_json_write_escaped(FILE *file, const char *value)
{
    const unsigned char *cursor;

    if (file == NULL || value == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (fputc('\"', file) == EOF) {
        return DG_STATUS_IO_ERROR;
    }

    cursor = (const unsigned char *)value;
    while (*cursor != '\0') {
        if (*cursor == '\"' || *cursor == '\\') {
            if (fputc('\\', file) == EOF || fputc((int)*cursor, file) == EOF) {
                return DG_STATUS_IO_ERROR;
            }
        } else if (*cursor < 0x20u) {
            if (fprintf(file, "\\u%04x", (unsigned int)*cursor) < 0) {
                return DG_STATUS_IO_ERROR;
            }
        } else {
            if (fputc((int)*cursor, file) == EOF) {
                return DG_STATUS_IO_ERROR;
            }
        }
        cursor += 1;
    }

    if (fputc('\"', file) == EOF) {
        return DG_STATUS_IO_ERROR;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_export_write_json_generation_request(
    FILE *file,
    const dg_generation_request_snapshot_t *snapshot
)
{
    size_t i;

    if (file == NULL || snapshot == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (snapshot->present == 0) {
        if (fprintf(file, "null") < 0) {
            return DG_STATUS_IO_ERROR;
        }
        return DG_STATUS_OK;
    }

    if (fprintf(file, "{\n") < 0 ||
        fprintf(file, "    \"width\": %d,\n", snapshot->width) < 0 ||
        fprintf(file, "    \"height\": %d,\n", snapshot->height) < 0 ||
        fprintf(file, "    \"seed\": %llu,\n", (unsigned long long)snapshot->seed) < 0 ||
        fprintf(file, "    \"algorithm_id\": %d,\n", snapshot->algorithm_id) < 0 ||
        fprintf(
            file,
            "    \"algorithm\": \"%s\",\n",
            dg_export_algorithm_name(snapshot->algorithm_id)
        ) < 0 ||
        fprintf(file, "    \"params\": {\n") < 0) {
        return DG_STATUS_IO_ERROR;
    }

    switch ((dg_algorithm_t)snapshot->algorithm_id) {
    case DG_ALGORITHM_BSP_TREE:
        if (fprintf(file, "      \"min_rooms\": %d,\n", snapshot->params.bsp.min_rooms) < 0 ||
            fprintf(file, "      \"max_rooms\": %d,\n", snapshot->params.bsp.max_rooms) < 0 ||
            fprintf(file, "      \"room_min_size\": %d,\n", snapshot->params.bsp.room_min_size) < 0 ||
            fprintf(file, "      \"room_max_size\": %d\n", snapshot->params.bsp.room_max_size) < 0) {
            return DG_STATUS_IO_ERROR;
        }
        break;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        if (fprintf(
                file,
                "      \"wiggle_percent\": %d\n",
                snapshot->params.drunkards_walk.wiggle_percent
            ) < 0) {
            return DG_STATUS_IO_ERROR;
        }
        break;
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        if (fprintf(
                file,
                "      \"initial_wall_percent\": %d,\n",
                snapshot->params.cellular_automata.initial_wall_percent
            ) < 0 ||
            fprintf(
                file,
                "      \"simulation_steps\": %d,\n",
                snapshot->params.cellular_automata.simulation_steps
            ) < 0 ||
            fprintf(
                file,
                "      \"wall_threshold\": %d\n",
                snapshot->params.cellular_automata.wall_threshold
            ) < 0) {
            return DG_STATUS_IO_ERROR;
        }
        break;
    case DG_ALGORITHM_VALUE_NOISE:
        if (fprintf(
                file,
                "      \"feature_size\": %d,\n",
                snapshot->params.value_noise.feature_size
            ) < 0 ||
            fprintf(file, "      \"octaves\": %d,\n", snapshot->params.value_noise.octaves) < 0 ||
            fprintf(
                file,
                "      \"persistence_percent\": %d,\n",
                snapshot->params.value_noise.persistence_percent
            ) < 0 ||
            fprintf(
                file,
                "      \"floor_threshold_percent\": %d\n",
                snapshot->params.value_noise.floor_threshold_percent
            ) < 0) {
            return DG_STATUS_IO_ERROR;
        }
        break;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        if (fprintf(
                file,
                "      \"min_rooms\": %d,\n",
                snapshot->params.rooms_and_mazes.min_rooms
            ) < 0 ||
            fprintf(
                file,
                "      \"max_rooms\": %d,\n",
                snapshot->params.rooms_and_mazes.max_rooms
            ) < 0 ||
            fprintf(
                file,
                "      \"room_min_size\": %d,\n",
                snapshot->params.rooms_and_mazes.room_min_size
            ) < 0 ||
            fprintf(
                file,
                "      \"room_max_size\": %d,\n",
                snapshot->params.rooms_and_mazes.room_max_size
            ) < 0 ||
            fprintf(
                file,
                "      \"maze_wiggle_percent\": %d,\n",
                snapshot->params.rooms_and_mazes.maze_wiggle_percent
            ) < 0 ||
            fprintf(
                file,
                "      \"min_room_connections\": %d,\n",
                snapshot->params.rooms_and_mazes.min_room_connections
            ) < 0 ||
            fprintf(
                file,
                "      \"max_room_connections\": %d,\n",
                snapshot->params.rooms_and_mazes.max_room_connections
            ) < 0 ||
            fprintf(
                file,
                "      \"ensure_full_connectivity\": %d,\n",
                snapshot->params.rooms_and_mazes.ensure_full_connectivity
            ) < 0 ||
            fprintf(
                file,
                "      \"dead_end_prune_steps\": %d\n",
                snapshot->params.rooms_and_mazes.dead_end_prune_steps
            ) < 0) {
            return DG_STATUS_IO_ERROR;
        }
        break;
    case DG_ALGORITHM_ROOM_GRAPH:
        if (fprintf(
                file,
                "      \"min_rooms\": %d,\n",
                snapshot->params.room_graph.min_rooms
            ) < 0 ||
            fprintf(
                file,
                "      \"max_rooms\": %d,\n",
                snapshot->params.room_graph.max_rooms
            ) < 0 ||
            fprintf(
                file,
                "      \"room_min_size\": %d,\n",
                snapshot->params.room_graph.room_min_size
            ) < 0 ||
            fprintf(
                file,
                "      \"room_max_size\": %d,\n",
                snapshot->params.room_graph.room_max_size
            ) < 0 ||
            fprintf(
                file,
                "      \"neighbor_candidates\": %d,\n",
                snapshot->params.room_graph.neighbor_candidates
            ) < 0 ||
            fprintf(
                file,
                "      \"extra_connection_chance_percent\": %d\n",
                snapshot->params.room_graph.extra_connection_chance_percent
            ) < 0) {
            return DG_STATUS_IO_ERROR;
        }
        break;
    case DG_ALGORITHM_WORM_CAVES:
        if (fprintf(
                file,
                "      \"worm_count\": %d,\n",
                snapshot->params.worm_caves.worm_count
            ) < 0 ||
            fprintf(
                file,
                "      \"wiggle_percent\": %d,\n",
                snapshot->params.worm_caves.wiggle_percent
            ) < 0 ||
            fprintf(
                file,
                "      \"branch_chance_percent\": %d,\n",
                snapshot->params.worm_caves.branch_chance_percent
            ) < 0 ||
            fprintf(
                file,
                "      \"target_floor_percent\": %d,\n",
                snapshot->params.worm_caves.target_floor_percent
            ) < 0 ||
            fprintf(
                file,
                "      \"brush_radius\": %d,\n",
                snapshot->params.worm_caves.brush_radius
            ) < 0 ||
            fprintf(
                file,
                "      \"max_steps_per_worm\": %d,\n",
                snapshot->params.worm_caves.max_steps_per_worm
            ) < 0 ||
            fprintf(
                file,
                "      \"ensure_connected\": %d\n",
                snapshot->params.worm_caves.ensure_connected
            ) < 0) {
            return DG_STATUS_IO_ERROR;
        }
        break;
    case DG_ALGORITHM_SIMPLEX_NOISE:
        if (fprintf(
                file,
                "      \"feature_size\": %d,\n",
                snapshot->params.simplex_noise.feature_size
            ) < 0 ||
            fprintf(
                file,
                "      \"octaves\": %d,\n",
                snapshot->params.simplex_noise.octaves
            ) < 0 ||
            fprintf(
                file,
                "      \"persistence_percent\": %d,\n",
                snapshot->params.simplex_noise.persistence_percent
            ) < 0 ||
            fprintf(
                file,
                "      \"floor_threshold_percent\": %d,\n",
                snapshot->params.simplex_noise.floor_threshold_percent
            ) < 0 ||
            fprintf(
                file,
                "      \"ensure_connected\": %d\n",
                snapshot->params.simplex_noise.ensure_connected
            ) < 0) {
            return DG_STATUS_IO_ERROR;
        }
        break;
    default:
        return DG_STATUS_IO_ERROR;
    }

    if (fprintf(file, "    },\n") < 0 ||
        fprintf(file, "    \"post_process_enabled\": %d,\n", snapshot->process.enabled) < 0 ||
        fprintf(file, "    \"process\": [\n") < 0) {
        return DG_STATUS_IO_ERROR;
    }

    for (i = 0; i < snapshot->process.method_count; ++i) {
        const dg_snapshot_process_method_t *method = &snapshot->process.methods[i];
        const char *comma = (i + 1u < snapshot->process.method_count) ? "," : "";

        if (fprintf(file, "      {\n") < 0 ||
            fprintf(file, "        \"type\": %d,\n", method->type) < 0) {
            return DG_STATUS_IO_ERROR;
        }

        switch ((dg_process_method_type_t)method->type) {
        case DG_PROCESS_METHOD_SCALE:
            if (fprintf(
                    file,
                    "        \"type_name\": \"scale\",\n"
                    "        \"factor\": %d\n",
                    method->params.scale.factor
                ) < 0) {
                return DG_STATUS_IO_ERROR;
            }
            break;
        case DG_PROCESS_METHOD_ROOM_SHAPE:
            if (fprintf(
                    file,
                    "        \"type_name\": \"room_shape\",\n"
                    "        \"mode\": %d,\n"
                    "        \"mode_name\": \"%s\",\n"
                    "        \"organicity\": %d\n",
                    method->params.room_shape.mode,
                    method->params.room_shape.mode == (int)DG_ROOM_SHAPE_RECTANGULAR ?
                        "rectangular" :
                        (method->params.room_shape.mode == (int)DG_ROOM_SHAPE_ORGANIC ?
                            "organic" :
                            (method->params.room_shape.mode == (int)DG_ROOM_SHAPE_CELLULAR ?
                                "cellular" :
                                "chamfered")),
                    method->params.room_shape.organicity
                ) < 0) {
                return DG_STATUS_IO_ERROR;
            }
            break;
        case DG_PROCESS_METHOD_PATH_SMOOTH:
            if (fprintf(
                    file,
                    "        \"type_name\": \"path_smooth\",\n"
                    "        \"strength\": %d,\n"
                    "        \"inner_enabled\": %d,\n"
                    "        \"outer_enabled\": %d\n",
                    method->params.path_smooth.strength,
                    method->params.path_smooth.inner_enabled,
                    method->params.path_smooth.outer_enabled
                ) < 0) {
                return DG_STATUS_IO_ERROR;
            }
            break;
        case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
            if (fprintf(
                    file,
                    "        \"type_name\": \"corridor_roughen\",\n"
                    "        \"strength\": %d,\n"
                    "        \"max_depth\": %d,\n"
                    "        \"mode\": %d,\n"
                    "        \"mode_name\": \"%s\"\n",
                    method->params.corridor_roughen.strength,
                    method->params.corridor_roughen.max_depth,
                    method->params.corridor_roughen.mode,
                    method->params.corridor_roughen.mode == (int)DG_CORRIDOR_ROUGHEN_ORGANIC ?
                        "organic" :
                        "uniform"
                ) < 0) {
                return DG_STATUS_IO_ERROR;
            }
            break;
        default:
            return DG_STATUS_IO_ERROR;
        }

        if (fprintf(file, "      }%s\n", comma) < 0) {
            return DG_STATUS_IO_ERROR;
        }
    }

    if (fprintf(file, "    ],\n") < 0 ||
        fprintf(file, "    \"room_types\": {\n") < 0 ||
        fprintf(
            file,
            "      \"policy\": {\n"
            "        \"strict_mode\": %d,\n"
            "        \"allow_untyped_rooms\": %d,\n"
            "        \"default_type_id\": %u\n"
            "      },\n",
            snapshot->room_types.policy.strict_mode,
            snapshot->room_types.policy.allow_untyped_rooms,
            (unsigned int)snapshot->room_types.policy.default_type_id
        ) < 0 ||
        fprintf(file, "      \"definitions\": [\n") < 0) {
        return DG_STATUS_IO_ERROR;
    }

    for (i = 0; i < snapshot->room_types.definition_count; ++i) {
        const dg_snapshot_room_type_definition_t *definition = &snapshot->room_types.definitions[i];
        const char *comma = (i + 1u < snapshot->room_types.definition_count) ? "," : "";

        if (fprintf(
                file,
                "        {\n"
                "          \"type_id\": %u,\n"
                "          \"enabled\": %d,\n"
                "          \"min_count\": %d,\n"
                "          \"max_count\": %d,\n"
                "          \"target_count\": %d,\n"
                "          \"constraints\": {\n"
                "            \"area_min\": %d,\n"
                "            \"area_max\": %d,\n"
                "            \"degree_min\": %d,\n"
                "            \"degree_max\": %d,\n"
                "            \"border_distance_min\": %d,\n"
                "            \"border_distance_max\": %d,\n"
                "            \"graph_depth_min\": %d,\n"
                "            \"graph_depth_max\": %d\n"
                "          },\n"
                "          \"preferences\": {\n"
                "            \"weight\": %d,\n"
                "            \"larger_room_bias\": %d,\n"
                "            \"higher_degree_bias\": %d,\n"
                "            \"border_distance_bias\": %d\n"
                "          }\n"
                "        }%s\n",
                (unsigned int)definition->type_id,
                definition->enabled,
                definition->min_count,
                definition->max_count,
                definition->target_count,
                definition->constraints.area_min,
                definition->constraints.area_max,
                definition->constraints.degree_min,
                definition->constraints.degree_max,
                definition->constraints.border_distance_min,
                definition->constraints.border_distance_max,
                definition->constraints.graph_depth_min,
                definition->constraints.graph_depth_max,
                definition->preferences.weight,
                definition->preferences.larger_room_bias,
                definition->preferences.higher_degree_bias,
                definition->preferences.border_distance_bias,
                comma
            ) < 0) {
            return DG_STATUS_IO_ERROR;
        }
    }

    if (fprintf(file, "      ]\n") < 0 ||
        fprintf(file, "    }\n") < 0 ||
        fprintf(file, "  }") < 0) {
        return DG_STATUS_IO_ERROR;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_export_write_json_file(
    const dg_map_t *map,
    const char *png_path,
    const char *json_path,
    const dg_export_room_type_palette_entry_t *palette_entries,
    size_t palette_count
)
{
    FILE *file;
    dg_status_t status;
    size_t i;
    const dg_generation_request_snapshot_t *snapshot;
    size_t configured_type_count;

    if (map == NULL || png_path == NULL || json_path == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }
    if (map->tiles == NULL || map->width <= 0 || map->height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }
    if (palette_count > 0u && palette_entries == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    snapshot = &map->metadata.generation_request;
    configured_type_count = snapshot->present == 1 ? snapshot->room_types.definition_count : 0u;
    if (configured_type_count > 0u && snapshot->room_types.definitions == NULL) {
        return DG_STATUS_IO_ERROR;
    }

    file = fopen(json_path, "wb");
    if (file == NULL) {
        return DG_STATUS_IO_ERROR;
    }

    if (fprintf(
            file,
            "{\n"
            "  \"format\": \"dungeoneer_png_json_v1\",\n"
            "  \"image\": {\n"
            "    \"path\": "
        ) < 0) {
        (void)fclose(file);
        return DG_STATUS_IO_ERROR;
    }

    status = dg_export_json_write_escaped(file, png_path);
    if (status != DG_STATUS_OK) {
        (void)fclose(file);
        return status;
    }

    if (fprintf(
            file,
            ",\n"
            "    \"width\": %d,\n"
            "    \"height\": %d,\n"
            "    \"pixel_format\": \"rgba8\"\n"
            "  },\n"
            "  \"legend\": [\n",
            map->width,
            map->height
        ) < 0) {
        (void)fclose(file);
        return DG_STATUS_IO_ERROR;
    }

    for (i = 0; i < sizeof(DG_EXPORT_TILE_LEGEND) / sizeof(DG_EXPORT_TILE_LEGEND[0]); ++i) {
        const dg_export_tile_legend_entry_t *entry = &DG_EXPORT_TILE_LEGEND[i];
        const char *comma = (i + 1u < sizeof(DG_EXPORT_TILE_LEGEND) / sizeof(DG_EXPORT_TILE_LEGEND[0]))
                                ? ","
                                : "";
        if (fprintf(
                file,
                "    {\n"
                "      \"tile_id\": %d,\n"
                "      \"tile_name\": \"%s\",\n"
                "      \"rgba\": [%u, %u, %u, %u]\n"
                "    }%s\n",
                (int)entry->tile,
                entry->name,
                (unsigned int)entry->rgba[0],
                (unsigned int)entry->rgba[1],
                (unsigned int)entry->rgba[2],
                (unsigned int)entry->rgba[3],
                comma
            ) < 0) {
            (void)fclose(file);
            return DG_STATUS_IO_ERROR;
        }
    }

    if (fprintf(file, "  ],\n  \"room_type_palette\": [\n") < 0) {
        (void)fclose(file);
        return DG_STATUS_IO_ERROR;
    }
    for (i = 0; i < palette_count; ++i) {
        const dg_export_room_type_palette_entry_t *entry = &palette_entries[i];
        const char *comma = (i + 1u < palette_count) ? "," : "";
        if (fprintf(
                file,
                "    {\n"
                "      \"type_id\": %u,\n"
                "      \"room_count\": %llu,\n"
                "      \"tile_count\": %llu,\n"
                "      \"rgba\": [%u, %u, %u, %u]\n"
                "    }%s\n",
                (unsigned int)entry->type_id,
                (unsigned long long)entry->room_count,
                (unsigned long long)entry->tile_count,
                (unsigned int)entry->rgba[0],
                (unsigned int)entry->rgba[1],
                (unsigned int)entry->rgba[2],
                (unsigned int)entry->rgba[3],
                comma
            ) < 0) {
            (void)fclose(file);
            return DG_STATUS_IO_ERROR;
        }
    }

    if (fprintf(file, "  ],\n  \"configured_room_types\": [\n") < 0) {
        (void)fclose(file);
        return DG_STATUS_IO_ERROR;
    }
    for (i = 0; i < configured_type_count; ++i) {
        const dg_snapshot_room_type_definition_t *definition = &snapshot->room_types.definitions[i];
        unsigned char rgba[4];
        const char *comma = (i + 1u < configured_type_count) ? "," : "";
        dg_export_color_for_room_type(definition->type_id, rgba);
        if (fprintf(
                file,
                "    {\n"
                "      \"type_id\": %u,\n"
                "      \"enabled\": %d,\n"
                "      \"min_count\": %d,\n"
                "      \"max_count\": %d,\n"
                "      \"target_count\": %d,\n"
                "      \"rgba\": [%u, %u, %u, %u]\n"
                "    }%s\n",
                (unsigned int)definition->type_id,
                definition->enabled,
                definition->min_count,
                definition->max_count,
                definition->target_count,
                (unsigned int)rgba[0],
                (unsigned int)rgba[1],
                (unsigned int)rgba[2],
                (unsigned int)rgba[3],
                comma
            ) < 0) {
            (void)fclose(file);
            return DG_STATUS_IO_ERROR;
        }
    }

    if (fprintf(
            file,
            "  ],\n"
            "  \"metadata\": {\n"
            "    \"seed\": %llu,\n"
            "    \"algorithm_id\": %d,\n"
            "    \"algorithm\": \"%s\",\n"
            "    \"generation_class\": %d,\n"
            "    \"generation_class_name\": \"%s\",\n"
            "    \"generation_attempts\": %llu,\n"
            "    \"connected_floor\": %s,\n"
            "    \"connected_component_count\": %llu,\n"
            "    \"largest_component_size\": %llu,\n"
            "    \"walkable_tile_count\": %llu,\n"
            "    \"wall_tile_count\": %llu,\n"
            "    \"room_count\": %llu,\n"
            "    \"typed_room_count\": %llu,\n"
            "    \"untyped_room_count\": %llu,\n"
            "    \"corridor_count\": %llu,\n"
            "    \"corridor_total_length\": %llu,\n"
            "    \"entrance_exit_distance\": %d\n"
            "  },\n",
            (unsigned long long)map->metadata.seed,
            map->metadata.algorithm_id,
            dg_export_algorithm_name(map->metadata.algorithm_id),
            (int)map->metadata.generation_class,
            dg_export_generation_class_name(map->metadata.generation_class),
            (unsigned long long)map->metadata.generation_attempts,
            map->metadata.connected_floor ? "true" : "false",
            (unsigned long long)map->metadata.connected_component_count,
            (unsigned long long)map->metadata.largest_component_size,
            (unsigned long long)map->metadata.walkable_tile_count,
            (unsigned long long)map->metadata.wall_tile_count,
            (unsigned long long)map->metadata.room_count,
            (unsigned long long)map->metadata.diagnostics.typed_room_count,
            (unsigned long long)map->metadata.diagnostics.untyped_room_count,
            (unsigned long long)map->metadata.corridor_count,
            (unsigned long long)map->metadata.corridor_total_length,
            map->metadata.entrance_exit_distance
        ) < 0) {
        (void)fclose(file);
        return DG_STATUS_IO_ERROR;
    }

    if (fprintf(file, "  \"rooms\": [\n") < 0) {
        (void)fclose(file);
        return DG_STATUS_IO_ERROR;
    }
    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map->metadata.rooms[i];
        const char *comma = (i + 1u < map->metadata.room_count) ? "," : "";
        if (fprintf(
                file,
                "    {\n"
                "      \"id\": %d,\n"
                "      \"x\": %d,\n"
                "      \"y\": %d,\n"
                "      \"width\": %d,\n"
                "      \"height\": %d,\n"
                "      \"flags\": %u,\n"
                "      \"role\": %d,\n"
                "      \"role_name\": \"%s\",\n"
                "      \"type_id\": %u\n"
                "    }%s\n",
                room->id,
                room->bounds.x,
                room->bounds.y,
                room->bounds.width,
                room->bounds.height,
                (unsigned int)room->flags,
                (int)room->role,
                dg_export_room_role_name(room->role),
                (unsigned int)room->type_id,
                comma
            ) < 0) {
            (void)fclose(file);
            return DG_STATUS_IO_ERROR;
        }
    }

    if (fprintf(file, "  ],\n  \"corridors\": [\n") < 0) {
        (void)fclose(file);
        return DG_STATUS_IO_ERROR;
    }
    for (i = 0; i < map->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];
        const char *comma = (i + 1u < map->metadata.corridor_count) ? "," : "";
        if (fprintf(
                file,
                "    {\n"
                "      \"from_room_id\": %d,\n"
                "      \"to_room_id\": %d,\n"
                "      \"width\": %d,\n"
                "      \"length\": %d\n"
                "    }%s\n",
                corridor->from_room_id,
                corridor->to_room_id,
                corridor->width,
                corridor->length,
                comma
            ) < 0) {
            (void)fclose(file);
            return DG_STATUS_IO_ERROR;
        }
    }

    if (fprintf(file, "  ],\n  \"generation_request\": ") < 0) {
        (void)fclose(file);
        return DG_STATUS_IO_ERROR;
    }
    status = dg_export_write_json_generation_request(file, &map->metadata.generation_request);
    if (status != DG_STATUS_OK) {
        (void)fclose(file);
        return status;
    }

    if (fprintf(file, "\n}\n") < 0) {
        (void)fclose(file);
        return DG_STATUS_IO_ERROR;
    }

    if (fclose(file) != 0) {
        return DG_STATUS_IO_ERROR;
    }

    return DG_STATUS_OK;
}

dg_status_t dg_map_export_png_json(
    const dg_map_t *map,
    const char *png_path,
    const char *json_path
)
{
    dg_status_t status;
    int *room_index_by_tile;
    dg_export_room_type_palette_entry_t *palette_entries;
    size_t palette_count;

    if (map == NULL || png_path == NULL || json_path == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->tiles == NULL || map->width <= 0 || map->height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    room_index_by_tile = NULL;
    palette_entries = NULL;
    palette_count = 0u;
    status = dg_export_build_room_type_overlay(
        map,
        &room_index_by_tile,
        &palette_entries,
        &palette_count
    );
    if (status != DG_STATUS_OK) {
        free(room_index_by_tile);
        free(palette_entries);
        return status;
    }

    status = dg_export_write_png_file(map, png_path, room_index_by_tile);
    if (status != DG_STATUS_OK) {
        free(room_index_by_tile);
        free(palette_entries);
        return status;
    }

    status = dg_export_write_json_file(map, png_path, json_path, palette_entries, palette_count);
    free(room_index_by_tile);
    free(palette_entries);
    return status;
}
