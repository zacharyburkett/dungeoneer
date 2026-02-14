#include "dungeoneer/map.h"

#include <stdlib.h>

static void dg_map_clear_generation_request_snapshot(
    dg_generation_request_snapshot_t *snapshot
)
{
    if (snapshot == NULL) {
        return;
    }

    free(snapshot->process.methods);
    free(snapshot->room_types.definitions);
    *snapshot = (dg_generation_request_snapshot_t){0};
}

static void dg_map_clear_generation_diagnostics(
    dg_generation_diagnostics_t *diagnostics
)
{
    if (diagnostics == NULL) {
        return;
    }

    free(diagnostics->process_steps);
    free(diagnostics->room_type_quotas);
    *diagnostics = (dg_generation_diagnostics_t){0};
}

static size_t dg_map_index(const dg_map_t *map, int x, int y)
{
    return ((size_t)y * (size_t)map->width) + (size_t)x;
}

static bool dg_map_can_allocate(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    if ((size_t)width > SIZE_MAX / (size_t)height) {
        return false;
    }

    if (((size_t)width * (size_t)height) > (SIZE_MAX / sizeof(dg_tile_t))) {
        return false;
    }

    return true;
}

dg_status_t dg_map_init(dg_map_t *map, int width, int height, dg_tile_t initial_tile)
{
    size_t cell_count;
    size_t i;

    if (map == NULL || !dg_map_can_allocate(width, height)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    cell_count = (size_t)width * (size_t)height;
    map->tiles = (dg_tile_t *)malloc(cell_count * sizeof(dg_tile_t));
    if (map->tiles == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    map->width = width;
    map->height = height;
    map->metadata.rooms = NULL;
    map->metadata.room_count = 0;
    map->metadata.room_capacity = 0;
    map->metadata.corridors = NULL;
    map->metadata.corridor_count = 0;
    map->metadata.corridor_capacity = 0;
    map->metadata.room_entrances = NULL;
    map->metadata.room_entrance_count = 0;
    map->metadata.room_entrance_capacity = 0;
    map->metadata.room_adjacency = NULL;
    map->metadata.room_adjacency_count = 0;
    map->metadata.room_neighbors = NULL;
    map->metadata.room_neighbor_count = 0;
    map->metadata.seed = 0;
    map->metadata.algorithm_id = -1;
    map->metadata.generation_class = DG_MAP_GENERATION_CLASS_UNKNOWN;
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
    map->metadata.diagnostics = (dg_generation_diagnostics_t){0};
    map->metadata.generation_request = (dg_generation_request_snapshot_t){0};

    for (i = 0; i < cell_count; ++i) {
        map->tiles[i] = initial_tile;
    }

    return DG_STATUS_OK;
}

void dg_map_destroy(dg_map_t *map)
{
    if (map == NULL) {
        return;
    }

    free(map->tiles);
    map->tiles = NULL;

    dg_map_clear_metadata(map);

    map->width = 0;
    map->height = 0;
}

dg_status_t dg_map_fill(dg_map_t *map, dg_tile_t tile)
{
    size_t cell_count;
    size_t i;

    if (map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    for (i = 0; i < cell_count; ++i) {
        map->tiles[i] = tile;
    }

    return DG_STATUS_OK;
}

bool dg_map_in_bounds(const dg_map_t *map, int x, int y)
{
    if (map == NULL || map->tiles == NULL) {
        return false;
    }

    return x >= 0 && y >= 0 && x < map->width && y < map->height;
}

dg_status_t dg_map_set_tile(dg_map_t *map, int x, int y, dg_tile_t tile)
{
    if (!dg_map_in_bounds(map, x, y)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    map->tiles[dg_map_index(map, x, y)] = tile;
    return DG_STATUS_OK;
}

dg_tile_t dg_map_get_tile(const dg_map_t *map, int x, int y)
{
    if (!dg_map_in_bounds(map, x, y)) {
        return DG_TILE_VOID;
    }

    return map->tiles[dg_map_index(map, x, y)];
}

void dg_map_clear_metadata(dg_map_t *map)
{
    if (map == NULL) {
        return;
    }

    free(map->metadata.rooms);
    free(map->metadata.corridors);
    free(map->metadata.room_entrances);
    free(map->metadata.room_adjacency);
    free(map->metadata.room_neighbors);
    dg_map_clear_generation_diagnostics(&map->metadata.diagnostics);
    dg_map_clear_generation_request_snapshot(&map->metadata.generation_request);
    map->metadata.rooms = NULL;
    map->metadata.room_count = 0;
    map->metadata.room_capacity = 0;
    map->metadata.corridors = NULL;
    map->metadata.corridor_count = 0;
    map->metadata.corridor_capacity = 0;
    map->metadata.room_entrances = NULL;
    map->metadata.room_entrance_count = 0;
    map->metadata.room_entrance_capacity = 0;
    map->metadata.room_adjacency = NULL;
    map->metadata.room_adjacency_count = 0;
    map->metadata.room_neighbors = NULL;
    map->metadata.room_neighbor_count = 0;
    map->metadata.seed = 0;
    map->metadata.algorithm_id = -1;
    map->metadata.generation_class = DG_MAP_GENERATION_CLASS_UNKNOWN;
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
    map->metadata.diagnostics = (dg_generation_diagnostics_t){0};
    map->metadata.generation_request = (dg_generation_request_snapshot_t){0};
}

dg_status_t dg_map_add_room(dg_map_t *map, const dg_rect_t *bounds, dg_room_flags_t flags)
{
    size_t new_capacity;
    dg_room_metadata_t *expanded_rooms;
    dg_room_metadata_t *room;

    if (map == NULL || map->tiles == NULL || bounds == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (bounds->width <= 0 || bounds->height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (bounds->x < 0 || bounds->y < 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if ((long long)bounds->x + (long long)bounds->width > (long long)map->width) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if ((long long)bounds->y + (long long)bounds->height > (long long)map->height) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.room_count == map->metadata.room_capacity) {
        if (map->metadata.room_capacity == 0) {
            new_capacity = 8;
        } else {
            if (map->metadata.room_capacity > (SIZE_MAX / 2)) {
                return DG_STATUS_ALLOCATION_FAILED;
            }
            new_capacity = map->metadata.room_capacity * 2;
        }

        if (new_capacity > (SIZE_MAX / sizeof(dg_room_metadata_t))) {
            return DG_STATUS_ALLOCATION_FAILED;
        }

        expanded_rooms = (dg_room_metadata_t *)realloc(
            map->metadata.rooms,
            new_capacity * sizeof(dg_room_metadata_t)
        );
        if (expanded_rooms == NULL) {
            return DG_STATUS_ALLOCATION_FAILED;
        }

        map->metadata.rooms = expanded_rooms;
        map->metadata.room_capacity = new_capacity;
    }

    room = &map->metadata.rooms[map->metadata.room_count];
    room->id = (int)map->metadata.room_count;
    room->bounds = *bounds;
    room->flags = flags;
    room->role = DG_ROOM_ROLE_NONE;
    room->type_id = DG_ROOM_TYPE_UNASSIGNED;
    map->metadata.room_count += 1;

    return DG_STATUS_OK;
}

dg_status_t dg_map_add_corridor(
    dg_map_t *map,
    int from_room_id,
    int to_room_id,
    int width,
    int length
)
{
    size_t new_capacity;
    dg_corridor_metadata_t *expanded_corridors;
    dg_corridor_metadata_t *corridor;

    if (map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (width <= 0 || length <= 0 || from_room_id < 0 || to_room_id < 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.corridor_count == map->metadata.corridor_capacity) {
        if (map->metadata.corridor_capacity == 0) {
            new_capacity = 8;
        } else {
            if (map->metadata.corridor_capacity > (SIZE_MAX / 2)) {
                return DG_STATUS_ALLOCATION_FAILED;
            }
            new_capacity = map->metadata.corridor_capacity * 2;
        }

        if (new_capacity > (SIZE_MAX / sizeof(dg_corridor_metadata_t))) {
            return DG_STATUS_ALLOCATION_FAILED;
        }

        expanded_corridors = (dg_corridor_metadata_t *)realloc(
            map->metadata.corridors,
            new_capacity * sizeof(dg_corridor_metadata_t)
        );
        if (expanded_corridors == NULL) {
            return DG_STATUS_ALLOCATION_FAILED;
        }

        map->metadata.corridors = expanded_corridors;
        map->metadata.corridor_capacity = new_capacity;
    }

    corridor = &map->metadata.corridors[map->metadata.corridor_count];
    corridor->from_room_id = from_room_id;
    corridor->to_room_id = to_room_id;
    corridor->width = width;
    corridor->length = length;
    map->metadata.corridor_count += 1;

    return DG_STATUS_OK;
}
