#include "internal.h"

#include <stdlib.h>
#include <string.h>

static bool dg_point_in_any_room(const dg_map_t *map, int x, int y)
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

static bool dg_is_corridor_floor(const dg_map_t *map, int x, int y)
{
    if (map == NULL || !dg_map_in_bounds(map, x, y)) {
        return false;
    }

    return dg_is_walkable_tile(dg_map_get_tile(map, x, y)) &&
           !dg_point_in_any_room(map, x, y);
}

static bool dg_is_corridor_floor_in_tiles(
    const dg_map_t *map,
    const dg_tile_t *tiles,
    int x,
    int y
)
{
    if (map == NULL || tiles == NULL || !dg_map_in_bounds(map, x, y)) {
        return false;
    }

    return dg_is_walkable_tile(tiles[dg_tile_index(map, x, y)]) &&
           !dg_point_in_any_room(map, x, y);
}

static bool dg_is_walkable_room_tile(const dg_map_t *map, int x, int y)
{
    if (map == NULL || !dg_map_in_bounds(map, x, y)) {
        return false;
    }

    return dg_point_in_any_room(map, x, y) &&
           dg_is_walkable_tile(dg_map_get_tile(map, x, y));
}

static bool dg_is_walkable_room_tile_in_tiles(
    const dg_map_t *map,
    const dg_tile_t *tiles,
    int x,
    int y
)
{
    if (map == NULL || tiles == NULL || !dg_map_in_bounds(map, x, y)) {
        return false;
    }

    return dg_point_in_any_room(map, x, y) &&
           dg_is_walkable_tile(tiles[dg_tile_index(map, x, y)]);
}

static bool dg_corridor_touches_room(const dg_map_t *map, int x, int y)
{
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    int d;

    if (!dg_is_corridor_floor(map, x, y)) {
        return false;
    }

    for (d = 0; d < 4; ++d) {
        int nx = x + directions[d][0];
        int ny = y + directions[d][1];

        if (dg_is_walkable_room_tile(map, nx, ny)) {
            return true;
        }
    }

    return false;
}

static bool dg_corridor_touches_room_in_tiles(
    const dg_map_t *map,
    const dg_tile_t *tiles,
    int x,
    int y
)
{
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    int d;

    if (!dg_is_corridor_floor_in_tiles(map, tiles, x, y)) {
        return false;
    }

    for (d = 0; d < 4; ++d) {
        int nx = x + directions[d][0];
        int ny = y + directions[d][1];

        if (dg_is_walkable_room_tile_in_tiles(map, tiles, nx, ny)) {
            return true;
        }
    }

    return false;
}

