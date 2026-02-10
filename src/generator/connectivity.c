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

dg_status_t dg_analyze_connectivity(const dg_map_t *map, dg_connectivity_stats_t *out_stats)
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

dg_status_t dg_smooth_walkable_regions(dg_map_t *map, int smoothing_passes)
{
    size_t cell_count;
    dg_tile_t *buffer;
    int pass;
    int x;
    int y;

    if (smoothing_passes <= 0) {
        return DG_STATUS_OK;
    }

    cell_count = (size_t)map->width * (size_t)map->height;
    buffer = (dg_tile_t *)malloc(cell_count * sizeof(dg_tile_t));
    if (buffer == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (pass = 0; pass < smoothing_passes; ++pass) {
        memcpy(buffer, map->tiles, cell_count * sizeof(dg_tile_t));

        for (y = 1; y < map->height - 1; ++y) {
            for (x = 1; x < map->width - 1; ++x) {
                bool n;
                bool e;
                bool s;
                bool w;
                size_t index = dg_tile_index(map, x, y);

                if (map->tiles[index] != DG_TILE_WALL) {
                    continue;
                }
                if (dg_point_in_any_room(map, x, y)) {
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

                n = dg_is_walkable_tile(dg_map_get_tile(map, x, y - 1)) &&
                    !dg_point_in_any_room(map, x, y - 1);
                e = dg_is_walkable_tile(dg_map_get_tile(map, x + 1, y)) &&
                    !dg_point_in_any_room(map, x + 1, y);
                s = dg_is_walkable_tile(dg_map_get_tile(map, x, y + 1)) &&
                    !dg_point_in_any_room(map, x, y + 1);
                w = dg_is_walkable_tile(dg_map_get_tile(map, x - 1, y)) &&
                    !dg_point_in_any_room(map, x - 1, y);

                /*
                 * Corridor path smoothing:
                 * round inner 90-degree bends by filling the corner wall.
                 * This is additive only, so it will not break existing paths.
                 */
                if ((n && e && !s && !w) ||
                    (e && s && !n && !w) ||
                    (s && w && !n && !e) ||
                    (w && n && !s && !e)) {
                    buffer[index] = DG_TILE_FLOOR;
                }
            }
        }

        memcpy(map->tiles, buffer, cell_count * sizeof(dg_tile_t));
    }

    free(buffer);
    return DG_STATUS_OK;
}
