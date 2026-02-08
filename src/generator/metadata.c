#include "internal.h"

#include <limits.h>
#include <stdlib.h>

static bool dg_corridor_endpoints_valid(const dg_map_t *map, const dg_corridor_metadata_t *corridor)
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

static dg_status_t dg_compute_room_distances(
    const dg_map_t *map,
    int start_room_id,
    int *distances,
    int *queue
)
{
    size_t room_count;
    size_t i;
    size_t head;
    size_t tail;

    if (map == NULL || distances == NULL || queue == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    room_count = map->metadata.room_count;
    if (room_count == 0) {
        return DG_STATUS_OK;
    }

    if (start_room_id < 0 || (size_t)start_room_id >= room_count) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (i = 0; i < room_count; ++i) {
        distances[i] = -1;
    }

    head = 0;
    tail = 0;
    queue[tail++] = start_room_id;
    distances[start_room_id] = 0;

    while (head < tail) {
        int room_id;
        const dg_room_adjacency_span_t *span;
        size_t neighbor_index;

        room_id = queue[head++];
        if (room_id < 0 || (size_t)room_id >= room_count) {
            return DG_STATUS_GENERATION_FAILED;
        }

        span = &map->metadata.room_adjacency[room_id];
        if (span->start_index + span->count > map->metadata.room_neighbor_count) {
            return DG_STATUS_GENERATION_FAILED;
        }

        for (neighbor_index = span->start_index;
             neighbor_index < span->start_index + span->count;
             ++neighbor_index) {
            int neighbor_room_id = map->metadata.room_neighbors[neighbor_index].room_id;

            if (neighbor_room_id < 0 || (size_t)neighbor_room_id >= room_count) {
                return DG_STATUS_GENERATION_FAILED;
            }

            if (distances[neighbor_room_id] >= 0) {
                continue;
            }

            distances[neighbor_room_id] = distances[room_id] + 1;
            queue[tail++] = neighbor_room_id;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_compute_distances_to_role(
    const dg_map_t *map,
    dg_room_role_t role,
    int *distances,
    int *queue
)
{
    size_t room_count;
    size_t i;
    size_t head;
    size_t tail;

    if (map == NULL || distances == NULL || queue == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    room_count = map->metadata.room_count;
    if (room_count == 0) {
        return DG_STATUS_OK;
    }

    for (i = 0; i < room_count; ++i) {
        distances[i] = -1;
    }

    head = 0;
    tail = 0;
    for (i = 0; i < room_count; ++i) {
        if (map->metadata.rooms[i].role == role) {
            distances[i] = 0;
            queue[tail++] = (int)i;
        }
    }

    while (head < tail) {
        int room_id;
        const dg_room_adjacency_span_t *span;
        size_t neighbor_index;

        room_id = queue[head++];
        if (room_id < 0 || (size_t)room_id >= room_count) {
            return DG_STATUS_GENERATION_FAILED;
        }

        span = &map->metadata.room_adjacency[room_id];
        if (span->start_index + span->count > map->metadata.room_neighbor_count) {
            return DG_STATUS_GENERATION_FAILED;
        }

        for (neighbor_index = span->start_index;
             neighbor_index < span->start_index + span->count;
             ++neighbor_index) {
            int neighbor_room_id = map->metadata.room_neighbors[neighbor_index].room_id;

            if (neighbor_room_id < 0 || (size_t)neighbor_room_id >= room_count) {
                return DG_STATUS_GENERATION_FAILED;
            }

            if (distances[neighbor_room_id] >= 0) {
                continue;
            }

            distances[neighbor_room_id] = distances[room_id] + 1;
            queue[tail++] = neighbor_room_id;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_compute_min_role_distance(
    const dg_map_t *map,
    dg_room_role_t from_role,
    dg_room_role_t to_role,
    int *out_distance,
    int *distances,
    int *queue
)
{
    size_t room_count;
    size_t i;
    bool has_from;
    bool has_to;
    int best_distance;
    dg_status_t status;

    if (map == NULL || out_distance == NULL || distances == NULL || queue == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    room_count = map->metadata.room_count;
    has_from = false;
    has_to = false;
    for (i = 0; i < room_count; ++i) {
        if (map->metadata.rooms[i].role == from_role) {
            has_from = true;
        }
        if (map->metadata.rooms[i].role == to_role) {
            has_to = true;
        }
    }

    if (!has_from || !has_to) {
        *out_distance = -1;
        return DG_STATUS_OK;
    }

    best_distance = -1;
    for (i = 0; i < room_count; ++i) {
        size_t j;

        if (map->metadata.rooms[i].role != from_role) {
            continue;
        }

        status = dg_compute_room_distances(map, (int)i, distances, queue);
        if (status != DG_STATUS_OK) {
            return status;
        }

        for (j = 0; j < room_count; ++j) {
            int distance;

            if (map->metadata.rooms[j].role != to_role) {
                continue;
            }

            distance = distances[j];
            if (distance < 0) {
                continue;
            }

            if (best_distance < 0 || distance < best_distance) {
                best_distance = distance;
            }
        }
    }

    *out_distance = best_distance;
    return DG_STATUS_OK;
}

static dg_status_t dg_build_room_graph_metadata(
    dg_map_t *map,
    size_t *out_leaf_room_count,
    size_t *out_corridor_total_length
)
{
    size_t i;
    size_t room_count;
    size_t valid_corridor_count;
    size_t neighbor_count;
    size_t running_index;
    size_t leaf_room_count;
    size_t corridor_total_length;
    int *degrees;
    size_t *write_cursor;
    dg_room_adjacency_span_t *room_adjacency;
    dg_room_neighbor_t *room_neighbors;

    if (map == NULL || out_leaf_room_count == NULL || out_corridor_total_length == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    room_count = map->metadata.room_count;
    *out_leaf_room_count = 0;
    *out_corridor_total_length = 0;

    free(map->metadata.room_adjacency);
    free(map->metadata.room_neighbors);
    map->metadata.room_adjacency = NULL;
    map->metadata.room_adjacency_count = 0;
    map->metadata.room_neighbors = NULL;
    map->metadata.room_neighbor_count = 0;

    if (room_count == 0) {
        return DG_STATUS_OK;
    }

    degrees = (int *)calloc(room_count, sizeof(int));
    if (degrees == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    corridor_total_length = 0;
    valid_corridor_count = 0;
    for (i = 0; i < map->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];
        if (corridor->length > 0) {
            corridor_total_length += (size_t)corridor->length;
        }

        if (!dg_corridor_endpoints_valid(map, corridor)) {
            continue;
        }

        degrees[corridor->from_room_id] += 1;
        degrees[corridor->to_room_id] += 1;
        valid_corridor_count += 1;
    }

    leaf_room_count = 0;
    for (i = 0; i < room_count; ++i) {
        if (degrees[i] == 1) {
            leaf_room_count += 1;
        }
    }

    room_adjacency = (dg_room_adjacency_span_t *)calloc(room_count, sizeof(dg_room_adjacency_span_t));
    if (room_adjacency == NULL) {
        free(degrees);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    neighbor_count = valid_corridor_count * 2;
    room_neighbors = NULL;
    if (neighbor_count > 0) {
        room_neighbors = (dg_room_neighbor_t *)malloc(neighbor_count * sizeof(dg_room_neighbor_t));
        if (room_neighbors == NULL) {
            free(room_adjacency);
            free(degrees);
            return DG_STATUS_ALLOCATION_FAILED;
        }
    }

    running_index = 0;
    for (i = 0; i < room_count; ++i) {
        room_adjacency[i].start_index = running_index;
        room_adjacency[i].count = (size_t)degrees[i];
        running_index += (size_t)degrees[i];
    }

    if (running_index != neighbor_count) {
        free(room_neighbors);
        free(room_adjacency);
        free(degrees);
        return DG_STATUS_GENERATION_FAILED;
    }

    write_cursor = (size_t *)malloc(room_count * sizeof(size_t));
    if (write_cursor == NULL) {
        free(room_neighbors);
        free(room_adjacency);
        free(degrees);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (i = 0; i < room_count; ++i) {
        write_cursor[i] = room_adjacency[i].start_index;
    }

    for (i = 0; i < map->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];
        size_t from_pos;
        size_t to_pos;

        if (!dg_corridor_endpoints_valid(map, corridor)) {
            continue;
        }

        from_pos = write_cursor[corridor->from_room_id]++;
        to_pos = write_cursor[corridor->to_room_id]++;

        room_neighbors[from_pos].room_id = corridor->to_room_id;
        room_neighbors[from_pos].corridor_index = (int)i;
        room_neighbors[to_pos].room_id = corridor->from_room_id;
        room_neighbors[to_pos].corridor_index = (int)i;
    }

    free(write_cursor);
    free(degrees);

    map->metadata.room_adjacency = room_adjacency;
    map->metadata.room_adjacency_count = room_count;
    map->metadata.room_neighbors = room_neighbors;
    map->metadata.room_neighbor_count = neighbor_count;

    *out_leaf_room_count = leaf_room_count;
    *out_corridor_total_length = corridor_total_length;
    return DG_STATUS_OK;
}

static int dg_pick_room_by_score(
    const dg_map_t *map,
    const unsigned char *taken,
    const int *scores,
    bool prefer_leaf,
    bool require_leaf
)
{
    size_t room_count;
    int best_room;
    int best_score;
    int pass_count;
    int pass;

    if (map == NULL || taken == NULL || scores == NULL) {
        return -1;
    }

    room_count = map->metadata.room_count;
    pass_count = (prefer_leaf && !require_leaf) ? 2 : 1;
    for (pass = 0; pass < pass_count; ++pass) {
        bool leaf_only;
        size_t i;

        leaf_only = (prefer_leaf && pass == 0);
        best_room = -1;
        best_score = INT_MIN;

        for (i = 0; i < room_count; ++i) {
            bool is_leaf;
            int score;

            if (taken[i] != 0) {
                continue;
            }

            is_leaf = map->metadata.room_adjacency[i].count == 1;
            if (require_leaf && !is_leaf) {
                continue;
            }
            if (leaf_only && !is_leaf) {
                continue;
            }

            score = scores[i];
            if (
                best_room < 0 ||
                score > best_score ||
                (score == best_score && (int)i < best_room)
            ) {
                best_room = (int)i;
                best_score = score;
            }
        }

        if (best_room >= 0) {
            return best_room;
        }
    }

    return -1;
}

static int dg_score_room_for_role(
    int distance_from_entrance,
    int room_degree,
    bool is_leaf,
    const dg_role_placement_weights_t *weights
)
{
    long long score;
    long long leaf_term;

    if (weights == NULL) {
        return 0;
    }

    if (distance_from_entrance < 0) {
        distance_from_entrance = 0;
    }
    if (room_degree < 0) {
        room_degree = 0;
    }

    leaf_term = is_leaf ? weights->leaf_bonus : 0;
    score = ((long long)weights->distance_weight * (long long)distance_from_entrance) +
            ((long long)weights->degree_weight * (long long)room_degree) +
            leaf_term;

    if (score > INT_MAX) {
        return INT_MAX;
    }
    if (score < INT_MIN) {
        return INT_MIN;
    }
    return (int)score;
}

static bool dg_assign_role_to_room(
    dg_map_t *map,
    unsigned char *taken,
    int room_id,
    dg_room_role_t role
)
{
    if (map == NULL || taken == NULL) {
        return false;
    }

    if (room_id < 0 || (size_t)room_id >= map->metadata.room_count) {
        return false;
    }

    if (role == DG_ROOM_ROLE_NONE) {
        return false;
    }

    if (taken[room_id] != 0) {
        return false;
    }

    map->metadata.rooms[room_id].role = role;
    taken[room_id] = 1;
    return true;
}

static dg_status_t dg_select_farthest_pair(
    const dg_map_t *map,
    const unsigned char *taken,
    int *distances,
    int *queue,
    int min_leaf_rooms_to_preserve,
    int *out_room_a,
    int *out_room_b,
    int *out_distance
)
{
    size_t room_count;
    size_t i;
    size_t total_leaf_rooms;
    int best_a;
    int best_b;
    int best_distance;

    if (
        map == NULL || taken == NULL || distances == NULL || queue == NULL ||
        out_room_a == NULL || out_room_b == NULL || out_distance == NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    room_count = map->metadata.room_count;
    total_leaf_rooms = 0;
    for (i = 0; i < room_count; ++i) {
        if (map->metadata.room_adjacency[i].count == 1) {
            total_leaf_rooms += 1;
        }
    }

    best_a = -1;
    best_b = -1;
    best_distance = -1;

    for (i = 0; i < room_count; ++i) {
        size_t j;
        dg_status_t status;

        if (taken[i] != 0) {
            continue;
        }

        status = dg_compute_room_distances(map, (int)i, distances, queue);
        if (status != DG_STATUS_OK) {
            return status;
        }

        for (j = i + 1; j < room_count; ++j) {
            int distance;
            int used_leaf_rooms;
            int remaining_leaf_rooms;

            if (taken[j] != 0) {
                continue;
            }

            distance = distances[j];
            if (distance < 0) {
                continue;
            }

            if (min_leaf_rooms_to_preserve > 0) {
                used_leaf_rooms = 0;
                if (map->metadata.room_adjacency[i].count == 1) {
                    used_leaf_rooms += 1;
                }
                if (map->metadata.room_adjacency[j].count == 1) {
                    used_leaf_rooms += 1;
                }

                remaining_leaf_rooms = (int)total_leaf_rooms - used_leaf_rooms;
                if (remaining_leaf_rooms < min_leaf_rooms_to_preserve) {
                    continue;
                }
            }

            if (distance > best_distance) {
                best_distance = distance;
                best_a = (int)i;
                best_b = (int)j;
            }
        }
    }

    if (best_distance < 0) {
        return DG_STATUS_GENERATION_FAILED;
    }

    *out_room_a = best_a;
    *out_room_b = best_b;
    *out_distance = best_distance;
    return DG_STATUS_OK;
}

static void dg_count_assigned_roles(dg_map_t *map)
{
    size_t i;

    map->metadata.entrance_room_count = 0;
    map->metadata.exit_room_count = 0;
    map->metadata.boss_room_count = 0;
    map->metadata.treasure_room_count = 0;
    map->metadata.shop_room_count = 0;

    for (i = 0; i < map->metadata.room_count; ++i) {
        switch (map->metadata.rooms[i].role) {
        case DG_ROOM_ROLE_ENTRANCE:
            map->metadata.entrance_room_count += 1;
            break;
        case DG_ROOM_ROLE_EXIT:
            map->metadata.exit_room_count += 1;
            break;
        case DG_ROOM_ROLE_BOSS:
            map->metadata.boss_room_count += 1;
            break;
        case DG_ROOM_ROLE_TREASURE:
            map->metadata.treasure_room_count += 1;
            break;
        case DG_ROOM_ROLE_SHOP:
            map->metadata.shop_room_count += 1;
            break;
        case DG_ROOM_ROLE_NONE:
        default:
            break;
        }
    }
}

static bool dg_role_assignment_requested(const dg_generation_constraints_t *constraints)
{
    if (constraints == NULL) {
        return false;
    }

    return constraints->required_entrance_rooms > 0 ||
           constraints->required_exit_rooms > 0 ||
           constraints->required_boss_rooms > 0 ||
           constraints->required_treasure_rooms > 0 ||
           constraints->required_shop_rooms > 0 ||
           constraints->min_entrance_exit_distance > 0;
}

static dg_status_t dg_assign_room_roles(
    const dg_generate_request_t *request,
    dg_map_t *map
)
{
    const dg_generation_constraints_t *constraints;
    size_t room_count;
    size_t i;
    int need_entrance;
    int need_exit;
    int need_boss;
    int need_treasure;
    int need_shop;
    int total_required;
    int primary_entrance;
    unsigned char *taken;
    int *scores;
    int *distances;
    int *queue;
    dg_status_t status;

    if (request == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    constraints = &request->constraints;
    room_count = map->metadata.room_count;
    for (i = 0; i < room_count; ++i) {
        map->metadata.rooms[i].role = DG_ROOM_ROLE_NONE;
    }

    map->metadata.entrance_room_count = 0;
    map->metadata.exit_room_count = 0;
    map->metadata.boss_room_count = 0;
    map->metadata.treasure_room_count = 0;
    map->metadata.shop_room_count = 0;
    map->metadata.entrance_exit_distance = -1;

    if (!dg_role_assignment_requested(constraints)) {
        return DG_STATUS_OK;
    }

    need_entrance = constraints->required_entrance_rooms;
    need_exit = constraints->required_exit_rooms;
    need_boss = constraints->required_boss_rooms;
    need_treasure = constraints->required_treasure_rooms;
    need_shop = constraints->required_shop_rooms;

    if (room_count == 0) {
        return DG_STATUS_GENERATION_FAILED;
    }

    total_required = need_entrance + need_exit + need_boss + need_treasure + need_shop;
    if (total_required > (int)room_count) {
        return DG_STATUS_GENERATION_FAILED;
    }

    taken = (unsigned char *)calloc(room_count, sizeof(unsigned char));
    scores = (int *)calloc(room_count, sizeof(int));
    distances = (int *)calloc(room_count, sizeof(int));
    queue = (int *)calloc(room_count, sizeof(int));
    if (taken == NULL || scores == NULL || distances == NULL || queue == NULL) {
        free(taken);
        free(scores);
        free(distances);
        free(queue);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    primary_entrance = -1;
    if (need_entrance > 0 && need_exit > 0) {
        int entrance_room;
        int exit_room;
        int pair_distance;

        status = dg_select_farthest_pair(
            map,
            taken,
            distances,
            queue,
            constraints->require_boss_on_leaf ? need_boss : 0,
            &entrance_room,
            &exit_room,
            &pair_distance
        );
        if (status != DG_STATUS_OK) {
            free(taken);
            free(scores);
            free(distances);
            free(queue);
            return status;
        }

        if (
            constraints->min_entrance_exit_distance > 0 &&
            pair_distance < constraints->min_entrance_exit_distance
        ) {
            free(taken);
            free(scores);
            free(distances);
            free(queue);
            return DG_STATUS_GENERATION_FAILED;
        }

        if (!dg_assign_role_to_room(map, taken, entrance_room, DG_ROOM_ROLE_ENTRANCE)) {
            free(taken);
            free(scores);
            free(distances);
            free(queue);
            return DG_STATUS_GENERATION_FAILED;
        }
        if (!dg_assign_role_to_room(map, taken, exit_room, DG_ROOM_ROLE_EXIT)) {
            free(taken);
            free(scores);
            free(distances);
            free(queue);
            return DG_STATUS_GENERATION_FAILED;
        }

        primary_entrance = entrance_room;
        need_entrance -= 1;
        need_exit -= 1;
    }

    for (i = 0; i < room_count; ++i) {
        int degree;
        bool is_leaf;

        degree = (int)map->metadata.room_adjacency[i].count;
        is_leaf = map->metadata.room_adjacency[i].count == 1;
        scores[i] = dg_score_room_for_role(
            0,
            degree,
            is_leaf,
            &constraints->entrance_weights
        );
    }
    while (need_entrance > 0) {
        int room_id = dg_pick_room_by_score(map, taken, scores, false, false);
        if (room_id < 0 || !dg_assign_role_to_room(map, taken, room_id, DG_ROOM_ROLE_ENTRANCE)) {
            free(taken);
            free(scores);
            free(distances);
            free(queue);
            return DG_STATUS_GENERATION_FAILED;
        }
        if (primary_entrance < 0) {
            primary_entrance = room_id;
        }
        need_entrance -= 1;
    }

    if (primary_entrance >= 0) {
        status = dg_compute_room_distances(map, primary_entrance, distances, queue);
        if (status != DG_STATUS_OK) {
            free(taken);
            free(scores);
            free(distances);
            free(queue);
            return status;
        }
    } else {
        for (i = 0; i < room_count; ++i) {
            distances[i] = -1;
        }
    }

    for (i = 0; i < room_count; ++i) {
        int degree;
        bool is_leaf;
        degree = (int)map->metadata.room_adjacency[i].count;
        is_leaf = map->metadata.room_adjacency[i].count == 1;
        scores[i] = dg_score_room_for_role(
            distances[i],
            degree,
            is_leaf,
            &constraints->exit_weights
        );
    }

    while (need_exit > 0) {
        int room_id = dg_pick_room_by_score(map, taken, scores, false, false);
        if (room_id < 0 || !dg_assign_role_to_room(map, taken, room_id, DG_ROOM_ROLE_EXIT)) {
            free(taken);
            free(scores);
            free(distances);
            free(queue);
            return DG_STATUS_GENERATION_FAILED;
        }
        need_exit -= 1;
    }

    status = dg_compute_distances_to_role(map, DG_ROOM_ROLE_ENTRANCE, distances, queue);
    if (status != DG_STATUS_OK) {
        free(taken);
        free(scores);
        free(distances);
        free(queue);
        return status;
    }

    for (i = 0; i < room_count; ++i) {
        int degree;
        bool is_leaf;

        degree = (int)map->metadata.room_adjacency[i].count;
        is_leaf = map->metadata.room_adjacency[i].count == 1;
        scores[i] = dg_score_room_for_role(
            distances[i],
            degree,
            is_leaf,
            &constraints->boss_weights
        );
    }

    while (need_boss > 0) {
        int room_id = dg_pick_room_by_score(map, taken, scores, false, constraints->require_boss_on_leaf);
        if (room_id < 0 || !dg_assign_role_to_room(map, taken, room_id, DG_ROOM_ROLE_BOSS)) {
            free(taken);
            free(scores);
            free(distances);
            free(queue);
            return DG_STATUS_GENERATION_FAILED;
        }
        need_boss -= 1;
    }

    for (i = 0; i < room_count; ++i) {
        int degree;
        bool is_leaf;

        degree = (int)map->metadata.room_adjacency[i].count;
        is_leaf = map->metadata.room_adjacency[i].count == 1;
        scores[i] = dg_score_room_for_role(
            distances[i],
            degree,
            is_leaf,
            &constraints->treasure_weights
        );
    }
    while (need_treasure > 0) {
        int room_id = dg_pick_room_by_score(map, taken, scores, false, false);
        if (room_id < 0 || !dg_assign_role_to_room(map, taken, room_id, DG_ROOM_ROLE_TREASURE)) {
            free(taken);
            free(scores);
            free(distances);
            free(queue);
            return DG_STATUS_GENERATION_FAILED;
        }
        need_treasure -= 1;
    }

    for (i = 0; i < room_count; ++i) {
        int degree;
        bool is_leaf;

        degree = (int)map->metadata.room_adjacency[i].count;
        is_leaf = map->metadata.room_adjacency[i].count == 1;
        scores[i] = dg_score_room_for_role(
            distances[i],
            degree,
            is_leaf,
            &constraints->shop_weights
        );
    }
    while (need_shop > 0) {
        int room_id = dg_pick_room_by_score(map, taken, scores, false, false);
        if (room_id < 0 || !dg_assign_role_to_room(map, taken, room_id, DG_ROOM_ROLE_SHOP)) {
            free(taken);
            free(scores);
            free(distances);
            free(queue);
            return DG_STATUS_GENERATION_FAILED;
        }
        need_shop -= 1;
    }

    dg_count_assigned_roles(map);

    status = dg_compute_min_role_distance(
        map,
        DG_ROOM_ROLE_ENTRANCE,
        DG_ROOM_ROLE_EXIT,
        &map->metadata.entrance_exit_distance,
        distances,
        queue
    );
    if (status != DG_STATUS_OK) {
        free(taken);
        free(scores);
        free(distances);
        free(queue);
        return status;
    }

    if (
        constraints->min_entrance_exit_distance > 0 &&
        map->metadata.entrance_exit_distance < constraints->min_entrance_exit_distance
    ) {
        free(taken);
        free(scores);
        free(distances);
        free(queue);
        return DG_STATUS_GENERATION_FAILED;
    }

    if (constraints->require_boss_on_leaf) {
        for (i = 0; i < room_count; ++i) {
            if (map->metadata.rooms[i].role != DG_ROOM_ROLE_BOSS) {
                continue;
            }
            if (map->metadata.room_adjacency[i].count != 1) {
                free(taken);
                free(scores);
                free(distances);
                free(queue);
                return DG_STATUS_GENERATION_FAILED;
            }
        }
    }

    free(taken);
    free(scores);
    free(distances);
    free(queue);
    return DG_STATUS_OK;
}

dg_status_t dg_populate_runtime_metadata(
    const dg_generate_request_t *request,
    dg_map_t *map,
    uint64_t seed,
    int algorithm_id,
    size_t generation_attempts
)
{
    size_t i;
    size_t cell_count;
    size_t walkable_tile_count;
    size_t wall_tile_count;
    size_t special_room_count;
    size_t leaf_room_count;
    size_t corridor_total_length;
    dg_connectivity_stats_t connectivity;
    dg_status_t status;

    if (request == NULL || map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    walkable_tile_count = 0;
    wall_tile_count = 0;
    for (i = 0; i < cell_count; ++i) {
        if (dg_is_walkable_tile(map->tiles[i])) {
            walkable_tile_count += 1;
        }
        if (map->tiles[i] == DG_TILE_WALL) {
            wall_tile_count += 1;
        }
    }

    special_room_count = 0;
    for (i = 0; i < map->metadata.room_count; ++i) {
        if ((map->metadata.rooms[i].flags & DG_ROOM_FLAG_SPECIAL) != 0u) {
            special_room_count += 1;
        }
    }

    status = dg_build_room_graph_metadata(map, &leaf_room_count, &corridor_total_length);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_assign_room_roles(request, map);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_analyze_connectivity(map, &connectivity);
    if (status != DG_STATUS_OK) {
        return status;
    }

    map->metadata.seed = seed;
    map->metadata.algorithm_id = algorithm_id;
    map->metadata.walkable_tile_count = walkable_tile_count;
    map->metadata.wall_tile_count = wall_tile_count;
    map->metadata.special_room_count = special_room_count;
    map->metadata.leaf_room_count = leaf_room_count;
    map->metadata.corridor_total_length = corridor_total_length;
    map->metadata.connected_component_count = connectivity.component_count;
    map->metadata.largest_component_size = connectivity.largest_component_size;
    map->metadata.connected_floor = connectivity.connected_floor;
    map->metadata.generation_attempts = generation_attempts;

    return DG_STATUS_OK;
}

void dg_init_empty_map(dg_map_t *map)
{
    if (map == NULL) {
        return;
    }

    map->width = 0;
    map->height = 0;
    map->tiles = NULL;
    map->metadata.rooms = NULL;
    map->metadata.room_count = 0;
    map->metadata.room_capacity = 0;
    map->metadata.corridors = NULL;
    map->metadata.corridor_count = 0;
    map->metadata.corridor_capacity = 0;
    map->metadata.room_adjacency = NULL;
    map->metadata.room_adjacency_count = 0;
    map->metadata.room_neighbors = NULL;
    map->metadata.room_neighbor_count = 0;
    map->metadata.seed = 0;
    map->metadata.algorithm_id = -1;
    map->metadata.walkable_tile_count = 0;
    map->metadata.wall_tile_count = 0;
    map->metadata.special_room_count = 0;
    map->metadata.entrance_room_count = 0;
    map->metadata.exit_room_count = 0;
    map->metadata.boss_room_count = 0;
    map->metadata.treasure_room_count = 0;
    map->metadata.shop_room_count = 0;
    map->metadata.leaf_room_count = 0;
    map->metadata.corridor_total_length = 0;
    map->metadata.entrance_exit_distance = -1;
    map->metadata.connected_component_count = 0;
    map->metadata.largest_component_size = 0;
    map->metadata.connected_floor = false;
    map->metadata.generation_attempts = 0;
}
