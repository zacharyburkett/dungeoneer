#include "internal.h"

#include <stdlib.h>

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
    int *room_degrees;
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
        if ((map->metadata.rooms[i].flags & DG_ROOM_FLAG_SPECIAL) != 0u) {
            special_room_count += 1;
        }
    }

    leaf_room_count = 0;
    corridor_total_length = 0;
    room_degrees = NULL;
    if (map->metadata.room_count > 0) {
        room_degrees = (int *)calloc(map->metadata.room_count, sizeof(int));
        if (room_degrees == NULL) {
            return DG_STATUS_ALLOCATION_FAILED;
        }
    }

    for (i = 0; i < map->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];
        if (corridor->length > 0) {
            corridor_total_length += (size_t)corridor->length;
        }

        if (room_degrees != NULL) {
            if (corridor->from_room_id >= 0 && (size_t)corridor->from_room_id < map->metadata.room_count) {
                room_degrees[corridor->from_room_id] += 1;
            }
            if (corridor->to_room_id >= 0 && (size_t)corridor->to_room_id < map->metadata.room_count) {
                room_degrees[corridor->to_room_id] += 1;
            }
        }
    }

    if (room_degrees != NULL) {
        for (i = 0; i < map->metadata.room_count; ++i) {
            if (room_degrees[i] == 1) {
                leaf_room_count += 1;
            }
        }
        free(room_degrees);
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
    map->metadata.seed = 0;
    map->metadata.algorithm_id = -1;
    map->metadata.walkable_tile_count = 0;
    map->metadata.wall_tile_count = 0;
    map->metadata.special_room_count = 0;
    map->metadata.leaf_room_count = 0;
    map->metadata.corridor_total_length = 0;
    map->metadata.connected_component_count = 0;
    map->metadata.largest_component_size = 0;
    map->metadata.connected_floor = false;
    map->metadata.generation_attempts = 0;
}
