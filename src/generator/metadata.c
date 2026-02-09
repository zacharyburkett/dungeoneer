#include "internal.h"

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

dg_status_t dg_populate_runtime_metadata(
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

    if (map == NULL || map->tiles == NULL) {
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
        map->metadata.rooms[i].role = DG_ROOM_ROLE_NONE;
        if ((map->metadata.rooms[i].flags & DG_ROOM_FLAG_SPECIAL) != 0u) {
            special_room_count += 1;
        }
    }

    status = dg_build_room_graph_metadata(map, &leaf_room_count, &corridor_total_length);
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
    map->metadata.entrance_room_count = 0;
    map->metadata.exit_room_count = 0;
    map->metadata.boss_room_count = 0;
    map->metadata.treasure_room_count = 0;
    map->metadata.shop_room_count = 0;
    map->metadata.leaf_room_count = leaf_room_count;
    map->metadata.corridor_total_length = corridor_total_length;
    map->metadata.entrance_exit_distance = -1;
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