static bool dg_has_corridor_path_when_blocked(
    const dg_map_t *map,
    const dg_tile_t *tiles,
    int start_x,
    int start_y,
    int target_x,
    int target_y,
    int block_x,
    int block_y,
    unsigned char *visited,
    size_t *queue
)
{
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    size_t cell_count;
    size_t head;
    size_t tail;

    if (map == NULL || tiles == NULL || visited == NULL || queue == NULL) {
        return false;
    }
    if (!dg_map_in_bounds(map, start_x, start_y) ||
        !dg_map_in_bounds(map, target_x, target_y)) {
        return false;
    }
    if (!dg_is_corridor_floor_in_tiles(map, tiles, start_x, start_y) ||
        !dg_is_corridor_floor_in_tiles(map, tiles, target_x, target_y)) {
        return false;
    }
    if (start_x == target_x && start_y == target_y) {
        return true;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    memset(visited, 0, cell_count * sizeof(*visited));
    head = 0;
    tail = 0;

    queue[tail++] = dg_tile_index(map, start_x, start_y);
    visited[dg_tile_index(map, start_x, start_y)] = 1u;

    while (head < tail) {
        size_t current = queue[head++];
        int x = (int)(current % (size_t)map->width);
        int y = (int)(current / (size_t)map->width);
        int d;

        for (d = 0; d < 4; ++d) {
            int nx = x + directions[d][0];
            int ny = y + directions[d][1];
            size_t nindex;

            if (!dg_map_in_bounds(map, nx, ny)) {
                continue;
            }
            if (nx == block_x && ny == block_y) {
                continue;
            }
            if (!dg_is_corridor_floor_in_tiles(map, tiles, nx, ny)) {
                continue;
            }

            nindex = dg_tile_index(map, nx, ny);
            if (visited[nindex] != 0u) {
                continue;
            }
            if (nx == target_x && ny == target_y) {
                return true;
            }

            visited[nindex] = 1u;
            queue[tail++] = nindex;
        }
    }

    return false;
}

size_t dg_count_walkable_tiles(const dg_map_t *map)
{
    size_t i;
    size_t walkable_count;
    size_t cell_count;

    walkable_count = 0;
    cell_count = (size_t)map->width * (size_t)map->height;
    for (i = 0; i < cell_count; ++i) {
        if (dg_is_walkable_tile(map->tiles[i])) {
            walkable_count += 1;
        }
    }

    return walkable_count;
}

dg_status_t dg_enforce_single_connected_region(dg_map_t *map)
{
    size_t cell_count;
    unsigned char *visited;
    size_t *queue;
    size_t head;
    size_t tail;
    size_t start_index;
    size_t i;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    if (map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    visited = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
    queue = (size_t *)malloc(cell_count * sizeof(size_t));
    if (visited == NULL || queue == NULL) {
        free(visited);
        free(queue);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    start_index = cell_count;
    for (i = 0; i < cell_count; ++i) {
        if (dg_is_walkable_tile(map->tiles[i])) {
            start_index = i;
            break;
        }
    }

    if (start_index == cell_count) {
        free(visited);
        free(queue);
        return DG_STATUS_OK;
    }

    head = 0;
    tail = 0;
    queue[tail++] = start_index;
    visited[start_index] = 1;

    while (head < tail) {
        size_t current = queue[head++];
        int x = (int)(current % (size_t)map->width);
        int y = (int)(current / (size_t)map->width);
        int d;

        for (d = 0; d < 4; ++d) {
            int nx = x + directions[d][0];
            int ny = y + directions[d][1];
            size_t neighbor_index;

            if (!dg_map_in_bounds(map, nx, ny)) {
                continue;
            }

            neighbor_index = dg_tile_index(map, nx, ny);
            if (visited[neighbor_index] != 0) {
                continue;
            }

            if (!dg_is_walkable_tile(map->tiles[neighbor_index])) {
                continue;
            }

            visited[neighbor_index] = 1;
            queue[tail++] = neighbor_index;
        }
    }

    for (i = 0; i < cell_count; ++i) {
        if (dg_is_walkable_tile(map->tiles[i]) && visited[i] == 0) {
            map->tiles[i] = DG_TILE_WALL;
        }
    }

    free(visited);
    free(queue);
    return DG_STATUS_OK;
}

static dg_status_t dg_analyze_connectivity_top_down(
    const dg_map_t *map,
    dg_connectivity_stats_t *out_stats
)
{
    size_t cell_count;
    unsigned char *visited;
    size_t *queue;
    size_t i;
    size_t head;
    size_t tail;
    size_t walkable_count;
    size_t component_count;
    size_t largest_component_size;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    if (map == NULL || map->tiles == NULL || out_stats == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    visited = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
    queue = (size_t *)malloc(cell_count * sizeof(size_t));
    if (visited == NULL || queue == NULL) {
        free(visited);
        free(queue);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    walkable_count = 0;
    component_count = 0;
    largest_component_size = 0;

    for (i = 0; i < cell_count; ++i) {
        size_t component_size;

        if (visited[i] != 0 || !dg_is_walkable_tile(map->tiles[i])) {
            continue;
        }

        component_count += 1;
        component_size = 0;

        head = 0;
        tail = 0;
        visited[i] = 1;
        queue[tail++] = i;

        while (head < tail) {
            size_t current = queue[head++];
            int x = (int)(current % (size_t)map->width);
            int y = (int)(current / (size_t)map->width);
            int d;

            component_size += 1;

            for (d = 0; d < 4; ++d) {
                int nx = x + directions[d][0];
                int ny = y + directions[d][1];
                size_t neighbor_index;

                if (!dg_map_in_bounds(map, nx, ny)) {
                    continue;
                }

                neighbor_index = dg_tile_index(map, nx, ny);
                if (visited[neighbor_index] != 0 || !dg_is_walkable_tile(map->tiles[neighbor_index])) {
                    continue;
                }

                visited[neighbor_index] = 1;
                queue[tail++] = neighbor_index;
            }
        }

        walkable_count += component_size;
        if (component_size > largest_component_size) {
            largest_component_size = component_size;
        }
    }

    out_stats->walkable_count = walkable_count;
    out_stats->component_count = component_count;
    out_stats->largest_component_size = largest_component_size;
    out_stats->connected_floor = (walkable_count > 0 && component_count == 1);

    free(visited);
    free(queue);
    return DG_STATUS_OK;
}

static bool dg_is_standable_side_view_tile(
    const dg_map_t *map,
    const unsigned char *walkable,
    int x,
    int y
)
{
    size_t index;
    size_t below_index;

    if (map == NULL || walkable == NULL || !dg_map_in_bounds(map, x, y)) {
        return false;
    }

    index = dg_tile_index(map, x, y);
    if (walkable[index] == 0u) {
        return false;
    }

    if (y + 1 >= map->height) {
        return true;
    }

    below_index = dg_tile_index(map, x, y + 1);
    return walkable[below_index] == 0u;
}

static bool dg_side_view_line_is_clear(
    const dg_map_t *map,
    const unsigned char *walkable,
    int x0,
    int y0,
    int x1,
    int y1
)
{
    int dx;
    int sx;
    int dy;
    int sy;
    int err;

    if (map == NULL || walkable == NULL) {
        return false;
    }

    dx = abs(x1 - x0);
    sx = (x0 < x1) ? 1 : -1;
    dy = -abs(y1 - y0);
    sy = (y0 < y1) ? 1 : -1;
    err = dx + dy;

    while (true) {
        size_t index;
        int e2;

        if (!dg_map_in_bounds(map, x0, y0)) {
            return false;
        }

        index = dg_tile_index(map, x0, y0);
        if (walkable[index] == 0u) {
            return false;
        }

        if (x0 == x1 && y0 == y1) {
            break;
        }

        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }

    return true;
}

static bool dg_side_view_can_move_one_way(
    const dg_map_t *map,
    const unsigned char *walkable,
    int source_x,
    int source_y,
    int target_x,
    int target_y,
    const dg_traversal_constraints_t *traversal
)
{
    int dx;
    int dy;

    if (map == NULL || walkable == NULL || traversal == NULL) {
        return false;
    }

    dx = abs(target_x - source_x);
    dy = target_y - source_y;

    if (dx > traversal->max_jump_across) {
        return false;
    }

    if (dy < 0) {
        if (-dy > traversal->max_jump_up) {
            return false;
        }
    } else if (dy > 0) {
        if (dy > traversal->max_drop_down) {
            return false;
        }
    }

    return dg_side_view_line_is_clear(map, walkable, source_x, source_y, target_x, target_y);
}

static bool dg_side_view_nodes_linked(
    const dg_map_t *map,
    const unsigned char *walkable,
    int source_x,
    int source_y,
    int target_x,
    int target_y,
    const dg_traversal_constraints_t *traversal
)
{
    if (source_x == target_x && source_y == target_y) {
        return true;
    }

    return dg_side_view_can_move_one_way(
               map,
               walkable,
               source_x,
               source_y,
               target_x,
               target_y,
               traversal
           ) ||
           dg_side_view_can_move_one_way(
               map,
               walkable,
               target_x,
               target_y,
               source_x,
               source_y,
               traversal
           );
}

static dg_status_t dg_analyze_side_view_grounded_connectivity(
    const dg_map_t *map,
    const dg_traversal_constraints_t *traversal,
    dg_connectivity_stats_t *out_stats
)
{
    size_t cell_count;
    unsigned char *walkable;
    unsigned char *standable;
    int *standable_node_ids;
    size_t *standable_tiles;
    unsigned char *visited_nodes;
    size_t *queue_nodes;
    size_t standable_count;
    size_t i;
    size_t component_count;
    size_t largest_component_size;
    int max_jump_across;
    int max_jump_up;
    int max_drop_down;

    if (map == NULL || map->tiles == NULL || traversal == NULL || out_stats == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    walkable = (unsigned char *)malloc(cell_count * sizeof(*walkable));
    standable = (unsigned char *)calloc(cell_count, sizeof(*standable));
    standable_node_ids = (int *)malloc(cell_count * sizeof(*standable_node_ids));
    standable_tiles = (size_t *)malloc(cell_count * sizeof(*standable_tiles));
    visited_nodes = (unsigned char *)calloc(cell_count, sizeof(*visited_nodes));
    queue_nodes = (size_t *)malloc(cell_count * sizeof(*queue_nodes));
    if (walkable == NULL || standable == NULL ||
        standable_node_ids == NULL || standable_tiles == NULL ||
        visited_nodes == NULL || queue_nodes == NULL) {
        free(walkable);
        free(standable);
        free(standable_node_ids);
        free(standable_tiles);
        free(visited_nodes);
        free(queue_nodes);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (i = 0; i < cell_count; ++i) {
        walkable[i] = dg_is_walkable_tile(map->tiles[i]) ? 1u : 0u;
        standable_node_ids[i] = -1;
    }

    standable_count = 0;
    for (i = 0; i < cell_count; ++i) {
        int x = (int)(i % (size_t)map->width);
        int y = (int)(i / (size_t)map->width);

        if (!dg_is_standable_side_view_tile(map, walkable, x, y)) {
            continue;
        }

        standable[i] = 1u;
        standable_node_ids[i] = (int)standable_count;
        standable_tiles[standable_count++] = i;
    }

    if (standable_count == 0u) {
        out_stats->walkable_count = 0u;
        out_stats->component_count = 0u;
        out_stats->largest_component_size = 0u;
        out_stats->connected_floor = false;
        free(walkable);
        free(standable);
        free(standable_node_ids);
        free(standable_tiles);
        free(visited_nodes);
        free(queue_nodes);
        return DG_STATUS_OK;
    }

    max_jump_across = dg_min_int(traversal->max_jump_across, map->width - 1);
    max_jump_up = dg_min_int(traversal->max_jump_up, map->height - 1);
    max_drop_down = dg_min_int(traversal->max_drop_down, map->height - 1);

    component_count = 0;
    largest_component_size = 0;

    for (i = 0; i < standable_count; ++i) {
        size_t head;
        size_t tail;
        size_t component_size;

        if (visited_nodes[i] != 0u) {
            continue;
        }

        component_count += 1;
        component_size = 0;
        head = 0;
        tail = 0;
        visited_nodes[i] = 1u;
        queue_nodes[tail++] = i;

        while (head < tail) {
            size_t node_id = queue_nodes[head++];
            size_t tile_index = standable_tiles[node_id];
            int source_x = (int)(tile_index % (size_t)map->width);
            int source_y = (int)(tile_index / (size_t)map->width);
            int min_x;
            int max_x;
            int min_y;
            int max_y;
            int target_y;

            component_size += 1;

            min_x = dg_max_int(0, source_x - max_jump_across);
            max_x = dg_min_int(map->width - 1, source_x + max_jump_across);
            min_y = dg_max_int(0, source_y - max_jump_up);
            max_y = dg_min_int(map->height - 1, source_y + max_drop_down);

            for (target_y = min_y; target_y <= max_y; ++target_y) {
                int target_x;
                size_t row_start = (size_t)target_y * (size_t)map->width;

                for (target_x = min_x; target_x <= max_x; ++target_x) {
                    size_t target_tile_index = row_start + (size_t)target_x;
                    int target_node_id;

                    if (target_x == source_x && target_y == source_y) {
                        continue;
                    }
                    if (standable[target_tile_index] == 0u) {
                        continue;
                    }

                    target_node_id = standable_node_ids[target_tile_index];
                    if (target_node_id < 0 || visited_nodes[target_node_id] != 0u) {
                        continue;
                    }

                    if (!dg_side_view_nodes_linked(
                            map,
                            walkable,
                            source_x,
                            source_y,
                            target_x,
                            target_y,
                            traversal
                        )) {
                        continue;
                    }

                    visited_nodes[target_node_id] = 1u;
                    queue_nodes[tail++] = (size_t)target_node_id;
                }
            }
        }

        if (component_size > largest_component_size) {
            largest_component_size = component_size;
        }
    }

    out_stats->walkable_count = standable_count;
    out_stats->component_count = component_count;
    out_stats->largest_component_size = largest_component_size;
    out_stats->connected_floor = (standable_count > 0u && component_count == 1u);

    free(walkable);
    free(standable);
    free(standable_node_ids);
    free(standable_tiles);
    free(visited_nodes);
    free(queue_nodes);
    return DG_STATUS_OK;
}

dg_status_t dg_analyze_connectivity_for_request(
    const dg_map_t *map,
    dg_generation_perspective_t perspective,
    const dg_traversal_constraints_t *traversal,
    dg_connectivity_stats_t *out_stats
)
{
    if (map == NULL || map->tiles == NULL || out_stats == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (perspective != DG_GENERATION_PERSPECTIVE_TOP_DOWN &&
        perspective != DG_GENERATION_PERSPECTIVE_SIDE_VIEW) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (perspective == DG_GENERATION_PERSPECTIVE_SIDE_VIEW) {
        if (traversal == NULL ||
            traversal->max_jump_up < 0 ||
            traversal->max_jump_across < 0 ||
            traversal->max_drop_down < 0 ||
            (traversal->require_grounded_connectivity != 0 &&
             traversal->require_grounded_connectivity != 1)) {
            return DG_STATUS_INVALID_ARGUMENT;
        }
    }

    if (perspective == DG_GENERATION_PERSPECTIVE_SIDE_VIEW &&
        traversal->require_grounded_connectivity != 0) {
        return dg_analyze_side_view_grounded_connectivity(map, traversal, out_stats);
    }

    return dg_analyze_connectivity_top_down(map, out_stats);
}

dg_status_t dg_analyze_connectivity(const dg_map_t *map, dg_connectivity_stats_t *out_stats)
{
    return dg_analyze_connectivity_top_down(map, out_stats);
}

dg_status_t dg_enforce_side_view_grounded_connectivity(
    dg_map_t *map,
    const dg_traversal_constraints_t *traversal
)
{
    size_t cell_count;
    unsigned char *walkable;
    unsigned char *standable;
    int *standable_node_ids;
    size_t *standable_tiles;
    unsigned char *visited_nodes;
    size_t *queue_nodes;
    int *node_component_ids;
    size_t *component_sizes;
    int *standable_component_by_tile;
    int *fall_component_by_tile;
    size_t standable_count;
    size_t component_count;
    size_t largest_component_id;
    size_t largest_component_size;
    int max_jump_across;
    int max_jump_up;
    int max_drop_down;
    size_t i;

    if (map == NULL || map->tiles == NULL || traversal == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }
    if (traversal->max_jump_up < 0 ||
        traversal->max_jump_across < 0 ||
        traversal->max_drop_down < 0 ||
        (traversal->require_grounded_connectivity != 0 &&
         traversal->require_grounded_connectivity != 1)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }
    if (traversal->require_grounded_connectivity == 0) {
        return DG_STATUS_OK;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    walkable = (unsigned char *)malloc(cell_count * sizeof(*walkable));
    standable = (unsigned char *)calloc(cell_count, sizeof(*standable));
    standable_node_ids = (int *)malloc(cell_count * sizeof(*standable_node_ids));
    standable_tiles = (size_t *)malloc(cell_count * sizeof(*standable_tiles));
    visited_nodes = (unsigned char *)calloc(cell_count, sizeof(*visited_nodes));
    queue_nodes = (size_t *)malloc(cell_count * sizeof(*queue_nodes));
    node_component_ids = (int *)malloc(cell_count * sizeof(*node_component_ids));
    component_sizes = (size_t *)calloc(cell_count, sizeof(*component_sizes));
    standable_component_by_tile = (int *)malloc(cell_count * sizeof(*standable_component_by_tile));
    fall_component_by_tile = (int *)malloc(cell_count * sizeof(*fall_component_by_tile));
    if (walkable == NULL || standable == NULL ||
        standable_node_ids == NULL || standable_tiles == NULL ||
        visited_nodes == NULL || queue_nodes == NULL ||
        node_component_ids == NULL || component_sizes == NULL ||
        standable_component_by_tile == NULL || fall_component_by_tile == NULL) {
        free(walkable);
        free(standable);
        free(standable_node_ids);
        free(standable_tiles);
        free(visited_nodes);
        free(queue_nodes);
        free(node_component_ids);
        free(component_sizes);
        free(standable_component_by_tile);
        free(fall_component_by_tile);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (i = 0; i < cell_count; ++i) {
        walkable[i] = dg_is_walkable_tile(map->tiles[i]) ? 1u : 0u;
        standable_node_ids[i] = -1;
        standable_component_by_tile[i] = -1;
        fall_component_by_tile[i] = -1;
    }

    standable_count = 0;
    for (i = 0; i < cell_count; ++i) {
        int x = (int)(i % (size_t)map->width);
        int y = (int)(i / (size_t)map->width);

        if (!dg_is_standable_side_view_tile(map, walkable, x, y)) {
            continue;
        }

        standable[i] = 1u;
        standable_node_ids[i] = (int)standable_count;
        standable_tiles[standable_count++] = i;
    }

    if (standable_count == 0u) {
        free(walkable);
        free(standable);
        free(standable_node_ids);
        free(standable_tiles);
        free(visited_nodes);
        free(queue_nodes);
        free(node_component_ids);
        free(component_sizes);
        free(standable_component_by_tile);
        free(fall_component_by_tile);
        return DG_STATUS_OK;
    }

    max_jump_across = dg_min_int(traversal->max_jump_across, map->width - 1);
    max_jump_up = dg_min_int(traversal->max_jump_up, map->height - 1);
    max_drop_down = dg_min_int(traversal->max_drop_down, map->height - 1);

    component_count = 0;
    largest_component_id = 0;
    largest_component_size = 0;
    for (i = 0; i < standable_count; ++i) {
        size_t head;
        size_t tail;
        size_t component_size;

        if (visited_nodes[i] != 0u) {
            continue;
        }

        component_size = 0;
        head = 0;
        tail = 0;
        visited_nodes[i] = 1u;
        queue_nodes[tail++] = i;

        while (head < tail) {
            size_t node_id = queue_nodes[head++];
            size_t tile_index = standable_tiles[node_id];
            int source_x = (int)(tile_index % (size_t)map->width);
            int source_y = (int)(tile_index / (size_t)map->width);
            int min_x;
            int max_x;
            int min_y;
            int max_y;
            int target_y;

            component_size += 1;
            node_component_ids[node_id] = (int)component_count;

            min_x = dg_max_int(0, source_x - max_jump_across);
            max_x = dg_min_int(map->width - 1, source_x + max_jump_across);
            min_y = dg_max_int(0, source_y - max_jump_up);
            max_y = dg_min_int(map->height - 1, source_y + max_drop_down);

            for (target_y = min_y; target_y <= max_y; ++target_y) {
                int target_x;
                size_t row_start = (size_t)target_y * (size_t)map->width;

                for (target_x = min_x; target_x <= max_x; ++target_x) {
                    size_t target_tile_index = row_start + (size_t)target_x;
                    int target_node_id;

                    if (target_x == source_x && target_y == source_y) {
                        continue;
                    }
                    if (standable[target_tile_index] == 0u) {
                        continue;
                    }

                    target_node_id = standable_node_ids[target_tile_index];
                    if (target_node_id < 0 || visited_nodes[target_node_id] != 0u) {
                        continue;
                    }

                    if (!dg_side_view_nodes_linked(
                            map,
                            walkable,
                            source_x,
                            source_y,
                            target_x,
                            target_y,
                            traversal
                        )) {
                        continue;
                    }

                    visited_nodes[target_node_id] = 1u;
                    queue_nodes[tail++] = (size_t)target_node_id;
                }
            }
        }

        component_sizes[component_count] = component_size;
        if (component_size > largest_component_size) {
            largest_component_size = component_size;
            largest_component_id = component_count;
        }
        component_count += 1;
    }

    if (component_count <= 1u) {
        free(walkable);
        free(standable);
        free(standable_node_ids);
        free(standable_tiles);
        free(visited_nodes);
        free(queue_nodes);
        free(node_component_ids);
        free(component_sizes);
        free(standable_component_by_tile);
        free(fall_component_by_tile);
        return DG_STATUS_OK;
    }

    for (i = 0; i < standable_count; ++i) {
        size_t tile_index = standable_tiles[i];
        standable_component_by_tile[tile_index] = node_component_ids[i];
    }

    {
        int x;
        for (x = 0; x < map->width; ++x) {
            int y;
            int current_component = -1;
            for (y = map->height - 1; y >= 0; --y) {
                size_t tile_index = dg_tile_index(map, x, y);
                if (walkable[tile_index] == 0u) {
                    current_component = -1;
                    fall_component_by_tile[tile_index] = -1;
                    continue;
                }

                if (standable_component_by_tile[tile_index] >= 0) {
                    current_component = standable_component_by_tile[tile_index];
                }
                fall_component_by_tile[tile_index] = current_component;
            }
        }
    }

    for (i = 0; i < cell_count; ++i) {
        if (walkable[i] == 0u) {
            continue;
        }
        if (fall_component_by_tile[i] != (int)largest_component_id) {
            map->tiles[i] = DG_TILE_WALL;
        }
    }

    free(walkable);
    free(standable);
    free(standable_node_ids);
    free(standable_tiles);
    free(visited_nodes);
    free(queue_nodes);
    free(node_component_ids);
    free(component_sizes);
    free(standable_component_by_tile);
    free(fall_component_by_tile);
    return DG_STATUS_OK;
}

dg_status_t dg_smooth_walkable_regions(
    dg_map_t *map,
    int smoothing_passes,
    int inner_enabled,
    int outer_enabled
)
{
    size_t cell_count;
    size_t i;
    dg_tile_t *source_tiles;
    dg_tile_t *outer_source_tiles;
    dg_tile_t *inner_buffer;
    dg_tile_t *outer_buffer;
    unsigned char *protected_tiles;
    unsigned char *visited;
    size_t *queue;
    int outer_passes;
    int pass;
    int x;
    int y;

    if (smoothing_passes <= 0 || (inner_enabled == 0 && outer_enabled == 0)) {
        return DG_STATUS_OK;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    source_tiles = (dg_tile_t *)malloc(cell_count * sizeof(dg_tile_t));
    outer_source_tiles = (dg_tile_t *)malloc(cell_count * sizeof(dg_tile_t));
    inner_buffer = (dg_tile_t *)malloc(cell_count * sizeof(dg_tile_t));
    outer_buffer = (dg_tile_t *)malloc(cell_count * sizeof(dg_tile_t));
    protected_tiles = (unsigned char *)calloc(cell_count, sizeof(*protected_tiles));
    visited = (unsigned char *)malloc(cell_count * sizeof(*visited));
    queue = (size_t *)malloc(cell_count * sizeof(*queue));
    if (source_tiles == NULL || outer_source_tiles == NULL ||
        inner_buffer == NULL || outer_buffer == NULL ||
        protected_tiles == NULL || visited == NULL || queue == NULL) {
        free(source_tiles);
        free(outer_source_tiles);
        free(inner_buffer);
        free(outer_buffer);
        free(protected_tiles);
        free(visited);
        free(queue);
        return DG_STATUS_ALLOCATION_FAILED;
    }
    memcpy(source_tiles, map->tiles, cell_count * sizeof(dg_tile_t));

    if (inner_enabled != 0) {
        for (pass = 0; pass < smoothing_passes; ++pass) {
            memcpy(inner_buffer, map->tiles, cell_count * sizeof(dg_tile_t));

            for (y = 1; y < map->height - 1; ++y) {
                for (x = 1; x < map->width - 1; ++x) {
                    int leg_a_x = x;
                    int leg_a_y = y;
                    int leg_b_x = x;
                    int leg_b_y = y;
                    bool can_fill = false;
                    bool n;
                    bool e;
                    bool s;
                    bool w;
                    size_t index = dg_tile_index(map, x, y);

                    if (map->tiles[index] != DG_TILE_WALL ||
                        dg_point_in_any_room(map, x, y)) {
                        continue;
                    }

                    /*
                     * Do not open extra room entrances while smoothing corridors.
                     */
                    if ((dg_point_in_any_room(map, x, y - 1) &&
                         dg_is_walkable_tile(dg_map_get_tile(map, x, y - 1))) ||
                        (dg_point_in_any_room(map, x + 1, y) &&
                         dg_is_walkable_tile(dg_map_get_tile(map, x + 1, y))) ||
                        (dg_point_in_any_room(map, x, y + 1) &&
                         dg_is_walkable_tile(dg_map_get_tile(map, x, y + 1))) ||
                        (dg_point_in_any_room(map, x - 1, y) &&
                         dg_is_walkable_tile(dg_map_get_tile(map, x - 1, y)))) {
                        continue;
                    }

                    n = dg_is_corridor_floor(map, x, y - 1);
                    e = dg_is_corridor_floor(map, x + 1, y);
                    s = dg_is_corridor_floor(map, x, y + 1);
                    w = dg_is_corridor_floor(map, x - 1, y);

                    /*
                     * Inner smoothing:
                     * fill the concave corner wall at 90-degree bends.
                     */
                    if (n && e && !s && !w) {
                        leg_a_x = x;
                        leg_a_y = y - 1;
                        leg_b_x = x + 1;
                        leg_b_y = y;
                        can_fill = true;
                    } else if (e && s && !n && !w) {
                        leg_a_x = x + 1;
                        leg_a_y = y;
                        leg_b_x = x;
                        leg_b_y = y + 1;
                        can_fill = true;
                    } else if (s && w && !n && !e) {
                        leg_a_x = x;
                        leg_a_y = y + 1;
                        leg_b_x = x - 1;
                        leg_b_y = y;
                        can_fill = true;
                    } else if (w && n && !s && !e) {
                        leg_a_x = x - 1;
                        leg_a_y = y;
                        leg_b_x = x;
                        leg_b_y = y - 1;
                        can_fill = true;
                    }

                    if (!can_fill) {
                        continue;
                    }

                    /*
                     * Do not smooth corridor corners that terminate into rooms.
                     */
                    if (dg_corridor_touches_room(map, leg_a_x, leg_a_y) ||
                        dg_corridor_touches_room(map, leg_b_x, leg_b_y)) {
                        continue;
                    }

                    inner_buffer[index] = DG_TILE_FLOOR;
                }
            }

            memcpy(map->tiles, inner_buffer, cell_count * sizeof(dg_tile_t));
        }
    }

    if (outer_enabled != 0) {
        if (inner_enabled != 0) {
            for (i = 0; i < cell_count; ++i) {
                int x_index;
                int y_index;

                if (source_tiles[i] != DG_TILE_WALL || !dg_is_walkable_tile(map->tiles[i])) {
                    continue;
                }

                x_index = (int)(i % (size_t)map->width);
                y_index = (int)(i / (size_t)map->width);
                if (dg_point_in_any_room(map, x_index, y_index)) {
                    continue;
                }

                protected_tiles[i] = 1u;
            }
        }

        /*
         * Outer smoothing uses iterative passes up to strength.
         * Connectivity and bridge checks still gate every trim.
         */
        outer_passes = smoothing_passes;
        for (pass = 0; pass < outer_passes; ++pass) {
            memcpy(outer_source_tiles, map->tiles, cell_count * sizeof(dg_tile_t));
            memcpy(outer_buffer, map->tiles, cell_count * sizeof(dg_tile_t));

            for (y = 1; y < map->height - 1; ++y) {
                for (x = 1; x < map->width - 1; ++x) {
                    int bridge_x = x;
                    int bridge_y = y;
                    int opposite_x = x;
                    int opposite_y = y;
                    int leg_a_x = x;
                    int leg_a_y = y;
                    int leg_b_x = x;
                    int leg_b_y = y;
                    bool source_n;
                    bool source_e;
                    bool source_s;
                    bool source_w;
                    bool can_trim = false;
                    size_t index = dg_tile_index(map, x, y);

                    if (!dg_is_corridor_floor(map, x, y)) {
                        continue;
                    }
                    if (protected_tiles[index] != 0u) {
                        continue;
                    }

                    /*
                     * Determine convex-corner candidates from this pass source.
                     * `protected_tiles` prevents trimming inner-added bridge tiles
                     * when both modes are enabled in one step.
                     */
                    if (!dg_is_corridor_floor_in_tiles(map, outer_source_tiles, x, y)) {
                        continue;
                    }
                    source_n = dg_is_corridor_floor_in_tiles(map, outer_source_tiles, x, y - 1);
                    source_e = dg_is_corridor_floor_in_tiles(map, outer_source_tiles, x + 1, y);
                    source_s = dg_is_corridor_floor_in_tiles(map, outer_source_tiles, x, y + 1);
                    source_w = dg_is_corridor_floor_in_tiles(map, outer_source_tiles, x - 1, y);

                    /*
                     * Outer smoothing:
                     * identify convex corners from the source snapshot, then trim
                     * only when the matching bridge exists in the current map so
                     * connectivity is preserved.
                     */
                    if (source_n && source_e && !source_s && !source_w) {
                        bridge_x = x + 1;
                        bridge_y = y - 1;
                        opposite_x = x - 1;
                        opposite_y = y + 1;
                        leg_a_x = x;
                        leg_a_y = y - 1;
                        leg_b_x = x + 1;
                        leg_b_y = y;
                        can_trim = true;
                    } else if (source_e && source_s && !source_n && !source_w) {
                        bridge_x = x + 1;
                        bridge_y = y + 1;
                        opposite_x = x - 1;
                        opposite_y = y - 1;
                        leg_a_x = x + 1;
                        leg_a_y = y;
                        leg_b_x = x;
                        leg_b_y = y + 1;
                        can_trim = true;
                    } else if (source_s && source_w && !source_n && !source_e) {
                        bridge_x = x - 1;
                        bridge_y = y + 1;
                        opposite_x = x + 1;
                        opposite_y = y - 1;
                        leg_a_x = x;
                        leg_a_y = y + 1;
                        leg_b_x = x - 1;
                        leg_b_y = y;
                        can_trim = true;
                    } else if (source_w && source_n && !source_s && !source_e) {
                        bridge_x = x - 1;
                        bridge_y = y - 1;
                        opposite_x = x + 1;
                        opposite_y = y + 1;
                        leg_a_x = x - 1;
                        leg_a_y = y;
                        leg_b_x = x;
                        leg_b_y = y - 1;
                        can_trim = true;
                    }

                    if (!can_trim ||
                        !dg_map_in_bounds(map, bridge_x, bridge_y) ||
                        dg_point_in_any_room(map, bridge_x, bridge_y)) {
                        continue;
                    }
                    if (dg_corridor_touches_room_in_tiles(map, outer_source_tiles, x, y) ||
                        dg_corridor_touches_room_in_tiles(map, outer_source_tiles, leg_a_x, leg_a_y) ||
                        dg_corridor_touches_room_in_tiles(map, outer_source_tiles, leg_b_x, leg_b_y)) {
                        continue;
                    }

                    if (!dg_is_corridor_floor(map, bridge_x, bridge_y)) {
                        continue;
                    }
                    if (dg_is_corridor_floor(map, opposite_x, opposite_y)) {
                        continue;
                    }

                    /*
                     * Prevent cascading trims from disconnecting paths:
                     * only trim if both bend legs and bridge are still present
                     * in the current output buffer.
                     */
                    if (!dg_is_corridor_floor_in_tiles(map, outer_buffer, leg_a_x, leg_a_y) ||
                        !dg_is_corridor_floor_in_tiles(map, outer_buffer, leg_b_x, leg_b_y) ||
                        !dg_is_corridor_floor_in_tiles(map, outer_buffer, bridge_x, bridge_y) ||
                        dg_is_corridor_floor_in_tiles(map, outer_buffer, opposite_x, opposite_y)) {
                        continue;
                    }
                    if (!dg_has_corridor_path_when_blocked(
                            map,
                            outer_buffer,
                            leg_a_x,
                            leg_a_y,
                            leg_b_x,
                            leg_b_y,
                            x,
                            y,
                            visited,
                            queue
                        )) {
                        continue;
                    }

                    outer_buffer[index] = DG_TILE_WALL;
                }
            }

            memcpy(map->tiles, outer_buffer, cell_count * sizeof(dg_tile_t));
        }
    }

    free(source_tiles);
    free(outer_source_tiles);
    free(inner_buffer);
    free(outer_buffer);
    free(protected_tiles);
    free(visited);
    free(queue);
    return DG_STATUS_OK;
}
