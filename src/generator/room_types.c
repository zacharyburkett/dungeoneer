#include "internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dungeoneer/io.h"

typedef struct dg_room_feature {
    size_t area;
    size_t degree;
    size_t border_distance;
    size_t graph_depth;
} dg_room_feature_t;

static void dg_clear_room_type_assignment_diagnostics(dg_map_t *map)
{
    if (map == NULL) {
        return;
    }

    free(map->metadata.diagnostics.room_type_quotas);
    map->metadata.diagnostics.room_type_quotas = NULL;
    map->metadata.diagnostics.room_type_count = 0;
    map->metadata.diagnostics.typed_room_count = 0;
    map->metadata.diagnostics.untyped_room_count = 0;
    map->metadata.diagnostics.room_type_min_miss_count = 0;
    map->metadata.diagnostics.room_type_max_excess_count = 0;
    map->metadata.diagnostics.room_type_target_miss_count = 0;
}

static dg_status_t dg_populate_room_type_assignment_diagnostics(
    const dg_generate_request_t *request,
    dg_map_t *map
)
{
    size_t i;

    if (map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_clear_room_type_assignment_diagnostics(map);
    if (map->metadata.generation_class != DG_MAP_GENERATION_CLASS_ROOM_LIKE ||
        map->metadata.rooms == NULL ||
        map->metadata.room_count == 0) {
        return DG_STATUS_OK;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        if (map->metadata.rooms[i].type_id == DG_ROOM_TYPE_UNASSIGNED) {
            map->metadata.diagnostics.untyped_room_count += 1;
        } else {
            map->metadata.diagnostics.typed_room_count += 1;
        }
    }

    if (request == NULL || request->room_types.definition_count == 0 ||
        request->room_types.definitions == NULL) {
        return DG_STATUS_OK;
    }

    if (request->room_types.definition_count >
        (SIZE_MAX / sizeof(*map->metadata.diagnostics.room_type_quotas))) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    map->metadata.diagnostics.room_type_quotas = (dg_room_type_quota_diagnostics_t *)calloc(
        request->room_types.definition_count,
        sizeof(*map->metadata.diagnostics.room_type_quotas)
    );
    if (map->metadata.diagnostics.room_type_quotas == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    map->metadata.diagnostics.room_type_count = request->room_types.definition_count;

    for (i = 0; i < request->room_types.definition_count; ++i) {
        const dg_room_type_definition_t *definition = &request->room_types.definitions[i];
        dg_room_type_quota_diagnostics_t *quota = &map->metadata.diagnostics.room_type_quotas[i];

        quota->type_id = definition->type_id;
        quota->enabled = definition->enabled;
        quota->min_count = definition->min_count;
        quota->max_count = definition->max_count;
        quota->target_count = definition->target_count;
        quota->assigned_count = 0;
        quota->min_satisfied = 1;
        quota->max_satisfied = 1;
        quota->target_satisfied = 1;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        uint32_t assigned_type = map->metadata.rooms[i].type_id;
        size_t j;

        if (assigned_type == DG_ROOM_TYPE_UNASSIGNED) {
            continue;
        }

        for (j = 0; j < map->metadata.diagnostics.room_type_count; ++j) {
            dg_room_type_quota_diagnostics_t *quota = &map->metadata.diagnostics.room_type_quotas[j];
            if (quota->type_id == assigned_type) {
                quota->assigned_count += 1;
                break;
            }
        }
    }

    for (i = 0; i < map->metadata.diagnostics.room_type_count; ++i) {
        dg_room_type_quota_diagnostics_t *quota = &map->metadata.diagnostics.room_type_quotas[i];

        quota->min_satisfied = quota->assigned_count >= (size_t)dg_max_int(quota->min_count, 0) ? 1 : 0;
        quota->max_satisfied =
            (quota->max_count == -1 || quota->assigned_count <= (size_t)quota->max_count) ? 1 : 0;
        quota->target_satisfied =
            (quota->target_count == -1 || quota->assigned_count == (size_t)quota->target_count) ? 1 : 0;

        if (quota->enabled != 1) {
            continue;
        }

        if (quota->min_satisfied == 0) {
            map->metadata.diagnostics.room_type_min_miss_count += 1;
        }
        if (quota->max_satisfied == 0) {
            map->metadata.diagnostics.room_type_max_excess_count += 1;
        }
        if (quota->target_count != -1 && quota->target_satisfied == 0) {
            map->metadata.diagnostics.room_type_target_miss_count += 1;
        }
    }

    return DG_STATUS_OK;
}

static bool dg_corridor_endpoints_are_valid(const dg_map_t *map, const dg_corridor_metadata_t *corridor)
{
    if (map == NULL || corridor == NULL) {
        return false;
    }

    if (corridor->from_room_id < 0 || corridor->to_room_id < 0) {
        return false;
    }

    if ((size_t)corridor->from_room_id >= map->metadata.room_count) {
        return false;
    }

    if ((size_t)corridor->to_room_id >= map->metadata.room_count) {
        return false;
    }

    if (corridor->from_room_id == corridor->to_room_id) {
        return false;
    }

    return true;
}

static bool dg_value_in_constraint_range(size_t value, int min_value, int max_value)
{
    if (min_value >= 0 && value < (size_t)min_value) {
        return false;
    }

    if (max_value != -1 && value > (size_t)max_value) {
        return false;
    }

    return true;
}

static bool dg_compute_room_border_distance(
    const dg_map_t *map,
    const dg_rect_t *bounds,
    size_t *out_border_distance
)
{
    long long left;
    long long right;
    long long top;
    long long bottom;
    long long min_horiz;
    long long min_vert;

    if (map == NULL || bounds == NULL || out_border_distance == NULL) {
        return false;
    }

    left = (long long)bounds->x;
    top = (long long)bounds->y;
    right = (long long)map->width - ((long long)bounds->x + (long long)bounds->width);
    bottom = (long long)map->height - ((long long)bounds->y + (long long)bounds->height);

    if (left < 0 || top < 0 || right < 0 || bottom < 0) {
        return false;
    }

    min_horiz = left < right ? left : right;
    min_vert = top < bottom ? top : bottom;
    *out_border_distance = (size_t)(min_horiz < min_vert ? min_horiz : min_vert);
    return true;
}

static size_t dg_room_degree_from_corridors(const dg_map_t *map, size_t room_index)
{
    size_t i;
    size_t degree;

    degree = 0;
    for (i = 0; i < map->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];

        if (!dg_corridor_endpoints_are_valid(map, corridor)) {
            continue;
        }

        if ((size_t)corridor->from_room_id == room_index || (size_t)corridor->to_room_id == room_index) {
            degree += 1;
        }
    }

    return degree;
}

static dg_status_t dg_compute_room_features(const dg_map_t *map, dg_room_feature_t **out_features)
{
    size_t room_count;
    size_t i;
    dg_room_feature_t *features;

    if (map == NULL || out_features == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_features = NULL;
    room_count = map->metadata.room_count;
    if (room_count == 0) {
        return DG_STATUS_OK;
    }

    features = (dg_room_feature_t *)calloc(room_count, sizeof(dg_room_feature_t));
    if (features == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (i = 0; i < room_count; ++i) {
        const dg_rect_t *bounds = &map->metadata.rooms[i].bounds;

        if (bounds->width <= 0 || bounds->height <= 0) {
            free(features);
            return DG_STATUS_GENERATION_FAILED;
        }

        if ((size_t)bounds->width > SIZE_MAX / (size_t)bounds->height) {
            free(features);
            return DG_STATUS_GENERATION_FAILED;
        }

        features[i].area = (size_t)bounds->width * (size_t)bounds->height;
        features[i].graph_depth = SIZE_MAX;

        if (map->metadata.room_adjacency != NULL && map->metadata.room_adjacency_count == room_count) {
            features[i].degree = map->metadata.room_adjacency[i].count;
        } else {
            features[i].degree = dg_room_degree_from_corridors(map, i);
        }

        if (!dg_compute_room_border_distance(map, bounds, &features[i].border_distance)) {
            free(features);
            return DG_STATUS_GENERATION_FAILED;
        }
    }

    *out_features = features;
    return DG_STATUS_OK;
}

static dg_status_t dg_populate_graph_depths(
    const dg_map_t *map,
    dg_room_feature_t *features,
    size_t room_count
)
{
    size_t *queue;
    size_t head;
    size_t tail;

    if (map == NULL || features == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (room_count == 0) {
        return DG_STATUS_OK;
    }

    queue = (size_t *)malloc(room_count * sizeof(size_t));
    if (queue == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    features[0].graph_depth = 0;
    head = 0;
    tail = 0;
    queue[tail++] = 0;

    while (head < tail) {
        size_t current = queue[head++];
        size_t current_depth = features[current].graph_depth;

        if (map->metadata.room_adjacency != NULL &&
            map->metadata.room_neighbors != NULL &&
            map->metadata.room_adjacency_count == room_count) {
            const dg_room_adjacency_span_t *span = &map->metadata.room_adjacency[current];
            size_t n;

            if (span->start_index > map->metadata.room_neighbor_count ||
                span->count > map->metadata.room_neighbor_count ||
                span->count > map->metadata.room_neighbor_count - span->start_index) {
                free(queue);
                return DG_STATUS_GENERATION_FAILED;
            }

            for (n = 0; n < span->count; ++n) {
                const dg_room_neighbor_t *neighbor = &map->metadata.room_neighbors[span->start_index + n];
                size_t next_room;

                if (neighbor->room_id < 0 || (size_t)neighbor->room_id >= room_count) {
                    free(queue);
                    return DG_STATUS_GENERATION_FAILED;
                }

                next_room = (size_t)neighbor->room_id;
                if (features[next_room].graph_depth != SIZE_MAX) {
                    continue;
                }

                features[next_room].graph_depth = current_depth + 1;
                queue[tail++] = next_room;
            }
        } else {
            size_t c;

            for (c = 0; c < map->metadata.corridor_count; ++c) {
                const dg_corridor_metadata_t *corridor = &map->metadata.corridors[c];
                size_t next_room;

                if (!dg_corridor_endpoints_are_valid(map, corridor)) {
                    continue;
                }

                if ((size_t)corridor->from_room_id == current) {
                    next_room = (size_t)corridor->to_room_id;
                } else if ((size_t)corridor->to_room_id == current) {
                    next_room = (size_t)corridor->from_room_id;
                } else {
                    continue;
                }

                if (features[next_room].graph_depth != SIZE_MAX) {
                    continue;
                }

                features[next_room].graph_depth = current_depth + 1;
                queue[tail++] = next_room;
            }
        }
    }

    free(queue);
    return DG_STATUS_OK;
}

static bool dg_room_matches_type_constraints(
    const dg_room_feature_t *feature,
    const dg_room_type_definition_t *type_definition
)
{
    if (feature == NULL || type_definition == NULL) {
        return false;
    }

    if (!dg_value_in_constraint_range(
            feature->area,
            type_definition->constraints.area_min,
            type_definition->constraints.area_max
        )) {
        return false;
    }

    if (!dg_value_in_constraint_range(
            feature->degree,
            type_definition->constraints.degree_min,
            type_definition->constraints.degree_max
        )) {
        return false;
    }

    if (!dg_value_in_constraint_range(
            feature->border_distance,
            type_definition->constraints.border_distance_min,
            type_definition->constraints.border_distance_max
        )) {
        return false;
    }

    if (feature->graph_depth == SIZE_MAX) {
        if (type_definition->constraints.graph_depth_min > 0 ||
            type_definition->constraints.graph_depth_max != -1) {
            return false;
        }
    } else {
        if (!dg_value_in_constraint_range(
                feature->graph_depth,
                type_definition->constraints.graph_depth_min,
                type_definition->constraints.graph_depth_max
            )) {
            return false;
        }
    }

    return true;
}

static int64_t dg_room_type_base_score(
    const dg_room_feature_t *feature,
    const dg_room_type_definition_t *type_definition
)
{
    int64_t score;

    score = (int64_t)type_definition->preferences.weight * 1000000LL;
    score += (int64_t)type_definition->preferences.larger_room_bias * (int64_t)feature->area;
    score +=
        (int64_t)type_definition->preferences.higher_degree_bias * (int64_t)feature->degree * 1000LL;
    score +=
        (int64_t)type_definition->preferences.border_distance_bias *
        (int64_t)feature->border_distance *
        1000LL;

    return score;
}

static bool dg_type_has_capacity(const dg_room_type_definition_t *type_definition, size_t assigned_count)
{
    if (type_definition->max_count == -1) {
        return true;
    }

    return assigned_count < (size_t)type_definition->max_count;
}

static size_t dg_find_default_type_enabled_index(
    const dg_room_type_assignment_config_t *config,
    const size_t *enabled_type_indices,
    size_t enabled_type_count
)
{
    size_t i;

    for (i = 0; i < enabled_type_count; ++i) {
        size_t definition_index = enabled_type_indices[i];
        if (config->definitions[definition_index].type_id == config->policy.default_type_id) {
            return i;
        }
    }

    return SIZE_MAX;
}

static bool dg_choose_best_room_for_type(
    dg_rng_t *rng,
    const dg_room_feature_t *features,
    const dg_room_type_definition_t *type_definition,
    const bool *eligibility_matrix,
    size_t room_count,
    size_t enabled_type_count,
    size_t enabled_type_index,
    const size_t *room_assignments,
    const size_t *eligible_type_counts_by_room,
    size_t *out_room_index
)
{
    size_t room_index;
    bool found;
    int64_t best_score;
    size_t tie_count;

    if (out_room_index == NULL) {
        return false;
    }

    found = false;
    best_score = INT64_MIN;
    tie_count = 0;

    for (room_index = 0; room_index < room_count; ++room_index) {
        int64_t score;

        if (room_assignments[room_index] != SIZE_MAX) {
            continue;
        }

        if (!eligibility_matrix[room_index * enabled_type_count + enabled_type_index]) {
            continue;
        }

        score = dg_room_type_base_score(&features[room_index], type_definition);
        if (eligible_type_counts_by_room[room_index] > 0) {
            score += 100000LL / (int64_t)eligible_type_counts_by_room[room_index];
        }

        if (!found || score > best_score) {
            found = true;
            best_score = score;
            *out_room_index = room_index;
            tie_count = 1;
            continue;
        }

        if (score == best_score) {
            tie_count += 1;
            if (rng != NULL && (((uint64_t)dg_rng_next_u32(rng)) % (uint64_t)tie_count == 0u)) {
                *out_room_index = room_index;
            }
        }
    }

    return found;
}

static size_t dg_choose_best_type_for_room(
    dg_rng_t *rng,
    const dg_room_feature_t *features,
    size_t room_index,
    const dg_room_type_assignment_config_t *config,
    const size_t *enabled_type_indices,
    size_t enabled_type_count,
    const bool *eligibility_matrix,
    const size_t *assigned_counts_by_enabled_type
)
{
    size_t enabled_type_index;
    size_t selected_enabled_type_index;
    bool found;
    int64_t best_score;
    size_t tie_count;

    found = false;
    best_score = INT64_MIN;
    tie_count = 0;
    selected_enabled_type_index = SIZE_MAX;

    for (enabled_type_index = 0; enabled_type_index < enabled_type_count; ++enabled_type_index) {
        const dg_room_type_definition_t *type_definition;
        int64_t score;

        if (!eligibility_matrix[room_index * enabled_type_count + enabled_type_index]) {
            continue;
        }

        type_definition = &config->definitions[enabled_type_indices[enabled_type_index]];
        if (!dg_type_has_capacity(type_definition, assigned_counts_by_enabled_type[enabled_type_index])) {
            continue;
        }

        score = dg_room_type_base_score(&features[room_index], type_definition);

        if (type_definition->target_count != -1 &&
            assigned_counts_by_enabled_type[enabled_type_index] < (size_t)type_definition->target_count) {
            score += 100000000000LL;
        }

        if (assigned_counts_by_enabled_type[enabled_type_index] < (size_t)type_definition->min_count) {
            score += 200000000000LL;
        }

        if (!found || score > best_score) {
            found = true;
            best_score = score;
            selected_enabled_type_index = enabled_type_index;
            tie_count = 1;
            continue;
        }

        if (score == best_score) {
            tie_count += 1;
            if (rng != NULL && (((uint64_t)dg_rng_next_u32(rng)) % (uint64_t)tie_count == 0u)) {
                selected_enabled_type_index = enabled_type_index;
            }
        }
    }

    return selected_enabled_type_index;
}

static void dg_sort_enabled_types_by_minimum_slack(
    size_t *ordered_enabled_indices,
    const dg_room_type_assignment_config_t *config,
    const size_t *enabled_type_indices,
    const size_t *eligible_counts_by_enabled_type,
    size_t enabled_type_count
)
{
    size_t i;
    size_t j;

    for (i = 0; i < enabled_type_count; ++i) {
        ordered_enabled_indices[i] = i;
    }

    for (i = 0; i < enabled_type_count; ++i) {
        size_t best = i;

        for (j = i + 1; j < enabled_type_count; ++j) {
            size_t left_enabled_index = ordered_enabled_indices[best];
            size_t right_enabled_index = ordered_enabled_indices[j];
            const dg_room_type_definition_t *left_definition;
            const dg_room_type_definition_t *right_definition;
            size_t left_eligible;
            size_t right_eligible;
            long long left_slack;
            long long right_slack;

            left_definition = &config->definitions[enabled_type_indices[left_enabled_index]];
            right_definition = &config->definitions[enabled_type_indices[right_enabled_index]];

            left_eligible = eligible_counts_by_enabled_type[left_enabled_index];
            right_eligible = eligible_counts_by_enabled_type[right_enabled_index];

            left_slack = (long long)left_eligible - (long long)left_definition->min_count;
            right_slack = (long long)right_eligible - (long long)right_definition->min_count;

            if (right_slack < left_slack ||
                (right_slack == left_slack &&
                 right_definition->type_id < left_definition->type_id)) {
                best = j;
            }
        }

        if (best != i) {
            size_t tmp = ordered_enabled_indices[i];
            ordered_enabled_indices[i] = ordered_enabled_indices[best];
            ordered_enabled_indices[best] = tmp;
        }
    }
}

static dg_status_t dg_validate_strict_assignment_feasibility(
    const dg_room_type_assignment_config_t *config,
    const size_t *enabled_type_indices,
    size_t enabled_type_count,
    const size_t *eligible_counts_by_enabled_type,
    const size_t *eligible_type_counts_by_room,
    size_t room_count
)
{
    size_t i;
    size_t total_minimum;
    bool has_unbounded_max;
    size_t total_maximum;

    if (config->policy.strict_mode == 0) {
        return DG_STATUS_OK;
    }

    if (enabled_type_count == 0 && config->policy.allow_untyped_rooms == 0) {
        return DG_STATUS_GENERATION_FAILED;
    }

    total_minimum = 0;
    has_unbounded_max = false;
    total_maximum = 0;

    for (i = 0; i < enabled_type_count; ++i) {
        const dg_room_type_definition_t *type_definition;
        size_t eligible_count;

        type_definition = &config->definitions[enabled_type_indices[i]];
        eligible_count = eligible_counts_by_enabled_type[i];

        if ((size_t)type_definition->min_count > eligible_count) {
            return DG_STATUS_GENERATION_FAILED;
        }

        total_minimum += (size_t)type_definition->min_count;

        if (type_definition->max_count == -1) {
            has_unbounded_max = true;
        } else {
            total_maximum += (size_t)type_definition->max_count;
        }
    }

    if (total_minimum > room_count) {
        return DG_STATUS_GENERATION_FAILED;
    }

    if (config->policy.allow_untyped_rooms == 0) {
        for (i = 0; i < room_count; ++i) {
            if (eligible_type_counts_by_room[i] == 0) {
                return DG_STATUS_GENERATION_FAILED;
            }
        }

        if (!has_unbounded_max && total_maximum < room_count) {
            return DG_STATUS_GENERATION_FAILED;
        }
    }

    return DG_STATUS_OK;
}

dg_status_t dg_apply_room_type_assignment(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    size_t room_count;
    size_t i;
    size_t enabled_type_count;
    size_t *enabled_type_indices;
    bool *eligibility_matrix;
    size_t *eligible_counts_by_enabled_type;
    size_t *eligible_type_counts_by_room;
    size_t *assigned_counts_by_enabled_type;
    size_t *room_assignments;
    size_t *ordered_enabled_indices;
    dg_room_feature_t *features;
    dg_status_t status;

    if (request == NULL || map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_clear_room_type_assignment_diagnostics(map);

    if (map->metadata.generation_class != DG_MAP_GENERATION_CLASS_ROOM_LIKE) {
        return DG_STATUS_OK;
    }

    room_count = map->metadata.room_count;
    for (i = 0; i < room_count; ++i) {
        map->metadata.rooms[i].type_id = DG_ROOM_TYPE_UNASSIGNED;
    }

    if (request->room_types.definition_count == 0 || room_count == 0) {
        return dg_populate_room_type_assignment_diagnostics(request, map);
    }

    features = NULL;
    status = dg_compute_room_features(map, &features);
    if (status != DG_STATUS_OK) {
        free(features);
        return status;
    }

    status = dg_populate_graph_depths(map, features, room_count);
    if (status != DG_STATUS_OK) {
        free(features);
        return status;
    }

    enabled_type_count = 0;
    for (i = 0; i < request->room_types.definition_count; ++i) {
        if (request->room_types.definitions[i].enabled == 1) {
            enabled_type_count += 1;
        }
    }

    if (enabled_type_count == 0) {
        free(features);
        return DG_STATUS_OK;
    }

    enabled_type_indices = (size_t *)malloc(enabled_type_count * sizeof(size_t));
    eligibility_matrix = (bool *)calloc(room_count * enabled_type_count, sizeof(bool));
    eligible_counts_by_enabled_type = (size_t *)calloc(enabled_type_count, sizeof(size_t));
    eligible_type_counts_by_room = (size_t *)calloc(room_count, sizeof(size_t));
    assigned_counts_by_enabled_type = (size_t *)calloc(enabled_type_count, sizeof(size_t));
    room_assignments = (size_t *)malloc(room_count * sizeof(size_t));
    ordered_enabled_indices = (size_t *)malloc(enabled_type_count * sizeof(size_t));

    if (enabled_type_indices == NULL || eligibility_matrix == NULL ||
        eligible_counts_by_enabled_type == NULL || eligible_type_counts_by_room == NULL ||
        assigned_counts_by_enabled_type == NULL || room_assignments == NULL ||
        ordered_enabled_indices == NULL) {
        free(ordered_enabled_indices);
        free(room_assignments);
        free(assigned_counts_by_enabled_type);
        free(eligible_type_counts_by_room);
        free(eligible_counts_by_enabled_type);
        free(eligibility_matrix);
        free(enabled_type_indices);
        free(features);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    {
        size_t write_index;

        write_index = 0;
        for (i = 0; i < request->room_types.definition_count; ++i) {
            if (request->room_types.definitions[i].enabled == 1) {
                enabled_type_indices[write_index++] = i;
            }
        }
    }

    for (i = 0; i < room_count; ++i) {
        room_assignments[i] = SIZE_MAX;
    }

    for (i = 0; i < room_count; ++i) {
        size_t enabled_type_index;

        for (enabled_type_index = 0; enabled_type_index < enabled_type_count; ++enabled_type_index) {
            size_t definition_index = enabled_type_indices[enabled_type_index];
            const dg_room_type_definition_t *type_definition =
                &request->room_types.definitions[definition_index];
            bool eligible = dg_room_matches_type_constraints(&features[i], type_definition);

            eligibility_matrix[i * enabled_type_count + enabled_type_index] = eligible;
            if (eligible) {
                eligible_counts_by_enabled_type[enabled_type_index] += 1;
                eligible_type_counts_by_room[i] += 1;
            }
        }
    }

    status = dg_validate_strict_assignment_feasibility(
        &request->room_types,
        enabled_type_indices,
        enabled_type_count,
        eligible_counts_by_enabled_type,
        eligible_type_counts_by_room,
        room_count
    );
    if (status != DG_STATUS_OK) {
        free(ordered_enabled_indices);
        free(room_assignments);
        free(assigned_counts_by_enabled_type);
        free(eligible_type_counts_by_room);
        free(eligible_counts_by_enabled_type);
        free(eligibility_matrix);
        free(enabled_type_indices);
        free(features);
        return status;
    }

    dg_sort_enabled_types_by_minimum_slack(
        ordered_enabled_indices,
        &request->room_types,
        enabled_type_indices,
        eligible_counts_by_enabled_type,
        enabled_type_count
    );

    for (i = 0; i < enabled_type_count; ++i) {
        size_t enabled_type_index = ordered_enabled_indices[i];
        size_t definition_index = enabled_type_indices[enabled_type_index];
        const dg_room_type_definition_t *type_definition =
            &request->room_types.definitions[definition_index];

        while (assigned_counts_by_enabled_type[enabled_type_index] < (size_t)type_definition->min_count) {
            size_t room_index;
            bool found;

            found = dg_choose_best_room_for_type(
                rng,
                features,
                type_definition,
                eligibility_matrix,
                room_count,
                enabled_type_count,
                enabled_type_index,
                room_assignments,
                eligible_type_counts_by_room,
                &room_index
            );

            if (!found) {
                if (request->room_types.policy.strict_mode == 1) {
                    free(ordered_enabled_indices);
                    free(room_assignments);
                    free(assigned_counts_by_enabled_type);
                    free(eligible_type_counts_by_room);
                    free(eligible_counts_by_enabled_type);
                    free(eligibility_matrix);
                    free(enabled_type_indices);
                    free(features);
                    return DG_STATUS_GENERATION_FAILED;
                }
                break;
            }

            room_assignments[room_index] = enabled_type_index;
            assigned_counts_by_enabled_type[enabled_type_index] += 1;
        }
    }

    for (i = 0; i < room_count; ++i) {
        size_t selected_enabled_type_index;

        if (room_assignments[i] != SIZE_MAX) {
            continue;
        }

        selected_enabled_type_index = dg_choose_best_type_for_room(
            rng,
            features,
            i,
            &request->room_types,
            enabled_type_indices,
            enabled_type_count,
            eligibility_matrix,
            assigned_counts_by_enabled_type
        );

        if (selected_enabled_type_index == SIZE_MAX) {
            continue;
        }

        room_assignments[i] = selected_enabled_type_index;
        assigned_counts_by_enabled_type[selected_enabled_type_index] += 1;
    }

    if (request->room_types.policy.allow_untyped_rooms == 0) {
        size_t default_enabled_type_index;

        default_enabled_type_index = dg_find_default_type_enabled_index(
            &request->room_types,
            enabled_type_indices,
            enabled_type_count
        );

        for (i = 0; i < room_count; ++i) {
            if (room_assignments[i] != SIZE_MAX) {
                continue;
            }

            if (request->room_types.policy.strict_mode == 1 ||
                default_enabled_type_index == SIZE_MAX) {
                free(ordered_enabled_indices);
                free(room_assignments);
                free(assigned_counts_by_enabled_type);
                free(eligible_type_counts_by_room);
                free(eligible_counts_by_enabled_type);
                free(eligibility_matrix);
                free(enabled_type_indices);
                free(features);
                return DG_STATUS_GENERATION_FAILED;
            }

            room_assignments[i] = default_enabled_type_index;
            assigned_counts_by_enabled_type[default_enabled_type_index] += 1;
        }
    }

    if (request->room_types.policy.strict_mode == 1) {
        size_t enabled_type_index;

        for (enabled_type_index = 0; enabled_type_index < enabled_type_count; ++enabled_type_index) {
            size_t definition_index = enabled_type_indices[enabled_type_index];
            const dg_room_type_definition_t *type_definition =
                &request->room_types.definitions[definition_index];

            if (assigned_counts_by_enabled_type[enabled_type_index] < (size_t)type_definition->min_count) {
                free(ordered_enabled_indices);
                free(room_assignments);
                free(assigned_counts_by_enabled_type);
                free(eligible_type_counts_by_room);
                free(eligible_counts_by_enabled_type);
                free(eligibility_matrix);
                free(enabled_type_indices);
                free(features);
                return DG_STATUS_GENERATION_FAILED;
            }

            if (type_definition->max_count != -1 &&
                assigned_counts_by_enabled_type[enabled_type_index] > (size_t)type_definition->max_count) {
                free(ordered_enabled_indices);
                free(room_assignments);
                free(assigned_counts_by_enabled_type);
                free(eligible_type_counts_by_room);
                free(eligible_counts_by_enabled_type);
                free(eligibility_matrix);
                free(enabled_type_indices);
                free(features);
                return DG_STATUS_GENERATION_FAILED;
            }
        }
    }

    for (i = 0; i < room_count; ++i) {
        if (room_assignments[i] == SIZE_MAX) {
            map->metadata.rooms[i].type_id = DG_ROOM_TYPE_UNASSIGNED;
        } else {
            size_t definition_index = enabled_type_indices[room_assignments[i]];
            map->metadata.rooms[i].type_id = request->room_types.definitions[definition_index].type_id;
        }
    }

    free(ordered_enabled_indices);
    free(room_assignments);
    free(assigned_counts_by_enabled_type);
    free(eligible_type_counts_by_room);
    free(eligible_counts_by_enabled_type);
    free(eligibility_matrix);
    free(enabled_type_indices);
    free(features);
    return dg_populate_room_type_assignment_diagnostics(request, map);
}

typedef struct dg_room_template_cache_entry {
    int has_template;
    int loaded;
    dg_map_t map;
} dg_room_template_cache_entry_t;

static int dg_room_template_application_depth = 0;

static size_t dg_find_room_type_definition_index_by_type_id(
    const dg_generate_request_t *request,
    uint32_t type_id
)
{
    size_t i;

    if (request == NULL || request->room_types.definitions == NULL) {
        return SIZE_MAX;
    }

    for (i = 0; i < request->room_types.definition_count; ++i) {
        if (request->room_types.definitions[i].type_id == type_id) {
            return i;
        }
    }

    return SIZE_MAX;
}

static bool dg_template_map_is_nested(const dg_map_t *template_map)
{
    size_t i;
    const dg_generation_request_snapshot_t *snapshot;

    if (template_map == NULL) {
        return true;
    }

    snapshot = &template_map->metadata.generation_request;
    if (snapshot->present != 1 || snapshot->room_types.definitions == NULL) {
        return false;
    }

    for (i = 0; i < snapshot->room_types.definition_count; ++i) {
        const dg_snapshot_room_type_definition_t *definition = &snapshot->room_types.definitions[i];
        if (definition->template_map_path[0] != '\0') {
            return true;
        }
    }

    return false;
}

static dg_status_t dg_validate_loaded_room_template(
    const dg_map_t *template_map
)
{
    if (template_map == NULL || template_map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (dg_template_map_is_nested(template_map)) {
        return DG_STATUS_GENERATION_FAILED;
    }

    return DG_STATUS_OK;
}

static void dg_release_template_request_buffers(
    dg_process_method_t *process_methods,
    dg_edge_opening_spec_t *edge_openings
)
{
    free(edge_openings);
    free(process_methods);
}

static bool dg_div_ceil_positive_int(int value, int divisor, int *out_result)
{
    int64_t result;

    if (out_result == NULL || value <= 0 || divisor <= 0) {
        return false;
    }

    result = ((int64_t)value + (int64_t)divisor - 1) / (int64_t)divisor;
    if (result <= 0 || result > INT_MAX) {
        return false;
    }

    *out_result = (int)result;
    return true;
}

static dg_status_t dg_compute_template_process_scale_factor(
    const dg_generation_request_snapshot_t *snapshot,
    int *out_factor
)
{
    uint64_t factor;
    size_t i;

    if (snapshot == NULL || out_factor == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    factor = 1u;
    if (snapshot->process.enabled == 0 || snapshot->process.method_count == 0u) {
        *out_factor = 1;
        return DG_STATUS_OK;
    }

    if (snapshot->process.methods == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (i = 0; i < snapshot->process.method_count; ++i) {
        const dg_snapshot_process_method_t *method = &snapshot->process.methods[i];
        if ((dg_process_method_type_t)method->type == DG_PROCESS_METHOD_SCALE) {
            if (method->params.scale.factor < 1) {
                return DG_STATUS_INVALID_ARGUMENT;
            }
            factor *= (uint64_t)method->params.scale.factor;
            if (factor > (uint64_t)INT_MAX) {
                return DG_STATUS_GENERATION_FAILED;
            }
        }
    }

    *out_factor = (int)factor;
    return DG_STATUS_OK;
}

static dg_status_t dg_compute_template_generation_dimensions(
    const dg_generation_request_snapshot_t *snapshot,
    int target_width,
    int target_height,
    int *out_width,
    int *out_height
)
{
    dg_status_t status;
    int scale_factor;
    int generation_width;
    int generation_height;

    if (snapshot == NULL || out_width == NULL || out_height == NULL ||
        target_width <= 0 || target_height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_compute_template_process_scale_factor(snapshot, &scale_factor);
    if (status != DG_STATUS_OK) {
        return status;
    }

    if (!dg_div_ceil_positive_int(target_width, scale_factor, &generation_width) ||
        !dg_div_ceil_positive_int(target_height, scale_factor, &generation_height)) {
        return DG_STATUS_GENERATION_FAILED;
    }

    *out_width = generation_width;
    *out_height = generation_height;
    return DG_STATUS_OK;
}

static dg_status_t dg_scale_runtime_edge_openings_to_dimensions(
    const dg_edge_opening_spec_t *source_openings,
    size_t source_count,
    int source_width,
    int source_height,
    int target_width,
    int target_height,
    dg_edge_opening_spec_t **out_openings
)
{
    dg_edge_opening_spec_t *openings;
    size_t i;

    if (out_openings == NULL ||
        source_width <= 0 || source_height <= 0 ||
        target_width <= 0 || target_height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_openings = NULL;
    if (source_count == 0u) {
        return DG_STATUS_OK;
    }
    if (source_openings == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }
    if (source_count > (SIZE_MAX / sizeof(*openings))) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    openings = (dg_edge_opening_spec_t *)malloc(source_count * sizeof(*openings));
    if (openings == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (i = 0; i < source_count; ++i) {
        const dg_edge_opening_spec_t *src = &source_openings[i];
        int source_span;
        int target_span;
        int start;
        int end;
        int64_t scaled_start_num;
        int64_t scaled_end_num;
        int scaled_start;
        int scaled_end;

        openings[i].side = src->side;
        openings[i].role = src->role;
        if (src->side == DG_MAP_EDGE_TOP || src->side == DG_MAP_EDGE_BOTTOM) {
            source_span = source_width;
            target_span = target_width;
        } else if (src->side == DG_MAP_EDGE_LEFT || src->side == DG_MAP_EDGE_RIGHT) {
            source_span = source_height;
            target_span = target_height;
        } else {
            free(openings);
            return DG_STATUS_INVALID_ARGUMENT;
        }

        start = src->start;
        end = src->end;
        if (start < 0) {
            start = 0;
        }
        if (start >= source_span) {
            start = source_span - 1;
        }
        if (end < start) {
            end = start;
        }
        if (end >= source_span) {
            end = source_span - 1;
        }

        scaled_start_num = (int64_t)start * (int64_t)target_span;
        scaled_end_num = (int64_t)(end + 1) * (int64_t)target_span - 1;
        scaled_start = (int)(scaled_start_num / (int64_t)source_span);
        scaled_end = (int)(scaled_end_num / (int64_t)source_span);

        if (scaled_start < 0) {
            scaled_start = 0;
        }
        if (scaled_start >= target_span) {
            scaled_start = target_span - 1;
        }
        if (scaled_end < scaled_start) {
            scaled_end = scaled_start;
        }
        if (scaled_end >= target_span) {
            scaled_end = target_span - 1;
        }

        openings[i].start = scaled_start;
        openings[i].end = scaled_end;
    }

    *out_openings = openings;
    return DG_STATUS_OK;
}

static bool dg_point_in_rect_local(const dg_rect_t *rect, int x, int y)
{
    return rect != NULL &&
           x >= rect->x &&
           y >= rect->y &&
           x < rect->x + rect->width &&
           y < rect->y + rect->height;
}

static bool dg_point_in_any_room_local(const dg_map_t *map, int x, int y)
{
    size_t i;

    if (map == NULL || map->metadata.rooms == NULL) {
        return false;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        if (dg_point_in_rect_local(&map->metadata.rooms[i].bounds, x, y)) {
            return true;
        }
    }

    return false;
}

static bool dg_room_boundary_opens_to_corridor(
    const dg_map_t *map,
    const dg_rect_t *room,
    int room_x,
    int room_y,
    int outside_x,
    int outside_y
)
{
    if (map == NULL || room == NULL) {
        return false;
    }

    if (!dg_map_in_bounds(map, room_x, room_y) ||
        !dg_is_walkable_tile(dg_map_get_tile(map, room_x, room_y))) {
        return false;
    }

    if (!dg_map_in_bounds(map, outside_x, outside_y)) {
        return false;
    }

    if (dg_point_in_rect_local(room, outside_x, outside_y) ||
        dg_point_in_any_room_local(map, outside_x, outside_y)) {
        return false;
    }

    return dg_is_walkable_tile(dg_map_get_tile(map, outside_x, outside_y));
}

static dg_status_t dg_collect_room_entrance_openings(
    const dg_map_t *map,
    const dg_rect_t *room,
    dg_edge_opening_spec_t **out_openings,
    size_t *out_opening_count
)
{
    size_t max_openings;
    dg_edge_opening_spec_t *openings;
    size_t opening_count;
    int local_coord;
    int run_start;

    if (map == NULL || room == NULL || out_openings == NULL || out_opening_count == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_openings = NULL;
    *out_opening_count = 0u;
    if (room->width <= 0 || room->height <= 0) {
        return DG_STATUS_OK;
    }

    max_openings = (size_t)(room->width + room->height) * 2u + 4u;
    openings = (dg_edge_opening_spec_t *)calloc(max_openings, sizeof(*openings));
    if (openings == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    opening_count = 0u;

    run_start = -1;
    for (local_coord = 0; local_coord < room->width; ++local_coord) {
        int x = room->x + local_coord;
        int y = room->y;
        bool open = dg_room_boundary_opens_to_corridor(map, room, x, y, x, y - 1);

        if (open && run_start < 0) {
            run_start = local_coord;
        }
        if ((!open || local_coord == room->width - 1) && run_start >= 0) {
            int run_end = open ? local_coord : (local_coord - 1);
            if (opening_count >= max_openings) {
                free(openings);
                return DG_STATUS_ALLOCATION_FAILED;
            }
            openings[opening_count++] = (dg_edge_opening_spec_t){
                DG_MAP_EDGE_TOP,
                run_start,
                run_end,
                DG_MAP_EDGE_OPENING_ROLE_NONE
            };
            run_start = -1;
        }
    }

    run_start = -1;
    for (local_coord = 0; local_coord < room->width; ++local_coord) {
        int x = room->x + local_coord;
        int y = room->y + room->height - 1;
        bool open = dg_room_boundary_opens_to_corridor(map, room, x, y, x, y + 1);

        if (open && run_start < 0) {
            run_start = local_coord;
        }
        if ((!open || local_coord == room->width - 1) && run_start >= 0) {
            int run_end = open ? local_coord : (local_coord - 1);
            if (opening_count >= max_openings) {
                free(openings);
                return DG_STATUS_ALLOCATION_FAILED;
            }
            openings[opening_count++] = (dg_edge_opening_spec_t){
                DG_MAP_EDGE_BOTTOM,
                run_start,
                run_end,
                DG_MAP_EDGE_OPENING_ROLE_NONE
            };
            run_start = -1;
        }
    }

    run_start = -1;
    for (local_coord = 0; local_coord < room->height; ++local_coord) {
        int x = room->x;
        int y = room->y + local_coord;
        bool open = dg_room_boundary_opens_to_corridor(map, room, x, y, x - 1, y);

        if (open && run_start < 0) {
            run_start = local_coord;
        }
        if ((!open || local_coord == room->height - 1) && run_start >= 0) {
            int run_end = open ? local_coord : (local_coord - 1);
            if (opening_count >= max_openings) {
                free(openings);
                return DG_STATUS_ALLOCATION_FAILED;
            }
            openings[opening_count++] = (dg_edge_opening_spec_t){
                DG_MAP_EDGE_LEFT,
                run_start,
                run_end,
                DG_MAP_EDGE_OPENING_ROLE_NONE
            };
            run_start = -1;
        }
    }

    run_start = -1;
    for (local_coord = 0; local_coord < room->height; ++local_coord) {
        int x = room->x + room->width - 1;
        int y = room->y + local_coord;
        bool open = dg_room_boundary_opens_to_corridor(map, room, x, y, x + 1, y);

        if (open && run_start < 0) {
            run_start = local_coord;
        }
        if ((!open || local_coord == room->height - 1) && run_start >= 0) {
            int run_end = open ? local_coord : (local_coord - 1);
            if (opening_count >= max_openings) {
                free(openings);
                return DG_STATUS_ALLOCATION_FAILED;
            }
            openings[opening_count++] = (dg_edge_opening_spec_t){
                DG_MAP_EDGE_RIGHT,
                run_start,
                run_end,
                DG_MAP_EDGE_OPENING_ROLE_NONE
            };
            run_start = -1;
        }
    }

    if (opening_count == 0u) {
        free(openings);
        return DG_STATUS_OK;
    }

    *out_openings = openings;
    *out_opening_count = opening_count;
    return DG_STATUS_OK;
}

static bool dg_edge_side_normal(
    dg_map_edge_side_t side,
    int *out_normal_x,
    int *out_normal_y
)
{
    if (out_normal_x == NULL || out_normal_y == NULL) {
        return false;
    }

    switch (side) {
    case DG_MAP_EDGE_TOP:
        *out_normal_x = 0;
        *out_normal_y = 1;
        return true;
    case DG_MAP_EDGE_RIGHT:
        *out_normal_x = -1;
        *out_normal_y = 0;
        return true;
    case DG_MAP_EDGE_BOTTOM:
        *out_normal_x = 0;
        *out_normal_y = -1;
        return true;
    case DG_MAP_EDGE_LEFT:
        *out_normal_x = 1;
        *out_normal_y = 0;
        return true;
    default:
        return false;
    }
}

static void dg_apply_edge_opening_patch_and_anchor(
    dg_map_t *map,
    const dg_edge_opening_spec_t *opening,
    dg_point_t *out_anchor
)
{
    int span;
    int start;
    int end;
    int coord;
    int normal_x;
    int normal_y;
    int mid;
    int edge_x;
    int edge_y;
    int inward_x;
    int inward_y;

    if (map == NULL || map->tiles == NULL || opening == NULL || out_anchor == NULL) {
        return;
    }

    if (!dg_edge_side_normal(opening->side, &normal_x, &normal_y)) {
        *out_anchor = (dg_point_t){0, 0};
        return;
    }

    if (opening->side == DG_MAP_EDGE_TOP || opening->side == DG_MAP_EDGE_BOTTOM) {
        span = map->width;
    } else {
        span = map->height;
    }
    if (span <= 0) {
        *out_anchor = (dg_point_t){0, 0};
        return;
    }

    start = dg_clamp_int(opening->start, 0, span - 1);
    end = dg_clamp_int(opening->end, 0, span - 1);
    if (end < start) {
        end = start;
    }

    for (coord = start; coord <= end; ++coord) {
        int x;
        int y;
        int ix;
        int iy;

        switch (opening->side) {
        case DG_MAP_EDGE_TOP:
            x = coord;
            y = 0;
            break;
        case DG_MAP_EDGE_RIGHT:
            x = map->width - 1;
            y = coord;
            break;
        case DG_MAP_EDGE_BOTTOM:
            x = coord;
            y = map->height - 1;
            break;
        case DG_MAP_EDGE_LEFT:
            x = 0;
            y = coord;
            break;
        default:
            continue;
        }

        (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
        ix = x + normal_x;
        iy = y + normal_y;
        if (dg_map_in_bounds(map, ix, iy)) {
            (void)dg_map_set_tile(map, ix, iy, DG_TILE_FLOOR);
        }
    }

    mid = start + (end - start) / 2;
    switch (opening->side) {
    case DG_MAP_EDGE_TOP:
        edge_x = mid;
        edge_y = 0;
        break;
    case DG_MAP_EDGE_RIGHT:
        edge_x = map->width - 1;
        edge_y = mid;
        break;
    case DG_MAP_EDGE_BOTTOM:
        edge_x = mid;
        edge_y = map->height - 1;
        break;
    case DG_MAP_EDGE_LEFT:
        edge_x = 0;
        edge_y = mid;
        break;
    default:
        edge_x = 0;
        edge_y = 0;
        break;
    }

    inward_x = edge_x + normal_x;
    inward_y = edge_y + normal_y;
    if (dg_map_in_bounds(map, inward_x, inward_y)) {
        *out_anchor = (dg_point_t){inward_x, inward_y};
    } else {
        *out_anchor = (dg_point_t){edge_x, edge_y};
    }
}

static int dg_count_walls_on_straight_segment(
    const dg_map_t *map,
    int x0,
    int y0,
    int x1,
    int y1
)
{
    int count;
    int x_step;
    int y_step;
    int x;
    int y;

    if (map == NULL || map->tiles == NULL) {
        return INT_MAX;
    }

    count = 0;
    x = x0;
    y = y0;
    x_step = (x1 > x0) ? 1 : ((x1 < x0) ? -1 : 0);
    y_step = (y1 > y0) ? 1 : ((y1 < y0) ? -1 : 0);

    while (true) {
        if (!dg_map_in_bounds(map, x, y)) {
            return INT_MAX;
        }
        if (!dg_is_walkable_tile(dg_map_get_tile(map, x, y))) {
            count += 1;
        }
        if (x == x1 && y == y1) {
            break;
        }
        x += x_step;
        y += y_step;
    }

    return count;
}

static int dg_count_walls_on_hv_path(
    const dg_map_t *map,
    int x0,
    int y0,
    int x1,
    int y1
)
{
    int horizontal;
    int vertical;

    horizontal = dg_count_walls_on_straight_segment(map, x0, y0, x1, y0);
    vertical = dg_count_walls_on_straight_segment(map, x1, y0, x1, y1);
    if (horizontal == INT_MAX || vertical == INT_MAX) {
        return INT_MAX;
    }
    if (!dg_is_walkable_tile(dg_map_get_tile(map, x1, y0))) {
        /* Corner counted twice, remove duplicate count. */
        return horizontal + vertical - 1;
    }
    return horizontal + vertical;
}

static int dg_count_walls_on_vh_path(
    const dg_map_t *map,
    int x0,
    int y0,
    int x1,
    int y1
)
{
    int vertical;
    int horizontal;

    vertical = dg_count_walls_on_straight_segment(map, x0, y0, x0, y1);
    horizontal = dg_count_walls_on_straight_segment(map, x0, y1, x1, y1);
    if (vertical == INT_MAX || horizontal == INT_MAX) {
        return INT_MAX;
    }
    if (!dg_is_walkable_tile(dg_map_get_tile(map, x0, y1))) {
        return vertical + horizontal - 1;
    }
    return vertical + horizontal;
}

static void dg_carve_straight_segment(dg_map_t *map, int x0, int y0, int x1, int y1)
{
    int x_step;
    int y_step;
    int x;
    int y;

    if (map == NULL || map->tiles == NULL) {
        return;
    }

    x = x0;
    y = y0;
    x_step = (x1 > x0) ? 1 : ((x1 < x0) ? -1 : 0);
    y_step = (y1 > y0) ? 1 : ((y1 < y0) ? -1 : 0);

    while (true) {
        if (dg_map_in_bounds(map, x, y)) {
            (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
        }
        if (x == x1 && y == y1) {
            break;
        }
        x += x_step;
        y += y_step;
    }
}

static void dg_carve_low_cost_path(
    dg_map_t *map,
    dg_point_t from,
    dg_point_t to
)
{
    int cost_hv;
    int cost_vh;

    if (map == NULL || map->tiles == NULL) {
        return;
    }
    if (!dg_map_in_bounds(map, from.x, from.y) || !dg_map_in_bounds(map, to.x, to.y)) {
        return;
    }

    cost_hv = dg_count_walls_on_hv_path(map, from.x, from.y, to.x, to.y);
    cost_vh = dg_count_walls_on_vh_path(map, from.x, from.y, to.x, to.y);
    if (cost_hv <= cost_vh) {
        dg_carve_straight_segment(map, from.x, from.y, to.x, from.y);
        dg_carve_straight_segment(map, to.x, from.y, to.x, to.y);
    } else {
        dg_carve_straight_segment(map, from.x, from.y, from.x, to.y);
        dg_carve_straight_segment(map, from.x, to.y, to.x, to.y);
    }
}

static bool dg_walkable_path_exists(
    const dg_map_t *map,
    dg_point_t start,
    dg_point_t goal
)
{
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    size_t cell_count;
    unsigned char *visited;
    size_t *queue;
    size_t head;
    size_t tail;
    size_t goal_index;
    int width;

    if (map == NULL || map->tiles == NULL) {
        return false;
    }
    if (!dg_map_in_bounds(map, start.x, start.y) || !dg_map_in_bounds(map, goal.x, goal.y)) {
        return false;
    }
    if (!dg_is_walkable_tile(dg_map_get_tile(map, start.x, start.y)) ||
        !dg_is_walkable_tile(dg_map_get_tile(map, goal.x, goal.y))) {
        return false;
    }
    if (start.x == goal.x && start.y == goal.y) {
        return true;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    visited = (unsigned char *)calloc(cell_count, sizeof(*visited));
    queue = (size_t *)malloc(cell_count * sizeof(*queue));
    if (visited == NULL || queue == NULL) {
        free(queue);
        free(visited);
        return false;
    }

    width = map->width;
    goal_index = dg_tile_index(map, goal.x, goal.y);
    head = 0u;
    tail = 0u;
    queue[tail++] = dg_tile_index(map, start.x, start.y);
    visited[queue[0]] = 1u;

    while (head < tail) {
        size_t index = queue[head++];
        int x = (int)(index % (size_t)width);
        int y = (int)(index / (size_t)width);
        int d;

        for (d = 0; d < 4; ++d) {
            int nx = x + directions[d][0];
            int ny = y + directions[d][1];
            size_t nindex;

            if (!dg_map_in_bounds(map, nx, ny)) {
                continue;
            }
            if (!dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
                continue;
            }

            nindex = dg_tile_index(map, nx, ny);
            if (visited[nindex] != 0u) {
                continue;
            }

            if (nindex == goal_index) {
                free(queue);
                free(visited);
                return true;
            }

            visited[nindex] = 1u;
            queue[tail++] = nindex;
        }
    }

    free(queue);
    free(visited);
    return false;
}

static bool dg_walkable_reaches_base_tiles(
    const dg_map_t *map,
    dg_point_t start,
    const dg_tile_t *base_tiles,
    const unsigned char *exclude_mask
)
{
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    size_t cell_count;
    unsigned char *visited;
    size_t *queue;
    size_t head;
    size_t tail;
    int width;

    if (map == NULL || map->tiles == NULL || base_tiles == NULL) {
        return false;
    }
    if (!dg_map_in_bounds(map, start.x, start.y)) {
        return false;
    }
    if (!dg_is_walkable_tile(dg_map_get_tile(map, start.x, start.y))) {
        return false;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    visited = (unsigned char *)calloc(cell_count, sizeof(*visited));
    queue = (size_t *)malloc(cell_count * sizeof(*queue));
    if (visited == NULL || queue == NULL) {
        free(queue);
        free(visited);
        return false;
    }

    width = map->width;
    head = 0u;
    tail = 0u;
    queue[tail++] = dg_tile_index(map, start.x, start.y);
    visited[queue[0]] = 1u;

    while (head < tail) {
        size_t index = queue[head++];
        int x = (int)(index % (size_t)width);
        int y = (int)(index / (size_t)width);
        int d;

        if (dg_is_walkable_tile(base_tiles[index]) &&
            (exclude_mask == NULL || exclude_mask[index] == 0u)) {
            free(queue);
            free(visited);
            return true;
        }

        for (d = 0; d < 4; ++d) {
            int nx = x + directions[d][0];
            int ny = y + directions[d][1];
            size_t nindex;

            if (!dg_map_in_bounds(map, nx, ny)) {
                continue;
            }
            if (!dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
                continue;
            }

            nindex = dg_tile_index(map, nx, ny);
            if (visited[nindex] != 0u) {
                continue;
            }

            visited[nindex] = 1u;
            queue[tail++] = nindex;
        }
    }

    free(queue);
    free(visited);
    return false;
}

static bool dg_find_nearest_walkable_in_tiles(
    const dg_map_t *map,
    const dg_tile_t *tiles,
    dg_point_t from,
    const unsigned char *exclude_mask,
    dg_point_t *out_target
)
{
    size_t i;
    size_t cell_count;
    int best_distance;
    bool found;
    dg_point_t best_point;

    if (map == NULL || map->tiles == NULL || tiles == NULL || out_target == NULL) {
        return false;
    }
    if (!dg_map_in_bounds(map, from.x, from.y)) {
        return false;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    best_distance = INT_MAX;
    best_point = from;
    found = false;

    for (i = 0u; i < cell_count; ++i) {
        int x;
        int y;
        int distance;

        if (!dg_is_walkable_tile(tiles[i])) {
            continue;
        }
        if (exclude_mask != NULL && exclude_mask[i] != 0u) {
            continue;
        }

        x = (int)(i % (size_t)map->width);
        y = (int)(i / (size_t)map->width);
        distance = abs(x - from.x) + abs(y - from.y);
        if (distance == 0) {
            continue;
        }
        if (distance < best_distance) {
            best_distance = distance;
            best_point = (dg_point_t){x, y};
            found = true;
        }
    }

    if (!found) {
        return false;
    }
    *out_target = best_point;
    return true;
}

static bool dg_detect_rooms_and_mazes_room_parity(
    const dg_map_t *map,
    int *out_parity_x,
    int *out_parity_y
)
{
    size_t i;

    if (map == NULL || out_parity_x == NULL || out_parity_y == NULL ||
        map->metadata.algorithm_id != (int)DG_ALGORITHM_ROOMS_AND_MAZES ||
        map->metadata.rooms == NULL ||
        map->metadata.room_count == 0u) {
        return false;
    }

    for (i = 0u; i < map->metadata.room_count; ++i) {
        const dg_rect_t *bounds = &map->metadata.rooms[i].bounds;
        if (bounds->width > 0 && bounds->height > 0) {
            *out_parity_x = bounds->x & 1;
            *out_parity_y = bounds->y & 1;
            return true;
        }
    }

    return false;
}

static void dg_align_room_like_span_for_parity(
    int *start,
    int *end,
    int min_coord,
    int max_coord,
    int parity
)
{
    if (start == NULL || end == NULL || min_coord > max_coord) {
        return;
    }

    if ((*start & 1) != parity) {
        if (*start > min_coord) {
            *start -= 1;
        } else if (*end < max_coord) {
            *end += 1;
        }
    }

    if (((*end - *start + 1) & 1) == 0) {
        if (*end < max_coord) {
            *end += 1;
        } else if (*start > min_coord) {
            *start -= 1;
        }
    }
}

static bool dg_build_room_like_entrance_rect(
    const dg_map_t *map,
    const dg_edge_opening_spec_t *opening,
    int depth,
    int parity_enabled,
    int parity_x,
    int parity_y,
    dg_rect_t *out_rect
)
{
    int span;
    int start;
    int end;
    int max_depth;
    dg_rect_t rect;

    if (map == NULL || opening == NULL || out_rect == NULL ||
        map->width <= 0 || map->height <= 0) {
        return false;
    }

    if (opening->side == DG_MAP_EDGE_TOP || opening->side == DG_MAP_EDGE_BOTTOM) {
        span = map->width;
        max_depth = map->height;
    } else if (opening->side == DG_MAP_EDGE_LEFT || opening->side == DG_MAP_EDGE_RIGHT) {
        span = map->height;
        max_depth = map->width;
    } else {
        return false;
    }
    if (span <= 0 || max_depth <= 0) {
        return false;
    }

    start = dg_clamp_int(opening->start, 0, span - 1);
    end = dg_clamp_int(opening->end, 0, span - 1);
    if (end < start) {
        end = start;
    }

    if (parity_enabled != 0) {
        if (opening->side == DG_MAP_EDGE_TOP || opening->side == DG_MAP_EDGE_BOTTOM) {
            dg_align_room_like_span_for_parity(&start, &end, 0, span - 1, parity_x);
        } else {
            dg_align_room_like_span_for_parity(&start, &end, 0, span - 1, parity_y);
        }
    }

    depth = dg_clamp_int(depth, 1, max_depth);
    if (parity_enabled != 0 && (depth & 1) == 0) {
        if (depth < max_depth) {
            depth += 1;
        } else if (depth > 1) {
            depth -= 1;
        }
    }

    rect = (dg_rect_t){0, 0, 1, 1};
    if (opening->side == DG_MAP_EDGE_TOP) {
        rect.x = start;
        rect.y = 0;
        rect.width = (end - start) + 1;
        rect.height = depth;
    } else if (opening->side == DG_MAP_EDGE_BOTTOM) {
        rect.x = start;
        rect.width = (end - start) + 1;
        rect.height = depth;
        rect.y = map->height - rect.height;
    } else if (opening->side == DG_MAP_EDGE_LEFT) {
        rect.x = 0;
        rect.y = start;
        rect.width = depth;
        rect.height = (end - start) + 1;
    } else {
        rect.y = start;
        rect.width = depth;
        rect.height = (end - start) + 1;
        rect.x = map->width - rect.width;
    }

    if (rect.width <= 0 || rect.height <= 0 ||
        rect.x < 0 || rect.y < 0 ||
        rect.x + rect.width > map->width ||
        rect.y + rect.height > map->height) {
        return false;
    }

    *out_rect = rect;
    return true;
}

static bool dg_room_like_rect_touches_walkable(const dg_map_t *map, const dg_rect_t *rect)
{
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    int y;
    int x;
    int d;

    if (map == NULL || map->tiles == NULL || rect == NULL) {
        return false;
    }

    for (y = rect->y; y < rect->y + rect->height; ++y) {
        for (x = rect->x; x < rect->x + rect->width; ++x) {
            if (!dg_map_in_bounds(map, x, y)) {
                continue;
            }
            if (dg_is_walkable_tile(dg_map_get_tile(map, x, y))) {
                return true;
            }
            for (d = 0; d < 4; ++d) {
                int nx = x + directions[d][0];
                int ny = y + directions[d][1];
                if (!dg_map_in_bounds(map, nx, ny)) {
                    continue;
                }
                if (dg_point_in_rect_local(rect, nx, ny)) {
                    continue;
                }
                if (dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
                    return true;
                }
            }
        }
    }

    return false;
}

static void dg_paint_room_like_rect(dg_map_t *map, const dg_rect_t *rect)
{
    int y;
    int x;

    if (map == NULL || map->tiles == NULL || rect == NULL) {
        return;
    }

    for (y = rect->y; y < rect->y + rect->height; ++y) {
        for (x = rect->x; x < rect->x + rect->width; ++x) {
            if (dg_map_in_bounds(map, x, y)) {
                (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
            }
        }
    }
}

static void dg_place_room_like_entrance_room(
    dg_map_t *map,
    const dg_edge_opening_spec_t *opening,
    int parity_enabled,
    int parity_x,
    int parity_y
)
{
    int span;
    int start;
    int end;
    int length;
    int base_depth;
    int max_depth;
    int step;
    int depth;
    dg_rect_t fallback_rect;
    dg_rect_t chosen_rect;
    bool has_fallback;
    bool found_connected;

    if (map == NULL || map->tiles == NULL || opening == NULL) {
        return;
    }

    if (opening->side == DG_MAP_EDGE_TOP || opening->side == DG_MAP_EDGE_BOTTOM) {
        span = map->width;
        max_depth = map->height;
    } else if (opening->side == DG_MAP_EDGE_LEFT || opening->side == DG_MAP_EDGE_RIGHT) {
        span = map->height;
        max_depth = map->width;
    } else {
        return;
    }
    if (span <= 0 || max_depth <= 0) {
        return;
    }

    start = dg_clamp_int(opening->start, 0, span - 1);
    end = dg_clamp_int(opening->end, 0, span - 1);
    if (end < start) {
        end = start;
    }

    length = (end - start) + 1;
    base_depth = dg_clamp_int(length, 2, 8);
    max_depth = dg_min_int(max_depth, dg_max_int(base_depth, 12));
    if (base_depth > max_depth) {
        base_depth = max_depth;
    }

    if (parity_enabled != 0 && (base_depth & 1) == 0) {
        if (base_depth < max_depth) {
            base_depth += 1;
        } else if (base_depth > 1) {
            base_depth -= 1;
        }
    }

    step = (parity_enabled != 0) ? 2 : 1;
    if (step < 1) {
        step = 1;
    }

    has_fallback = false;
    found_connected = false;
    chosen_rect = (dg_rect_t){0, 0, 1, 1};
    fallback_rect = chosen_rect;

    for (depth = base_depth; depth <= max_depth; depth += step) {
        dg_rect_t rect;

        if (!dg_build_room_like_entrance_rect(
                map,
                opening,
                depth,
                parity_enabled,
                parity_x,
                parity_y,
                &rect
            )) {
            continue;
        }

        if (!has_fallback) {
            fallback_rect = rect;
            has_fallback = true;
        }
        if (dg_room_like_rect_touches_walkable(map, &rect)) {
            chosen_rect = rect;
            found_connected = true;
            break;
        }
    }

    if (!has_fallback) {
        return;
    }
    if (!found_connected) {
        chosen_rect = fallback_rect;
    }

    dg_paint_room_like_rect(map, &chosen_rect);
}

static dg_status_t dg_enforce_template_opening_connectivity(
    dg_map_t *map,
    const dg_edge_opening_spec_t *openings,
    size_t opening_count,
    int use_room_like_entrance_rooms
)
{
    size_t cell_count;
    dg_tile_t *base_tiles;
    dg_point_t *anchors;
    size_t i;

    if (map == NULL || map->tiles == NULL || openings == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }
    if (opening_count == 0u) {
        return DG_STATUS_OK;
    }

    if (use_room_like_entrance_rooms != 0) {
        int parity_x = 0;
        int parity_y = 0;
        int parity_enabled = dg_detect_rooms_and_mazes_room_parity(map, &parity_x, &parity_y) ? 1 : 0;

        for (i = 0u; i < opening_count; ++i) {
            dg_place_room_like_entrance_room(
                map,
                &openings[i],
                parity_enabled,
                parity_x,
                parity_y
            );
        }
        return DG_STATUS_OK;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    base_tiles = (dg_tile_t *)malloc(cell_count * sizeof(*base_tiles));
    anchors = (dg_point_t *)malloc(opening_count * sizeof(*anchors));
    if (base_tiles == NULL || anchors == NULL) {
        free(anchors);
        free(base_tiles);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    memcpy(base_tiles, map->tiles, cell_count * sizeof(*base_tiles));
    for (i = 0u; i < opening_count; ++i) {
        dg_apply_edge_opening_patch_and_anchor(map, &openings[i], &anchors[i]);
    }

    for (i = 1u; i < opening_count; ++i) {
        if (!dg_walkable_path_exists(map, anchors[0], anchors[i])) {
            dg_carve_low_cost_path(map, anchors[0], anchors[i]);
        }
    }

    for (i = 0u; i < opening_count; ++i) {
        dg_point_t target;
        if (dg_walkable_reaches_base_tiles(
                map,
                anchors[i],
                base_tiles,
                NULL
            )) {
            continue;
        }
        if (dg_find_nearest_walkable_in_tiles(
                map,
                base_tiles,
                anchors[i],
                NULL,
                &target
            )) {
            if (!dg_walkable_path_exists(map, anchors[i], target)) {
                dg_carve_low_cost_path(map, anchors[i], target);
            }
        }
    }

    free(anchors);
    free(base_tiles);
    return DG_STATUS_OK;
}

static dg_status_t dg_build_template_request_from_snapshot(
    const dg_generation_request_snapshot_t *snapshot,
    int width,
    int height,
    uint64_t seed,
    dg_generate_request_t *out_request,
    dg_process_method_t **out_process_methods,
    dg_edge_opening_spec_t **out_edge_openings
)
{
    dg_generate_request_t request;
    dg_process_method_t *process_methods;
    dg_edge_opening_spec_t *edge_openings;
    size_t i;
    int source_span_h;
    int source_span_v;

    if (snapshot == NULL || out_request == NULL ||
        out_process_methods == NULL || out_edge_openings == NULL ||
        snapshot->present != 1 ||
        width <= 0 || height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_process_methods = NULL;
    *out_edge_openings = NULL;
    process_methods = NULL;
    edge_openings = NULL;

    dg_default_generate_request(&request, (dg_algorithm_t)snapshot->algorithm_id, width, height, seed);
    switch ((dg_algorithm_t)snapshot->algorithm_id) {
    case DG_ALGORITHM_BSP_TREE:
        request.params.bsp = (dg_bsp_config_t){
            snapshot->params.bsp.min_rooms,
            snapshot->params.bsp.max_rooms,
            snapshot->params.bsp.room_min_size,
            snapshot->params.bsp.room_max_size
        };
        break;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        request.params.drunkards_walk = (dg_drunkards_walk_config_t){
            snapshot->params.drunkards_walk.wiggle_percent
        };
        break;
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        request.params.cellular_automata = (dg_cellular_automata_config_t){
            snapshot->params.cellular_automata.initial_wall_percent,
            snapshot->params.cellular_automata.simulation_steps,
            snapshot->params.cellular_automata.wall_threshold
        };
        break;
    case DG_ALGORITHM_VALUE_NOISE:
        request.params.value_noise = (dg_value_noise_config_t){
            snapshot->params.value_noise.feature_size,
            snapshot->params.value_noise.octaves,
            snapshot->params.value_noise.persistence_percent,
            snapshot->params.value_noise.floor_threshold_percent
        };
        break;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        request.params.rooms_and_mazes = (dg_rooms_and_mazes_config_t){
            snapshot->params.rooms_and_mazes.min_rooms,
            snapshot->params.rooms_and_mazes.max_rooms,
            snapshot->params.rooms_and_mazes.room_min_size,
            snapshot->params.rooms_and_mazes.room_max_size,
            snapshot->params.rooms_and_mazes.maze_wiggle_percent,
            snapshot->params.rooms_and_mazes.min_room_connections,
            snapshot->params.rooms_and_mazes.max_room_connections,
            snapshot->params.rooms_and_mazes.ensure_full_connectivity,
            snapshot->params.rooms_and_mazes.dead_end_prune_steps
        };
        break;
    case DG_ALGORITHM_ROOM_GRAPH:
        request.params.room_graph = (dg_room_graph_config_t){
            snapshot->params.room_graph.min_rooms,
            snapshot->params.room_graph.max_rooms,
            snapshot->params.room_graph.room_min_size,
            snapshot->params.room_graph.room_max_size,
            snapshot->params.room_graph.neighbor_candidates,
            snapshot->params.room_graph.extra_connection_chance_percent
        };
        break;
    case DG_ALGORITHM_WORM_CAVES:
        request.params.worm_caves = (dg_worm_caves_config_t){
            snapshot->params.worm_caves.worm_count,
            snapshot->params.worm_caves.wiggle_percent,
            snapshot->params.worm_caves.branch_chance_percent,
            snapshot->params.worm_caves.target_floor_percent,
            snapshot->params.worm_caves.brush_radius,
            snapshot->params.worm_caves.max_steps_per_worm,
            snapshot->params.worm_caves.ensure_connected
        };
        break;
    case DG_ALGORITHM_SIMPLEX_NOISE:
        request.params.simplex_noise = (dg_simplex_noise_config_t){
            snapshot->params.simplex_noise.feature_size,
            snapshot->params.simplex_noise.octaves,
            snapshot->params.simplex_noise.persistence_percent,
            snapshot->params.simplex_noise.floor_threshold_percent,
            snapshot->params.simplex_noise.ensure_connected
        };
        break;
    default:
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (snapshot->process.method_count > 0u) {
        if (snapshot->process.methods == NULL ||
            snapshot->process.method_count > (SIZE_MAX / sizeof(*process_methods))) {
            return DG_STATUS_INVALID_ARGUMENT;
        }
        process_methods = (dg_process_method_t *)malloc(
            snapshot->process.method_count * sizeof(*process_methods)
        );
        if (process_methods == NULL) {
            return DG_STATUS_ALLOCATION_FAILED;
        }
        for (i = 0; i < snapshot->process.method_count; ++i) {
            process_methods[i].type = (dg_process_method_type_t)snapshot->process.methods[i].type;
            switch (process_methods[i].type) {
            case DG_PROCESS_METHOD_SCALE:
                process_methods[i].params.scale.factor = snapshot->process.methods[i].params.scale.factor;
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
                dg_release_template_request_buffers(process_methods, edge_openings);
                return DG_STATUS_INVALID_ARGUMENT;
            }
        }
    }
    request.process.enabled = snapshot->process.enabled;
    request.process.methods = process_methods;
    request.process.method_count = snapshot->process.method_count;

    source_span_h = snapshot->width > 0 ? snapshot->width : 1;
    source_span_v = snapshot->height > 0 ? snapshot->height : 1;
    if (snapshot->edge_openings.opening_count > 0u) {
        if (snapshot->edge_openings.openings == NULL ||
            snapshot->edge_openings.opening_count > (SIZE_MAX / sizeof(*edge_openings))) {
            dg_release_template_request_buffers(process_methods, edge_openings);
            return DG_STATUS_INVALID_ARGUMENT;
        }

        edge_openings = (dg_edge_opening_spec_t *)malloc(
            snapshot->edge_openings.opening_count * sizeof(*edge_openings)
        );
        if (edge_openings == NULL) {
            dg_release_template_request_buffers(process_methods, edge_openings);
            return DG_STATUS_ALLOCATION_FAILED;
        }

        for (i = 0; i < snapshot->edge_openings.opening_count; ++i) {
            const dg_snapshot_edge_opening_spec_t *src = &snapshot->edge_openings.openings[i];
            int target_span;
            int64_t scaled_start_num;
            int64_t scaled_end_num;
            int scaled_start;
            int scaled_end;

            edge_openings[i].side = (dg_map_edge_side_t)src->side;
            edge_openings[i].role = (dg_map_edge_opening_role_t)src->role;
            if (edge_openings[i].side == DG_MAP_EDGE_TOP || edge_openings[i].side == DG_MAP_EDGE_BOTTOM) {
                target_span = width;
                scaled_start_num = (int64_t)src->start * (int64_t)target_span;
                scaled_end_num = (int64_t)(src->end + 1) * (int64_t)target_span - 1;
                scaled_start = (int)(scaled_start_num / (int64_t)source_span_h);
                scaled_end = (int)(scaled_end_num / (int64_t)source_span_h);
                if (scaled_start < 0) {
                    scaled_start = 0;
                }
                if (scaled_end < scaled_start) {
                    scaled_end = scaled_start;
                }
                if (scaled_end >= width) {
                    scaled_end = width - 1;
                }
            } else {
                target_span = height;
                scaled_start_num = (int64_t)src->start * (int64_t)target_span;
                scaled_end_num = (int64_t)(src->end + 1) * (int64_t)target_span - 1;
                scaled_start = (int)(scaled_start_num / (int64_t)source_span_v);
                scaled_end = (int)(scaled_end_num / (int64_t)source_span_v);
                if (scaled_start < 0) {
                    scaled_start = 0;
                }
                if (scaled_end < scaled_start) {
                    scaled_end = scaled_start;
                }
                if (scaled_end >= height) {
                    scaled_end = height - 1;
                }
            }
            edge_openings[i].start = scaled_start;
            edge_openings[i].end = scaled_end;
        }
    }

    request.edge_openings.openings = edge_openings;
    request.edge_openings.opening_count = snapshot->edge_openings.opening_count;
    request.room_types.definitions = NULL;
    request.room_types.definition_count = 0u;
    dg_default_room_type_assignment_policy(&request.room_types.policy);

    *out_request = request;
    *out_process_methods = process_methods;
    *out_edge_openings = edge_openings;
    return DG_STATUS_OK;
}

static int dg_resample_coordinate_centered(int dst_index, int dst_span, int src_span)
{
    int64_t numerator;
    int64_t denominator;
    int source_index;

    if (dst_span <= 0 || src_span <= 0) {
        return 0;
    }
    if (dst_span == src_span) {
        return dst_index;
    }

    numerator = ((int64_t)dst_index * 2 + 1) * (int64_t)src_span;
    denominator = (int64_t)dst_span * 2;
    source_index = (int)(numerator / denominator);
    if (source_index < 0) {
        source_index = 0;
    }
    if (source_index >= src_span) {
        source_index = src_span - 1;
    }
    return source_index;
}

static dg_status_t dg_apply_template_to_room(
    dg_map_t *map,
    const dg_rect_t *room,
    const dg_map_t *template_map
)
{
    int local_x;
    int local_y;

    if (map == NULL || map->tiles == NULL || room == NULL ||
        template_map == NULL || template_map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (room->width <= 0 || room->height <= 0 ||
        template_map->width <= 0 || template_map->height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (local_y = 0; local_y < room->height; ++local_y) {
        for (local_x = 0; local_x < room->width; ++local_x) {
            int world_x;
            int world_y;
            int source_x;
            int source_y;
            dg_tile_t source_tile;

            world_x = room->x + local_x;
            world_y = room->y + local_y;
            if (!dg_map_in_bounds(map, world_x, world_y)) {
                continue;
            }
            source_x = dg_resample_coordinate_centered(local_x, room->width, template_map->width);
            source_y = dg_resample_coordinate_centered(local_y, room->height, template_map->height);
            source_tile = template_map->tiles[dg_tile_index(template_map, source_x, source_y)];
            map->tiles[dg_tile_index(map, world_x, world_y)] =
                dg_is_walkable_tile(source_tile) ? DG_TILE_FLOOR : DG_TILE_WALL;
        }
    }

    return DG_STATUS_OK;
}

dg_status_t dg_apply_room_type_templates(
    const dg_generate_request_t *request,
    dg_map_t *map
)
{
    dg_room_template_cache_entry_t *cache_entries;
    size_t cache_count;
    size_t untyped_cache_index;
    size_t i;
    dg_status_t status;
    bool has_any_templates;
    bool has_untyped_template;

    if (request == NULL || map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.generation_class != DG_MAP_GENERATION_CLASS_ROOM_LIKE ||
        map->metadata.rooms == NULL ||
        map->metadata.room_count == 0) {
        return DG_STATUS_OK;
    }

    if (request->room_types.definition_count > 0u &&
        request->room_types.definitions == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    has_untyped_template = request->room_types.policy.untyped_template_map_path[0] != '\0';
    has_any_templates = false;
    for (i = 0; i < request->room_types.definition_count; ++i) {
        if (request->room_types.definitions[i].template_map_path[0] != '\0') {
            has_any_templates = true;
        }
    }
    if (!has_any_templates && has_untyped_template) {
        has_any_templates = true;
    }
    if (!has_any_templates) {
        return DG_STATUS_OK;
    }

    if (dg_room_template_application_depth > 0) {
        return DG_STATUS_GENERATION_FAILED;
    }
    dg_room_template_application_depth += 1;
    cache_entries = NULL;
    status = DG_STATUS_OK;
    cache_count = request->room_types.definition_count + (has_untyped_template ? 1u : 0u);
    untyped_cache_index = request->room_types.definition_count;

    if (cache_count > (SIZE_MAX / sizeof(*cache_entries))) {
        status = DG_STATUS_ALLOCATION_FAILED;
        goto cleanup;
    }
    cache_entries = (dg_room_template_cache_entry_t *)calloc(
        cache_count,
        sizeof(*cache_entries)
    );
    if (cache_entries == NULL) {
        status = DG_STATUS_ALLOCATION_FAILED;
        goto cleanup;
    }

    for (i = 0; i < request->room_types.definition_count; ++i) {
        const dg_room_type_definition_t *definition = &request->room_types.definitions[i];
        dg_room_template_cache_entry_t *entry = &cache_entries[i];

        if (definition->template_map_path[0] == '\0') {
            continue;
        }

        entry->has_template = 1;
        entry->map = (dg_map_t){0};
        status = dg_map_load_file(definition->template_map_path, &entry->map);
        if (status != DG_STATUS_OK) {
            goto cleanup;
        }
        entry->loaded = 1;

        status = dg_validate_loaded_room_template(&entry->map);
        if (status != DG_STATUS_OK) {
            goto cleanup;
        }
    }

    if (has_untyped_template) {
        dg_room_template_cache_entry_t *entry = &cache_entries[untyped_cache_index];
        entry->has_template = 1;
        entry->map = (dg_map_t){0};
        status = dg_map_load_file(request->room_types.policy.untyped_template_map_path, &entry->map);
        if (status != DG_STATUS_OK) {
            goto cleanup;
        }
        entry->loaded = 1;

        status = dg_validate_loaded_room_template(&entry->map);
        if (status != DG_STATUS_OK) {
            goto cleanup;
        }
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map->metadata.rooms[i];
        const dg_map_edge_opening_query_t *opening_query;
        int required_opening_matches;
        size_t definition_index;
        dg_room_template_cache_entry_t *entry;
        dg_generate_request_t template_request;
        dg_process_method_t *process_methods;
        dg_edge_opening_spec_t *edge_openings;
        dg_edge_opening_spec_t *room_openings;
        dg_edge_opening_spec_t *connectivity_openings;
        size_t room_opening_count;
        size_t connectivity_opening_count;
        dg_map_t generated_template;
        int template_width;
        int template_height;
        int template_scale_factor;
        int attempt_width;
        int attempt_height;
        int attempt_index;
        size_t opening_match_count;
        int use_room_like_entrance_rooms;

        if (room->type_id == DG_ROOM_TYPE_UNASSIGNED) {
            if (!has_untyped_template) {
                continue;
            }
            entry = &cache_entries[untyped_cache_index];
            if (entry->has_template == 0 || entry->loaded == 0) {
                continue;
            }
            opening_query = NULL;
            required_opening_matches = 0;
        } else {
            const dg_room_type_definition_t *definition;
            definition_index = dg_find_room_type_definition_index_by_type_id(request, room->type_id);
            if (definition_index == SIZE_MAX) {
                continue;
            }

            entry = &cache_entries[definition_index];
            if (entry->has_template == 0 || entry->loaded == 0) {
                continue;
            }

            definition = &request->room_types.definitions[definition_index];
            opening_query = &definition->template_opening_query;
            required_opening_matches = definition->template_required_opening_matches;
        }

        room_openings = NULL;
        room_opening_count = 0u;
        status = dg_collect_room_entrance_openings(
            map,
            &room->bounds,
            &room_openings,
            &room_opening_count
        );
        if (status != DG_STATUS_OK) {
            goto cleanup;
        }

        status = dg_compute_template_generation_dimensions(
            &entry->map.metadata.generation_request,
            room->bounds.width,
            room->bounds.height,
            &template_width,
            &template_height
        );
        if (status != DG_STATUS_OK) {
            free(room_openings);
            goto cleanup;
        }

        status = dg_compute_template_process_scale_factor(
            &entry->map.metadata.generation_request,
            &template_scale_factor
        );
        if (status != DG_STATUS_OK) {
            free(room_openings);
            goto cleanup;
        }

        template_request = (dg_generate_request_t){0};
        process_methods = NULL;
        edge_openings = NULL;
        generated_template = (dg_map_t){0};

        for (attempt_index = 0; attempt_index < 4; ++attempt_index) {
            attempt_width = template_width + attempt_index;
            attempt_height = template_height + attempt_index;
            if (attempt_width > room->bounds.width) {
                attempt_width = room->bounds.width;
            }
            if (attempt_height > room->bounds.height) {
                attempt_height = room->bounds.height;
            }
            if (template_scale_factor > 1) {
                int max_attempt_width = dg_max_int(1, room->bounds.width - 1);
                int max_attempt_height = dg_max_int(1, room->bounds.height - 1);
                if (attempt_width > max_attempt_width) {
                    attempt_width = max_attempt_width;
                }
                if (attempt_height > max_attempt_height) {
                    attempt_height = max_attempt_height;
                }
            }

            status = dg_build_template_request_from_snapshot(
                &entry->map.metadata.generation_request,
                attempt_width,
                attempt_height,
                entry->map.metadata.seed ^
                    ((uint64_t)(room->id + 1) * UINT64_C(11400714819323198485)) ^
                    ((uint64_t)(attempt_index + 1) * UINT64_C(14029467366897019727)),
                &template_request,
                &process_methods,
                &edge_openings
            );
            if (status != DG_STATUS_OK) {
                break;
            }

            if (room_opening_count > 0u) {
                dg_edge_opening_spec_t *scaled_room_openings;

                scaled_room_openings = NULL;
                status = dg_scale_runtime_edge_openings_to_dimensions(
                    room_openings,
                    room_opening_count,
                    room->bounds.width,
                    room->bounds.height,
                    template_request.width,
                    template_request.height,
                    &scaled_room_openings
                );
                if (status != DG_STATUS_OK) {
                    dg_release_template_request_buffers(process_methods, edge_openings);
                    process_methods = NULL;
                    edge_openings = NULL;
                    break;
                }

                free(edge_openings);
                edge_openings = scaled_room_openings;
                template_request.edge_openings.openings = edge_openings;
                template_request.edge_openings.opening_count = room_opening_count;
            }

            status = dg_generate_internal_allow_small(&template_request, &generated_template);
            if (status == DG_STATUS_OK) {
                break;
            }

            dg_release_template_request_buffers(process_methods, edge_openings);
            process_methods = NULL;
            edge_openings = NULL;
            dg_map_destroy(&generated_template);
            generated_template = (dg_map_t){0};

            if (status != DG_STATUS_GENERATION_FAILED || attempt_index + 1 >= 4) {
                break;
            }
        }

        if (status != DG_STATUS_OK) {
            dg_release_template_request_buffers(process_methods, edge_openings);
            free(room_openings);
            goto cleanup;
        }

        if (required_opening_matches > 0 && opening_query != NULL) {
            opening_match_count = dg_map_query_edge_openings(
                &generated_template,
                opening_query,
                NULL,
                0u
            );
            if (opening_match_count < (size_t)required_opening_matches) {
                dg_release_template_request_buffers(process_methods, edge_openings);
                dg_map_destroy(&generated_template);
                free(room_openings);
                status = DG_STATUS_GENERATION_FAILED;
                goto cleanup;
            }
        }

        connectivity_openings = NULL;
        connectivity_opening_count = 0u;
        if (room_opening_count > 0u) {
            status = dg_scale_runtime_edge_openings_to_dimensions(
                room_openings,
                room_opening_count,
                room->bounds.width,
                room->bounds.height,
                generated_template.width,
                generated_template.height,
                &connectivity_openings
            );
            if (status != DG_STATUS_OK) {
                dg_release_template_request_buffers(process_methods, edge_openings);
                dg_map_destroy(&generated_template);
                free(room_openings);
                goto cleanup;
            }
            connectivity_opening_count = room_opening_count;
        } else if (template_request.edge_openings.opening_count > 0u &&
                   template_request.edge_openings.openings != NULL) {
            status = dg_scale_runtime_edge_openings_to_dimensions(
                template_request.edge_openings.openings,
                template_request.edge_openings.opening_count,
                template_request.width,
                template_request.height,
                generated_template.width,
                generated_template.height,
                &connectivity_openings
            );
            if (status != DG_STATUS_OK) {
                dg_release_template_request_buffers(process_methods, edge_openings);
                dg_map_destroy(&generated_template);
                free(room_openings);
                goto cleanup;
            }
            connectivity_opening_count = template_request.edge_openings.opening_count;
        }

        use_room_like_entrance_rooms = 0;
        if (generated_template.metadata.generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE) {
            use_room_like_entrance_rooms = 1;
        }
        /*
         * Rooms-and-mazes template generation handles entrance room placement
         * during generation, before random room placement, so no post-pass
         * room painting should happen here.
         */
        if (generated_template.metadata.algorithm_id == (int)DG_ALGORITHM_ROOMS_AND_MAZES) {
            use_room_like_entrance_rooms = 0;
            connectivity_opening_count = 0u;
        }
        if (connectivity_opening_count > 0u && connectivity_openings != NULL) {
            status = dg_enforce_template_opening_connectivity(
                &generated_template,
                connectivity_openings,
                connectivity_opening_count,
                use_room_like_entrance_rooms
            );
            if (status != DG_STATUS_OK) {
                free(connectivity_openings);
                dg_release_template_request_buffers(process_methods, edge_openings);
                dg_map_destroy(&generated_template);
                free(room_openings);
                goto cleanup;
            }
        }
        free(connectivity_openings);

        status = dg_apply_template_to_room(map, &room->bounds, &generated_template);
        dg_release_template_request_buffers(process_methods, edge_openings);
        dg_map_destroy(&generated_template);
        free(room_openings);
        if (status != DG_STATUS_OK) {
            goto cleanup;
        }
    }

cleanup:
    if (dg_room_template_application_depth > 0) {
        dg_room_template_application_depth -= 1;
    }
    if (cache_entries != NULL) {
        for (i = 0; i < cache_count; ++i) {
            if (cache_entries[i].loaded) {
                dg_map_destroy(&cache_entries[i].map);
                cache_entries[i].loaded = 0;
            }
        }
        free(cache_entries);
    }
    return status;
}
