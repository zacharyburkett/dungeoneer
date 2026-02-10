#include "internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

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
