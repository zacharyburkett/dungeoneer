#include "internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct dg_maze_cell {
    int x;
    int y;
    int last_dir;
} dg_maze_cell_t;

typedef struct dg_room_connector {
    int wall_x;
    int wall_y;
    int target_region;
    int target_room_id;
} dg_room_connector_t;

typedef struct dg_region_connector {
    int wall_x;
    int wall_y;
    int region_a;
    int region_b;
    int room_a;
    int room_b;
} dg_region_connector_t;

static const int DG_CARDINAL_DIRECTIONS[4][2] = {
    {1, 0},
    {-1, 0},
    {0, 1},
    {0, -1}
};

static bool dg_grid_in_bounds(const dg_map_t *map, int x, int y)
{
    return dg_map_in_bounds(map, x, y);
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

static bool dg_rng_range_with_parity(
    dg_rng_t *rng,
    int min_value,
    int max_value,
    int parity,
    int *out_value
)
{
    int first;
    int last;
    int step_count;

    if (rng == NULL || out_value == NULL || min_value > max_value) {
        return false;
    }

    first = min_value;
    if ((first & 1) != parity) {
        first += 1;
    }

    last = max_value;
    if ((last & 1) != parity) {
        last -= 1;
    }

    if (first > last) {
        return false;
    }

    step_count = (last - first) / 2;
    *out_value = first + dg_rng_range(rng, 0, step_count) * 2;
    return true;
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

static bool dg_point_inside_any_room(const dg_map_t *map, int x, int y)
{
    size_t i;

    if (map == NULL || map->metadata.rooms == NULL) {
        return false;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_rect_t *room = &map->metadata.rooms[i].bounds;
        if (x >= room->x && y >= room->y && x < room->x + room->width && y < room->y + room->height) {
            return true;
        }
    }

    return false;
}

static bool dg_wall_causes_room_diagonal_touch(const dg_map_t *map, int wall_x, int wall_y)
{
    static const int cardinals[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    static const int diagonals[4][2] = {
        {1, 1},
        {1, -1},
        {-1, 1},
        {-1, -1}
    };
    bool orth_room_neighbor;
    bool diagonal_room_neighbor;
    int d;

    if (map == NULL || map->metadata.room_count == 0) {
        return false;
    }

    orth_room_neighbor = false;
    diagonal_room_neighbor = false;

    for (d = 0; d < 4; ++d) {
        int nx = wall_x + cardinals[d][0];
        int ny = wall_y + cardinals[d][1];
        if (!dg_map_in_bounds(map, nx, ny)) {
            continue;
        }
        if (!dg_point_inside_any_room(map, nx, ny)) {
            continue;
        }
        if (!dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
            continue;
        }
        orth_room_neighbor = true;
        break;
    }

    for (d = 0; d < 4; ++d) {
        int nx = wall_x + diagonals[d][0];
        int ny = wall_y + diagonals[d][1];
        if (!dg_map_in_bounds(map, nx, ny)) {
            continue;
        }
        if (!dg_point_inside_any_room(map, nx, ny)) {
            continue;
        }
        if (!dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
            continue;
        }
        diagonal_room_neighbor = true;
        break;
    }

    return diagonal_room_neighbor && !orth_room_neighbor;
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
    int grid_parity_x,
    int grid_parity_y,
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
        (grid_parity_x != 0 && grid_parity_x != 1) ||
        (grid_parity_y != 0 && grid_parity_y != 1) ||
        regions == NULL ||
        out_next_region_id == NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    max_room_width = dg_min_int(config->room_max_size, map->width);
    max_room_height = dg_min_int(config->room_max_size, map->height);
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

        if (!dg_rng_range_with_parity(
                rng,
                config->room_min_size,
                max_room_width,
                1,
                &room.width
            )) {
            room.width = dg_rng_range(rng, config->room_min_size, max_room_width);
        }
        if (!dg_rng_range_with_parity(
                rng,
                config->room_min_size,
                max_room_height,
                1,
                &room.height
            )) {
            room.height = dg_rng_range(rng, config->room_min_size, max_room_height);
        }

        max_x = map->width - room.width;
        max_y = map->height - room.height;
        if (max_x < 0 || max_y < 0) {
            continue;
        }

        if (!dg_rng_range_with_parity(rng, 0, max_x, grid_parity_x, &room.x)) {
            room.x = dg_rng_range(rng, 0, max_x);
        }
        if (!dg_rng_range_with_parity(rng, 0, max_y, grid_parity_y, &room.y)) {
            room.y = dg_rng_range(rng, 0, max_y);
        }

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
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    static const int diagonal_directions[4][2] = {
        {1, 1},
        {1, -1},
        {-1, 1},
        {-1, -1}
    };
    int mid_x;
    int mid_y;
    int dst_x;
    int dst_y;
    size_t source_index;
    size_t mid_index;
    size_t dst_index;
    int source_region;
    int d;

    mid_x = x + dir_x;
    mid_y = y + dir_y;
    dst_x = x + dir_x * 2;
    dst_y = y + dir_y * 2;

    if (!dg_grid_in_bounds(map, mid_x, mid_y) || !dg_grid_in_bounds(map, dst_x, dst_y)) {
        return false;
    }

    if (dg_map_get_tile(map, mid_x, mid_y) != DG_TILE_WALL) {
        return false;
    }
    if (dg_map_get_tile(map, dst_x, dst_y) != DG_TILE_WALL) {
        return false;
    }

    source_index = dg_tile_index(map, x, y);
    mid_index = dg_tile_index(map, mid_x, mid_y);
    dst_index = dg_tile_index(map, dst_x, dst_y);
    source_region = regions[source_index];
    if (regions[mid_index] != -1 || regions[dst_index] != -1) {
        return false;
    }

    /*
     * Keep a one-wall buffer from existing walkable space.
     * Mid can touch the source and the new destination.
     * Destination can only touch the new mid cell.
     */
    for (d = 0; d < 4; ++d) {
        int nx = mid_x + directions[d][0];
        int ny = mid_y + directions[d][1];

        if (!dg_grid_in_bounds(map, nx, ny)) {
            continue;
        }

        if ((nx == x && ny == y) || (nx == dst_x && ny == dst_y)) {
            continue;
        }

        if (dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
            return false;
        }
    }

    for (d = 0; d < 4; ++d) {
        int nx = dst_x + directions[d][0];
        int ny = dst_y + directions[d][1];

        if (!dg_grid_in_bounds(map, nx, ny)) {
            continue;
        }

        if (nx == mid_x && ny == mid_y) {
            continue;
        }

        if (dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
            return false;
        }
    }

    /*
     * Also block diagonal touching against foreign walkable regions
     * (rooms or other maze regions). Same-region diagonals are allowed
     * so corridors can still make turns.
     */
    for (d = 0; d < 4; ++d) {
        int nx = mid_x + diagonal_directions[d][0];
        int ny = mid_y + diagonal_directions[d][1];
        size_t nindex;
        int neighbor_region;

        if (!dg_grid_in_bounds(map, nx, ny)) {
            continue;
        }

        if (!dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
            continue;
        }

        nindex = dg_tile_index(map, nx, ny);
        neighbor_region = regions[nindex];
        if (source_region > 0 && neighbor_region == source_region) {
            continue;
        }
        return false;
    }

    for (d = 0; d < 4; ++d) {
        int nx = dst_x + diagonal_directions[d][0];
        int ny = dst_y + diagonal_directions[d][1];
        size_t nindex;
        int neighbor_region;

        if (!dg_grid_in_bounds(map, nx, ny)) {
            continue;
        }

        if (!dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
            continue;
        }

        nindex = dg_tile_index(map, nx, ny);
        neighbor_region = regions[nindex];
        if (source_region > 0 && neighbor_region == source_region) {
            continue;
        }
        return false;
    }

    return true;
}

static bool dg_can_start_maze_region(
    const dg_map_t *map,
    const int *regions,
    int start_x,
    int start_y
)
{
    int d;

    for (d = 0; d < 4; ++d) {
        if (dg_can_carve_maze_step(
                map,
                regions,
                start_x,
                start_y,
                DG_CARDINAL_DIRECTIONS[d][0],
                DG_CARDINAL_DIRECTIONS[d][1]
            )) {
            return true;
        }
    }

    return false;
}

static dg_status_t dg_carve_maze_region(
    dg_map_t *map,
    int *regions,
    int start_x,
    int start_y,
    int region_id,
    int wiggle_percent,
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
    stack[stack_count++] = (dg_maze_cell_t){start_x, start_y, -1};

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
        bool can_continue_straight;
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

        can_continue_straight = false;
        if (cell.last_dir >= 0) {
            for (d = 0; d < valid_count; ++d) {
                if (valid_dirs[d] == cell.last_dir) {
                    can_continue_straight = true;
                    break;
                }
            }
        }

        if (can_continue_straight && dg_rng_range(rng, 0, 99) >= wiggle_percent) {
            dir_choice = cell.last_dir;
        } else {
            dir_choice = valid_dirs[dg_rng_range(rng, 0, valid_count - 1)];
            if (can_continue_straight && valid_count > 1 && dir_choice == cell.last_dir) {
                int fallback_index = dg_rng_range(rng, 0, valid_count - 2);
                if (valid_dirs[fallback_index] == cell.last_dir) {
                    fallback_index = valid_count - 1;
                }
                dir_choice = valid_dirs[fallback_index];
            }
        }
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

        stack[stack_count++] = (dg_maze_cell_t){dst_x, dst_y, dir_choice};
    }

    free(stack);
    return DG_STATUS_OK;
}

static dg_status_t dg_generate_maze_regions(
    dg_map_t *map,
    int *regions,
    int next_region_id,
    int wiggle_percent,
    int start_x,
    int start_y,
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
        (start_x != 0 && start_x != 1) ||
        (start_y != 0 && start_y != 1) ||
        out_next_region_id == NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (y = start_y; y < map->height; y += 2) {
        for (x = start_x; x < map->width; x += 2) {
            size_t index = dg_tile_index(map, x, y);
            dg_status_t status;

            if (map->tiles[index] != DG_TILE_WALL || regions[index] != -1) {
                continue;
            }

            if (!dg_can_start_maze_region(map, regions, x, y)) {
                continue;
            }

            status = dg_carve_maze_region(
                map,
                regions,
                x,
                y,
                next_region_id,
                wiggle_percent,
                rng
            );
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
    if (!dg_grid_in_bounds(map, wall_x, wall_y) || !dg_grid_in_bounds(map, target_x, target_y)) {
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

static bool dg_room_pair_is_linked(
    const unsigned char *room_links,
    int room_count,
    int room_a,
    int room_b
)
{
    if (room_links == NULL || room_a < 0 || room_b < 0 || room_a == room_b) {
        return false;
    }

    return room_links[(size_t)room_a * (size_t)room_count + (size_t)room_b] != 0u;
}

static void dg_mark_room_pair_linked(unsigned char *room_links, int room_count, int room_a, int room_b)
{
    if (room_links == NULL || room_a < 0 || room_b < 0 || room_a == room_b) {
        return;
    }

    room_links[(size_t)room_a * (size_t)room_count + (size_t)room_b] = 1u;
    room_links[(size_t)room_b * (size_t)room_count + (size_t)room_a] = 1u;
}

static int dg_collect_wall_neighbor_regions(
    const dg_map_t *map,
    const int *regions,
    int room_count,
    int wall_x,
    int wall_y,
    int out_regions[4],
    int out_room_ids[4]
)
{
    int count;
    int d;

    count = 0;
    for (d = 0; d < 4; ++d) {
        int nx = wall_x + DG_CARDINAL_DIRECTIONS[d][0];
        int ny = wall_y + DG_CARDINAL_DIRECTIONS[d][1];
        size_t nindex;
        int region;
        int i;
        bool duplicate;

        if (!dg_grid_in_bounds(map, nx, ny)) {
            continue;
        }
        if (!dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
            continue;
        }

        nindex = dg_tile_index(map, nx, ny);
        region = regions[nindex];
        if (region <= 0) {
            continue;
        }

        duplicate = false;
        for (i = 0; i < count; ++i) {
            if (out_regions[i] == region) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        out_regions[count] = region;
        out_room_ids[count] = (region <= room_count) ? (region - 1) : -1;
        count += 1;
    }

    return count;
}

static void dg_open_wall_and_union_neighbors(
    dg_map_t *map,
    int *regions,
    int *parents,
    int room_count,
    int wall_x,
    int wall_y,
    int assign_region
)
{
    int neighbor_regions[4];
    int neighbor_room_ids[4];
    int neighbor_count;
    int i;
    size_t wall_index;

    wall_index = dg_tile_index(map, wall_x, wall_y);
    (void)dg_map_set_tile(map, wall_x, wall_y, DG_TILE_FLOOR);
    regions[wall_index] = assign_region;

    neighbor_count = dg_collect_wall_neighbor_regions(
        map,
        regions,
        room_count,
        wall_x,
        wall_y,
        neighbor_regions,
        neighbor_room_ids
    );
    (void)neighbor_room_ids;

    for (i = 0; i < neighbor_count; ++i) {
        dg_region_union(parents, assign_region, neighbor_regions[i]);
    }
}

static dg_status_t dg_apply_room_connector(
    dg_map_t *map,
    int *regions,
    int *parents,
    int room_count,
    unsigned char *room_links,
    int source_room_id,
    int source_region,
    const dg_room_connector_t *connector
)
{
    if (
        map == NULL ||
        regions == NULL ||
        parents == NULL ||
        source_room_id < 0 ||
        source_region <= 0 ||
        connector == NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_open_wall_and_union_neighbors(
        map,
        regions,
        parents,
        room_count,
        connector->wall_x,
        connector->wall_y,
        source_region
    );

    if (connector->target_room_id >= 0) {
        if (!dg_room_pair_is_linked(room_links, room_count, source_room_id, connector->target_room_id)) {
            dg_status_t status;
            dg_mark_room_pair_linked(room_links, room_count, source_room_id, connector->target_room_id);
            status = dg_map_add_corridor(map, source_room_id, connector->target_room_id, 1, 1);
            if (status != DG_STATUS_OK) {
                return status;
            }
        }
    }

    dg_region_union(parents, source_region, connector->target_region);
    return DG_STATUS_OK;
}

static bool dg_choose_random_region_connector(
    const dg_map_t *map,
    const int *regions,
    int *parents,
    int room_count,
    const unsigned char *room_links,
    dg_rng_t *rng,
    dg_region_connector_t *out_connector
)
{
    size_t candidate_count;
    int x;
    int y;
    dg_region_connector_t chosen;

    if (
        map == NULL ||
        regions == NULL ||
        parents == NULL ||
        rng == NULL ||
        out_connector == NULL
    ) {
        return false;
    }

    candidate_count = 0;
    chosen = (dg_region_connector_t){0};

    for (y = 0; y < map->height; ++y) {
        for (x = 0; x < map->width; ++x) {
            int neighbor_regions[4];
            int neighbor_rooms[4];
            int neighbor_count;
            int i;
            int j;

            if (dg_map_get_tile(map, x, y) != DG_TILE_WALL) {
                continue;
            }

            if (dg_wall_causes_room_diagonal_touch(map, x, y)) {
                continue;
            }

            neighbor_count = dg_collect_wall_neighbor_regions(
                map,
                regions,
                room_count,
                x,
                y,
                neighbor_regions,
                neighbor_rooms
            );
            if (neighbor_count < 2) {
                continue;
            }

            for (i = 0; i < neighbor_count; ++i) {
                for (j = i + 1; j < neighbor_count; ++j) {
                    int region_a = neighbor_regions[i];
                    int region_b = neighbor_regions[j];
                    int room_a = neighbor_rooms[i];
                    int room_b = neighbor_rooms[j];

                    if (dg_region_find_root(parents, region_a) == dg_region_find_root(parents, region_b)) {
                        continue;
                    }

                    if (dg_room_pair_is_linked(room_links, room_count, room_a, room_b)) {
                        continue;
                    }

                    candidate_count += 1;
                    if (dg_rng_range(rng, 0, (int)candidate_count - 1) == 0) {
                        chosen = (dg_region_connector_t){
                            x,
                            y,
                            region_a,
                            region_b,
                            room_a,
                            room_b
                        };
                    }
                }
            }
        }
    }

    if (candidate_count == 0) {
        return false;
    }

    *out_connector = chosen;
    return true;
}

static dg_status_t dg_apply_region_connector(
    dg_map_t *map,
    int *regions,
    int *parents,
    int room_count,
    unsigned char *room_links,
    const dg_region_connector_t *connector
)
{
    if (map == NULL || regions == NULL || parents == NULL || connector == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_open_wall_and_union_neighbors(
        map,
        regions,
        parents,
        room_count,
        connector->wall_x,
        connector->wall_y,
        connector->region_a
    );

    if (connector->room_a >= 0 && connector->room_b >= 0) {
        if (!dg_room_pair_is_linked(room_links, room_count, connector->room_a, connector->room_b)) {
            dg_status_t status;
            dg_mark_room_pair_linked(room_links, room_count, connector->room_a, connector->room_b);
            status = dg_map_add_corridor(map, connector->room_a, connector->room_b, 1, 1);
            if (status != DG_STATUS_OK) {
                return status;
            }
        }
    }

    dg_region_union(parents, connector->region_a, connector->region_b);
    return DG_STATUS_OK;
}

static dg_status_t dg_count_region_components(
    const dg_map_t *map,
    const int *regions,
    int *parents,
    int next_region_id,
    int *out_component_count
)
{
    size_t cell_count;
    unsigned char *used;
    unsigned char *seen_root;
    size_t i;
    int component_count;
    int region_id;

    if (
        map == NULL ||
        map->tiles == NULL ||
        regions == NULL ||
        parents == NULL ||
        out_component_count == NULL ||
        next_region_id <= 0
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    used = (unsigned char *)calloc((size_t)next_region_id, sizeof(unsigned char));
    seen_root = (unsigned char *)calloc((size_t)next_region_id, sizeof(unsigned char));
    if (used == NULL || seen_root == NULL) {
        free(used);
        free(seen_root);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    for (i = 0; i < cell_count; ++i) {
        int region;

        if (!dg_is_walkable_tile(map->tiles[i])) {
            continue;
        }

        region = regions[i];
        if (region > 0 && region < next_region_id) {
            used[region] = 1u;
        }
    }

    component_count = 0;
    for (region_id = 1; region_id < next_region_id; ++region_id) {
        int root;
        if (used[region_id] == 0u) {
            continue;
        }

        root = dg_region_find_root(parents, region_id);
        if (root <= 0 || root >= next_region_id) {
            continue;
        }

        if (seen_root[root] == 0u) {
            seen_root[root] = 1u;
            component_count += 1;
        }
    }

    free(used);
    free(seen_root);
    *out_component_count = component_count;
    return DG_STATUS_OK;
}

static dg_status_t dg_connect_rooms_to_other_regions(
    const dg_rooms_and_mazes_config_t *config,
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

    if (
        config == NULL ||
        map == NULL ||
        map->tiles == NULL ||
        regions == NULL ||
        rng == NULL
    ) {
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
        int target_connection_count;
        int made_connections;
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
        target_connection_count = dg_rng_range(
            rng,
            config->min_room_connections,
            config->max_room_connections
        );
        made_connections = 0;
        while (made_connections < target_connection_count) {
            dg_status_t status;
            const dg_room_connector_t *chosen;

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

            if (candidate_count == 0) {
                break;
            }

            chosen = &candidates[dg_rng_range(rng, 0, (int)candidate_count - 1)];
            status = dg_apply_room_connector(
                map,
                regions,
                parents,
                room_count,
                room_links,
                room_id,
                room_region,
                chosen
            );
            if (status != DG_STATUS_OK) {
                free(candidates);
                free(room_order);
                free(room_links);
                free(parents);
                return status;
            }

            made_connections += 1;
        }

        free(candidates);
    }

    if (config->ensure_full_connectivity != 0) {
        while (true) {
            dg_region_connector_t connector;
            dg_status_t status;
            bool found;

            found = dg_choose_random_region_connector(
                map,
                regions,
                parents,
                room_count,
                room_links,
                rng,
                &connector
            );
            if (!found) {
                break;
            }

            status = dg_apply_region_connector(
                map,
                regions,
                parents,
                room_count,
                room_links,
                &connector
            );
            if (status != DG_STATUS_OK) {
                free(room_order);
                free(room_links);
                free(parents);
                return status;
            }
        }

        {
            int component_count;
            dg_status_t status = dg_count_region_components(
                map,
                regions,
                parents,
                next_region_id,
                &component_count
            );
            if (status != DG_STATUS_OK) {
                free(room_order);
                free(room_links);
                free(parents);
                return status;
            }

            if (component_count > 1) {
                free(room_order);
                free(room_links);
                free(parents);
                return DG_STATUS_GENERATION_FAILED;
            }
        }
    }

    free(room_order);
    free(room_links);
    free(parents);
    return DG_STATUS_OK;
}

static dg_status_t dg_remove_dead_ends(
    dg_map_t *map,
    int *regions,
    int max_prune_steps
)
{
    size_t cell_count;
    size_t *to_remove;
    int prune_steps;

    if (map == NULL || map->tiles == NULL || regions == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (max_prune_steps == 0) {
        return DG_STATUS_OK;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    to_remove = (size_t *)malloc(cell_count * sizeof(size_t));
    if (to_remove == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    prune_steps = 0;
    while (true) {
        size_t remove_count;
        int x;
        int y;

        if (max_prune_steps > 0 && prune_steps >= max_prune_steps) {
            break;
        }

        remove_count = 0;
        for (y = 0; y < map->height; ++y) {
            for (x = 0; x < map->width; ++x) {
                size_t index = dg_tile_index(map, x, y);
                int neighbors;
                int d;

                if (!dg_is_walkable_tile(map->tiles[index])) {
                    continue;
                }

                if (dg_point_inside_any_room(map, x, y)) {
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

        prune_steps += 1;
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
    int grid_parity_x;
    int grid_parity_y;
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
    grid_parity_x = dg_rng_range(rng, 0, 1);
    grid_parity_y = dg_rng_range(rng, 0, 1);
    cell_count = (size_t)map->width * (size_t)map->height;
    regions = (int *)malloc(cell_count * sizeof(int));
    if (regions == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    memset(regions, 0xFF, cell_count * sizeof(int));

    status = dg_place_random_rooms(
        config,
        map,
        rng,
        grid_parity_x,
        grid_parity_y,
        regions,
        &next_region_id
    );
    if (status != DG_STATUS_OK) {
        free(regions);
        return status;
    }

    status = dg_generate_maze_regions(
        map,
        regions,
        next_region_id,
        config->maze_wiggle_percent,
        grid_parity_x,
        grid_parity_y,
        rng,
        &next_region_id
    );
    if (status != DG_STATUS_OK) {
        free(regions);
        return status;
    }

    status = dg_connect_rooms_to_other_regions(config, map, regions, next_region_id, rng);
    if (status != DG_STATUS_OK) {
        free(regions);
        return status;
    }

    status = dg_remove_dead_ends(
        map,
        regions,
        config->dead_end_prune_steps
    );
    free(regions);
    if (status != DG_STATUS_OK) {
        return status;
    }

    return DG_STATUS_OK;
}
