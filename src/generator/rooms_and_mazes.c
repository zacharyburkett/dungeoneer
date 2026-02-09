#include "internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct dg_maze_cell {
    int x;
    int y;
} dg_maze_cell_t;

typedef struct dg_room_connector {
    int wall_x;
    int wall_y;
    int target_region;
    int target_room_id;
} dg_room_connector_t;

static const int DG_CARDINAL_DIRECTIONS[4][2] = {
    {1, 0},
    {-1, 0},
    {0, 1},
    {0, -1}
};

static bool dg_interior_in_bounds(const dg_map_t *map, int x, int y)
{
    return x > 0 && y > 0 && x < map->width - 1 && y < map->height - 1;
}

static int dg_region_find_root(int *parents, int region_id)
{
    int root;
    int current;

    root = region_id;
    while (parents[root] != root) {
        root = parents[root];
    }

    current = region_id;
    while (parents[current] != current) {
        int next = parents[current];
        parents[current] = root;
        current = next;
    }

    return root;
}

static void dg_region_union(int *parents, int left_region, int right_region)
{
    int left_root;
    int right_root;

    left_root = dg_region_find_root(parents, left_region);
    right_root = dg_region_find_root(parents, right_region);
    if (left_root == right_root) {
        return;
    }

    parents[right_root] = left_root;
}

static void dg_shuffle_ints(int *values, size_t count, dg_rng_t *rng)
{
    size_t i;

    if (values == NULL || rng == NULL || count <= 1) {
        return;
    }

    for (i = count - 1; i > 0; --i) {
        size_t j = (size_t)dg_rng_range(rng, 0, (int)i);
        int tmp = values[i];
        values[i] = values[j];
        values[j] = tmp;
    }
}

static void dg_carve_room_with_region(
    dg_map_t *map,
    const dg_rect_t *room,
    int *regions,
    int region_id
)
{
    int y;
    int x;

    for (y = room->y; y < room->y + room->height; ++y) {
        for (x = room->x; x < room->x + room->width; ++x) {
            size_t index = dg_tile_index(map, x, y);
            (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
            regions[index] = region_id;
        }
    }
}

static bool dg_room_overlaps_existing(const dg_map_t *map, const dg_rect_t *candidate)
{
    size_t i;

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_rect_t *placed = &map->metadata.rooms[i].bounds;
        if (dg_rects_overlap_with_padding(placed, candidate, 1)) {
            return true;
        }
    }

    return false;
}

static dg_status_t dg_place_random_rooms(
    const dg_rooms_and_mazes_config_t *config,
    dg_map_t *map,
    dg_rng_t *rng,
    int *regions,
    int *out_next_region_id
)
{
    int max_room_width;
    int max_room_height;
    int target_rooms;
    size_t placement_attempt;
    size_t placement_attempt_limit;

    if (
        config == NULL ||
        map == NULL ||
        map->tiles == NULL ||
        rng == NULL ||
        regions == NULL ||
        out_next_region_id == NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    max_room_width = dg_min_int(config->room_max_size, map->width - 2);
    max_room_height = dg_min_int(config->room_max_size, map->height - 2);
    if (max_room_width < config->room_min_size || max_room_height < config->room_min_size) {
        return DG_STATUS_GENERATION_FAILED;
    }

    target_rooms = dg_rng_range(rng, config->min_rooms, config->max_rooms);
    placement_attempt_limit = (size_t)target_rooms * 128u + 256u;
    for (placement_attempt = 0; placement_attempt < placement_attempt_limit; ++placement_attempt) {
        dg_rect_t room;
        int max_x;
        int max_y;
        dg_status_t status;
        int room_id;

        if ((int)map->metadata.room_count >= target_rooms) {
            break;
        }

        room.width = dg_rng_range(rng, config->room_min_size, max_room_width);
        room.height = dg_rng_range(rng, config->room_min_size, max_room_height);

        max_x = map->width - room.width - 1;
        max_y = map->height - room.height - 1;
        if (max_x < 1 || max_y < 1) {
            continue;
        }

        room.x = dg_rng_range(rng, 1, max_x);
        room.y = dg_rng_range(rng, 1, max_y);

        if (dg_room_overlaps_existing(map, &room)) {
            continue;
        }

        status = dg_map_add_room(map, &room, DG_ROOM_FLAG_NONE);
        if (status != DG_STATUS_OK) {
            return status;
        }

        room_id = (int)map->metadata.room_count - 1;
        dg_carve_room_with_region(map, &room, regions, room_id + 1);
    }

    if ((int)map->metadata.room_count < config->min_rooms) {
        return DG_STATUS_GENERATION_FAILED;
    }

    *out_next_region_id = (int)map->metadata.room_count + 1;
    return DG_STATUS_OK;
}

static bool dg_can_carve_maze_step(
    const dg_map_t *map,
    const int *regions,
    int x,
    int y,
    int dir_x,
    int dir_y
)
{
    int mid_x;
    int mid_y;
    int dst_x;
    int dst_y;
    size_t mid_index;
    size_t dst_index;

    mid_x = x + dir_x;
    mid_y = y + dir_y;
    dst_x = x + dir_x * 2;
    dst_y = y + dir_y * 2;

    if (!dg_interior_in_bounds(map, mid_x, mid_y) || !dg_interior_in_bounds(map, dst_x, dst_y)) {
        return false;
    }

    if (dg_map_get_tile(map, mid_x, mid_y) != DG_TILE_WALL) {
        return false;
    }
    if (dg_map_get_tile(map, dst_x, dst_y) != DG_TILE_WALL) {
        return false;
    }

    mid_index = dg_tile_index(map, mid_x, mid_y);
    dst_index = dg_tile_index(map, dst_x, dst_y);
    return regions[mid_index] == -1 && regions[dst_index] == -1;
}

static dg_status_t dg_carve_maze_region(
    dg_map_t *map,
    int *regions,
    int start_x,
    int start_y,
    int region_id,
    dg_rng_t *rng
)
{
    size_t cell_capacity;
    dg_maze_cell_t *stack;
    size_t stack_count;
    size_t start_index;

    if (map == NULL || map->tiles == NULL || regions == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    cell_capacity = (size_t)map->width * (size_t)map->height;
    stack = (dg_maze_cell_t *)malloc(cell_capacity * sizeof(dg_maze_cell_t));
    if (stack == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    start_index = dg_tile_index(map, start_x, start_y);
    (void)dg_map_set_tile(map, start_x, start_y, DG_TILE_FLOOR);
    regions[start_index] = region_id;

    stack_count = 0;
    stack[stack_count++] = (dg_maze_cell_t){start_x, start_y};

    while (stack_count > 0) {
        dg_maze_cell_t cell = stack[stack_count - 1];
        int valid_dirs[4];
        int valid_count;
        int dir_choice;
        int dir_x;
        int dir_y;
        int mid_x;
        int mid_y;
        int dst_x;
        int dst_y;
        size_t mid_index;
        size_t dst_index;
        int d;

        valid_count = 0;
        for (d = 0; d < 4; ++d) {
            if (dg_can_carve_maze_step(
                    map,
                    regions,
                    cell.x,
                    cell.y,
                    DG_CARDINAL_DIRECTIONS[d][0],
                    DG_CARDINAL_DIRECTIONS[d][1]
                )) {
                valid_dirs[valid_count++] = d;
            }
        }

        if (valid_count == 0) {
            stack_count -= 1;
            continue;
        }

        dir_choice = valid_dirs[dg_rng_range(rng, 0, valid_count - 1)];
        dir_x = DG_CARDINAL_DIRECTIONS[dir_choice][0];
        dir_y = DG_CARDINAL_DIRECTIONS[dir_choice][1];

        mid_x = cell.x + dir_x;
        mid_y = cell.y + dir_y;
        dst_x = cell.x + dir_x * 2;
        dst_y = cell.y + dir_y * 2;
        mid_index = dg_tile_index(map, mid_x, mid_y);
        dst_index = dg_tile_index(map, dst_x, dst_y);

        (void)dg_map_set_tile(map, mid_x, mid_y, DG_TILE_FLOOR);
        (void)dg_map_set_tile(map, dst_x, dst_y, DG_TILE_FLOOR);
        regions[mid_index] = region_id;
        regions[dst_index] = region_id;

        stack[stack_count++] = (dg_maze_cell_t){dst_x, dst_y};
    }

    free(stack);
    return DG_STATUS_OK;
}

static dg_status_t dg_generate_maze_regions(
    dg_map_t *map,
    int *regions,
    int next_region_id,
    dg_rng_t *rng,
    int *out_next_region_id
)
{
    int y;
    int x;

    if (
        map == NULL ||
        map->tiles == NULL ||
        regions == NULL ||
        rng == NULL ||
        out_next_region_id == NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (y = 1; y < map->height - 1; y += 2) {
        for (x = 1; x < map->width - 1; x += 2) {
            size_t index = dg_tile_index(map, x, y);
            dg_status_t status;

            if (map->tiles[index] != DG_TILE_WALL || regions[index] != -1) {
                continue;
            }

            status = dg_carve_maze_region(map, regions, x, y, next_region_id, rng);
            if (status != DG_STATUS_OK) {
                return status;
            }

            next_region_id += 1;
        }
    }

    *out_next_region_id = next_region_id;
    return DG_STATUS_OK;
}

static void dg_try_add_room_connector_candidate(
    const dg_map_t *map,
    const int *regions,
    int *parents,
    int room_id,
    int room_region,
    int room_count,
    const unsigned char *room_links,
    int boundary_x,
    int boundary_y,
    int dir_x,
    int dir_y,
    dg_room_connector_t *candidates,
    size_t candidate_capacity,
    size_t *in_out_candidate_count
)
{
    int wall_x;
    int wall_y;
    int target_x;
    int target_y;
    size_t target_index;
    int target_region;
    int target_room_id;

    if (
        map == NULL ||
        regions == NULL ||
        parents == NULL ||
        in_out_candidate_count == NULL ||
        candidates == NULL
    ) {
        return;
    }

    if (*in_out_candidate_count >= candidate_capacity) {
        return;
    }

    wall_x = boundary_x + dir_x;
    wall_y = boundary_y + dir_y;
    target_x = boundary_x + dir_x * 2;
    target_y = boundary_y + dir_y * 2;
    if (!dg_interior_in_bounds(map, wall_x, wall_y) || !dg_interior_in_bounds(map, target_x, target_y)) {
        return;
    }

    if (dg_map_get_tile(map, wall_x, wall_y) != DG_TILE_WALL) {
        return;
    }
    if (!dg_is_walkable_tile(dg_map_get_tile(map, target_x, target_y))) {
        return;
    }

    target_index = dg_tile_index(map, target_x, target_y);
    target_region = regions[target_index];
    if (target_region <= 0 || target_region == room_region) {
        return;
    }

    if (dg_region_find_root(parents, room_region) == dg_region_find_root(parents, target_region)) {
        return;
    }

    target_room_id = -1;
    if (target_region <= room_count) {
        target_room_id = target_region - 1;
        if (target_room_id == room_id) {
            return;
        }
        if (room_links != NULL &&
            room_links[(size_t)room_id * (size_t)room_count + (size_t)target_room_id] != 0u) {
            return;
        }
    }

    candidates[*in_out_candidate_count] = (dg_room_connector_t){
        wall_x,
        wall_y,
        target_region,
        target_room_id
    };
    *in_out_candidate_count += 1;
}

static dg_status_t dg_connect_rooms_to_other_regions(
    dg_map_t *map,
    int *regions,
    int next_region_id,
    dg_rng_t *rng
)
{
    int room_count;
    int *room_order;
    unsigned char *room_links;
    int *parents;
    size_t link_count;
    int i;

    if (map == NULL || map->tiles == NULL || regions == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    room_count = (int)map->metadata.room_count;
    if (room_count <= 0) {
        return DG_STATUS_OK;
    }
    if (next_region_id <= 1) {
        return DG_STATUS_GENERATION_FAILED;
    }

    if ((size_t)room_count > (SIZE_MAX / (size_t)room_count)) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    link_count = (size_t)room_count * (size_t)room_count;

    room_order = (int *)malloc((size_t)room_count * sizeof(int));
    room_links = (unsigned char *)calloc(link_count, sizeof(unsigned char));
    parents = (int *)malloc((size_t)next_region_id * sizeof(int));
    if (room_order == NULL || room_links == NULL || parents == NULL) {
        free(room_order);
        free(room_links);
        free(parents);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (i = 0; i < next_region_id; ++i) {
        parents[i] = i;
    }
    for (i = 0; i < room_count; ++i) {
        room_order[i] = i;
    }
    dg_shuffle_ints(room_order, (size_t)room_count, rng);

    for (i = 0; i < room_count; ++i) {
        int room_id = room_order[i];
        const dg_room_metadata_t *room = &map->metadata.rooms[room_id];
        int room_region = room_id + 1;
        size_t candidate_capacity;
        dg_room_connector_t *candidates;
        size_t candidate_count;
        int x;
        int y;

        candidate_capacity = (size_t)(room->bounds.width * 2 + room->bounds.height * 2);
        if (candidate_capacity == 0) {
            continue;
        }

        candidates = (dg_room_connector_t *)malloc(
            candidate_capacity * sizeof(dg_room_connector_t)
        );
        if (candidates == NULL) {
            free(room_order);
            free(room_links);
            free(parents);
            return DG_STATUS_ALLOCATION_FAILED;
        }
        candidate_count = 0;

        for (x = room->bounds.x; x < room->bounds.x + room->bounds.width; ++x) {
            dg_try_add_room_connector_candidate(
                map,
                regions,
                parents,
                room_id,
                room_region,
                room_count,
                room_links,
                x,
                room->bounds.y,
                0,
                -1,
                candidates,
                candidate_capacity,
                &candidate_count
            );
            dg_try_add_room_connector_candidate(
                map,
                regions,
                parents,
                room_id,
                room_region,
                room_count,
                room_links,
                x,
                room->bounds.y + room->bounds.height - 1,
                0,
                1,
                candidates,
                candidate_capacity,
                &candidate_count
            );
        }

        for (y = room->bounds.y + 1; y < room->bounds.y + room->bounds.height - 1; ++y) {
            dg_try_add_room_connector_candidate(
                map,
                regions,
                parents,
                room_id,
                room_region,
                room_count,
                room_links,
                room->bounds.x,
                y,
                -1,
                0,
                candidates,
                candidate_capacity,
                &candidate_count
            );
            dg_try_add_room_connector_candidate(
                map,
                regions,
                parents,
                room_id,
                room_region,
                room_count,
                room_links,
                room->bounds.x + room->bounds.width - 1,
                y,
                1,
                0,
                candidates,
                candidate_capacity,
                &candidate_count
            );
        }

        if (candidate_count > 0) {
            const dg_room_connector_t *chosen =
                &candidates[dg_rng_range(rng, 0, (int)candidate_count - 1)];
            size_t wall_index = dg_tile_index(map, chosen->wall_x, chosen->wall_y);

            (void)dg_map_set_tile(map, chosen->wall_x, chosen->wall_y, DG_TILE_FLOOR);
            regions[wall_index] = room_region;

            if (chosen->target_room_id >= 0) {
                dg_status_t status;
                room_links[(size_t)room_id * (size_t)room_count + (size_t)chosen->target_room_id] = 1u;
                room_links[(size_t)chosen->target_room_id * (size_t)room_count + (size_t)room_id] = 1u;

                status = dg_map_add_corridor(map, room_id, chosen->target_room_id, 1, 1);
                if (status != DG_STATUS_OK) {
                    free(candidates);
                    free(room_order);
                    free(room_links);
                    free(parents);
                    return status;
                }
            }

            dg_region_union(parents, room_region, chosen->target_region);
        }

        free(candidates);
    }

    free(room_order);
    free(room_links);
    free(parents);
    return DG_STATUS_OK;
}

static dg_status_t dg_remove_dead_ends(dg_map_t *map, int *regions, int room_count)
{
    size_t cell_count;
    size_t *to_remove;

    if (map == NULL || map->tiles == NULL || regions == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    to_remove = (size_t *)malloc(cell_count * sizeof(size_t));
    if (to_remove == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    while (true) {
        size_t remove_count;
        int x;
        int y;

        remove_count = 0;
        for (y = 1; y < map->height - 1; ++y) {
            for (x = 1; x < map->width - 1; ++x) {
                size_t index = dg_tile_index(map, x, y);
                int neighbors;
                int d;

                if (!dg_is_walkable_tile(map->tiles[index])) {
                    continue;
                }

                if (regions[index] > 0 && regions[index] <= room_count) {
                    continue;
                }

                neighbors = 0;
                for (d = 0; d < 4; ++d) {
                    int nx = x + DG_CARDINAL_DIRECTIONS[d][0];
                    int ny = y + DG_CARDINAL_DIRECTIONS[d][1];
                    if (!dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
                        continue;
                    }
                    neighbors += 1;
                }

                if (neighbors <= 1) {
                    to_remove[remove_count++] = index;
                }
            }
        }

        if (remove_count == 0) {
            break;
        }

        {
            size_t remove_index;
            for (remove_index = 0; remove_index < remove_count; ++remove_index) {
                size_t index = to_remove[remove_index];
                map->tiles[index] = DG_TILE_WALL;
                regions[index] = -1;
            }
        }
    }

    free(to_remove);
    return DG_STATUS_OK;
}

dg_status_t dg_generate_rooms_and_mazes_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    const dg_rooms_and_mazes_config_t *config;
    size_t cell_count;
    int *regions;
    int next_region_id;
    dg_status_t status;

    if (request == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    config = &request->params.rooms_and_mazes;
    cell_count = (size_t)map->width * (size_t)map->height;
    regions = (int *)malloc(cell_count * sizeof(int));
    if (regions == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    memset(regions, 0xFF, cell_count * sizeof(int));

    status = dg_place_random_rooms(config, map, rng, regions, &next_region_id);
    if (status != DG_STATUS_OK) {
        free(regions);
        return status;
    }

    status = dg_generate_maze_regions(map, regions, next_region_id, rng, &next_region_id);
    if (status != DG_STATUS_OK) {
        free(regions);
        return status;
    }

    status = dg_connect_rooms_to_other_regions(map, regions, next_region_id, rng);
    if (status != DG_STATUS_OK) {
        free(regions);
        return status;
    }

    status = dg_remove_dead_ends(map, regions, (int)map->metadata.room_count);
    free(regions);
    if (status != DG_STATUS_OK) {
        return status;
    }

    return DG_STATUS_OK;
}
