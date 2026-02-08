#include "dungeoneer/dungeoneer.h"

#include <stdio.h>
#include <stdlib.h>

#define ASSERT_TRUE(condition)                                                      \
    do {                                                                            \
        if (!(condition)) {                                                         \
            fprintf(stderr, "Assertion failed at %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                                    \
            return 1;                                                               \
        }                                                                           \
    } while (0)

#define ASSERT_STATUS(actual, expected)                                             \
    do {                                                                            \
        dg_status_t status_result = (actual);                                       \
        if (status_result != (expected)) {                                          \
            fprintf(stderr, "Unexpected status at %s:%d: got %s expected %s\n",    \
                    __FILE__, __LINE__, dg_status_string(status_result),            \
                    dg_status_string((expected)));                                  \
            return 1;                                                               \
        }                                                                           \
    } while (0)

static bool is_walkable(dg_tile_t tile)
{
    return tile == DG_TILE_FLOOR || tile == DG_TILE_DOOR;
}

static size_t count_walkable_tiles(const dg_map_t *map)
{
    size_t i;
    size_t count;
    size_t cell_count;

    count = 0;
    cell_count = (size_t)map->width * (size_t)map->height;
    for (i = 0; i < cell_count; ++i) {
        if (is_walkable(map->tiles[i])) {
            count += 1;
        }
    }

    return count;
}

static size_t count_special_rooms(const dg_map_t *map)
{
    size_t i;
    size_t count;

    count = 0;
    for (i = 0; i < map->metadata.room_count; ++i) {
        if ((map->metadata.rooms[i].flags & DG_ROOM_FLAG_SPECIAL) != 0u) {
            count += 1;
        }
    }

    return count;
}

static size_t count_rooms_with_role(const dg_map_t *map, dg_room_role_t role)
{
    size_t i;
    size_t count;

    count = 0;
    for (i = 0; i < map->metadata.room_count; ++i) {
        if (map->metadata.rooms[i].role == role) {
            count += 1;
        }
    }

    return count;
}

static int find_first_room_with_role(const dg_map_t *map, dg_room_role_t role)
{
    size_t i;

    if (map == NULL) {
        return -1;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        if (map->metadata.rooms[i].role == role) {
            return (int)i;
        }
    }

    return -1;
}

static bool compute_room_distances(const dg_map_t *map, int start_room_id, int *distances)
{
    size_t room_count;
    int *queue;
    size_t i;
    size_t head;
    size_t tail;

    if (map == NULL || distances == NULL) {
        return false;
    }

    room_count = map->metadata.room_count;
    if (room_count == 0) {
        return false;
    }

    if (start_room_id < 0 || (size_t)start_room_id >= room_count) {
        return false;
    }

    queue = (int *)malloc(room_count * sizeof(int));
    if (queue == NULL) {
        return false;
    }

    for (i = 0; i < room_count; ++i) {
        distances[i] = -1;
    }

    head = 0;
    tail = 0;
    distances[start_room_id] = 0;
    queue[tail++] = start_room_id;

    while (head < tail) {
        int room_id;
        const dg_room_adjacency_span_t *span;
        size_t neighbor_index;

        room_id = queue[head++];
        if (room_id < 0 || (size_t)room_id >= room_count) {
            free(queue);
            return false;
        }

        span = &map->metadata.room_adjacency[room_id];
        if (span->start_index + span->count > map->metadata.room_neighbor_count) {
            free(queue);
            return false;
        }

        for (neighbor_index = span->start_index;
             neighbor_index < span->start_index + span->count;
             ++neighbor_index) {
            int neighbor_room_id;

            neighbor_room_id = map->metadata.room_neighbors[neighbor_index].room_id;
            if (neighbor_room_id < 0 || (size_t)neighbor_room_id >= room_count) {
                free(queue);
                return false;
            }

            if (distances[neighbor_room_id] >= 0) {
                continue;
            }

            distances[neighbor_room_id] = distances[room_id] + 1;
            queue[tail++] = neighbor_room_id;
        }
    }

    free(queue);
    return true;
}

static bool region_is_non_walkable(const dg_map_t *map, const dg_rect_t *region)
{
    int x;
    int y;
    int x0;
    int y0;
    int x1;
    int y1;

    if (region->width <= 0 || region->height <= 0) {
        return true;
    }

    x0 = region->x < 0 ? 0 : region->x;
    y0 = region->y < 0 ? 0 : region->y;
    x1 = region->x + region->width;
    y1 = region->y + region->height;
    if (x1 > map->width) {
        x1 = map->width;
    }
    if (y1 > map->height) {
        y1 = map->height;
    }

    if (x0 >= x1 || y0 >= y1) {
        return true;
    }

    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            if (is_walkable(dg_map_get_tile(map, x, y))) {
                return false;
            }
        }
    }

    return true;
}

static bool has_outer_walls(const dg_map_t *map)
{
    int x;
    int y;

    for (x = 0; x < map->width; ++x) {
        if (dg_map_get_tile(map, x, 0) != DG_TILE_WALL) {
            return false;
        }
        if (dg_map_get_tile(map, x, map->height - 1) != DG_TILE_WALL) {
            return false;
        }
    }

    for (y = 0; y < map->height; ++y) {
        if (dg_map_get_tile(map, 0, y) != DG_TILE_WALL) {
            return false;
        }
        if (dg_map_get_tile(map, map->width - 1, y) != DG_TILE_WALL) {
            return false;
        }
    }

    return true;
}

static bool is_connected(const dg_map_t *map)
{
    size_t cell_count;
    unsigned char *visited;
    size_t *queue;
    size_t head;
    size_t tail;
    size_t i;
    size_t start;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    cell_count = (size_t)map->width * (size_t)map->height;
    visited = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
    queue = (size_t *)malloc(cell_count * sizeof(size_t));
    if (visited == NULL || queue == NULL) {
        free(visited);
        free(queue);
        return false;
    }

    start = cell_count;
    for (i = 0; i < cell_count; ++i) {
        if (is_walkable(map->tiles[i])) {
            start = i;
            break;
        }
    }

    if (start == cell_count) {
        free(visited);
        free(queue);
        return false;
    }

    head = 0;
    tail = 0;
    queue[tail++] = start;
    visited[start] = 1;

    while (head < tail) {
        size_t current = queue[head++];
        int x = (int)(current % (size_t)map->width);
        int y = (int)(current / (size_t)map->width);
        int d;

        for (d = 0; d < 4; ++d) {
            int nx = x + directions[d][0];
            int ny = y + directions[d][1];
            size_t neighbor;

            if (!dg_map_in_bounds(map, nx, ny)) {
                continue;
            }

            neighbor = (size_t)ny * (size_t)map->width + (size_t)nx;
            if (visited[neighbor] != 0 || !is_walkable(map->tiles[neighbor])) {
                continue;
            }

            visited[neighbor] = 1;
            queue[tail++] = neighbor;
        }
    }

    for (i = 0; i < cell_count; ++i) {
        if (is_walkable(map->tiles[i]) && visited[i] == 0) {
            free(visited);
            free(queue);
            return false;
        }
    }

    free(visited);
    free(queue);
    return true;
}

static bool has_reverse_neighbor(
    const dg_map_t *map,
    int from_room_id,
    int to_room_id,
    int corridor_index
)
{
    size_t i;
    const dg_room_adjacency_span_t *span;

    if (map == NULL) {
        return false;
    }

    if (from_room_id < 0 || to_room_id < 0) {
        return false;
    }

    if ((size_t)from_room_id >= map->metadata.room_adjacency_count) {
        return false;
    }

    span = &map->metadata.room_adjacency[from_room_id];
    if (span->start_index + span->count > map->metadata.room_neighbor_count) {
        return false;
    }

    for (i = span->start_index; i < span->start_index + span->count; ++i) {
        const dg_room_neighbor_t *neighbor = &map->metadata.room_neighbors[i];
        if (neighbor->room_id == to_room_id && neighbor->corridor_index == corridor_index) {
            return true;
        }
    }

    return false;
}

static int test_map_basics(void)
{
    dg_map_t map = {0};
    dg_rect_t room = {2, 2, 4, 3};

    ASSERT_STATUS(dg_map_init(&map, 16, 8, DG_TILE_WALL), DG_STATUS_OK);
    ASSERT_TRUE(dg_map_in_bounds(&map, 0, 0));
    ASSERT_TRUE(!dg_map_in_bounds(&map, -1, 0));
    ASSERT_STATUS(dg_map_set_tile(&map, 3, 3, DG_TILE_FLOOR), DG_STATUS_OK);
    ASSERT_TRUE(dg_map_get_tile(&map, 3, 3) == DG_TILE_FLOOR);
    ASSERT_STATUS(dg_map_add_room(&map, &room, DG_ROOM_FLAG_SPECIAL), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_add_corridor(&map, 0, 0, 1, 3), DG_STATUS_OK);
    ASSERT_TRUE(map.metadata.room_count == 1);
    ASSERT_TRUE(map.metadata.corridor_count == 1);
    ASSERT_TRUE(map.metadata.corridors[0].length == 3);
    ASSERT_TRUE((map.metadata.rooms[0].flags & DG_ROOM_FLAG_SPECIAL) != 0);
    ASSERT_TRUE(map.metadata.rooms[0].role == DG_ROOM_ROLE_NONE);

    dg_map_destroy(&map);
    return 0;
}

static int test_rng_reproducibility(void)
{
    int i;
    dg_rng_t a = {0};
    dg_rng_t b = {0};

    dg_rng_seed(&a, 123456u);
    dg_rng_seed(&b, 123456u);
    for (i = 0; i < 64; ++i) {
        ASSERT_TRUE(dg_rng_next_u32(&a) == dg_rng_next_u32(&b));
    }

    return 0;
}

static dg_room_flags_t mark_every_third_room(int room_index, const dg_rect_t *bounds, void *user_data)
{
    (void)bounds;
    (void)user_data;
    return ((room_index + 1) % 3 == 0) ? DG_ROOM_FLAG_SPECIAL : DG_ROOM_FLAG_NONE;
}

static int test_rooms_and_corridors_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;
    size_t special_rooms;

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_CORRIDORS, 80, 40, 42u);
    request.params.rooms.min_rooms = 8;
    request.params.rooms.max_rooms = 12;
    request.params.rooms.classify_room = mark_every_third_room;
    request.params.rooms.classify_room_user_data = NULL;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 200);
    ASSERT_TRUE(map.metadata.room_count >= 2);
    ASSERT_TRUE(is_connected(&map));
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(map.metadata.corridor_count == map.metadata.room_count - 1);
    ASSERT_TRUE(map.metadata.seed == 42u);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_ROOMS_AND_CORRIDORS);
    ASSERT_TRUE(map.metadata.generation_attempts == 1);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(map.metadata.connected_floor);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.wall_tile_count > 0);
    ASSERT_TRUE(map.metadata.corridor_total_length >= map.metadata.corridor_count);
    ASSERT_TRUE(map.metadata.leaf_room_count >= 2);
    ASSERT_TRUE(map.metadata.leaf_room_count <= map.metadata.room_count);
    ASSERT_TRUE(map.metadata.entrance_room_count == 0);
    ASSERT_TRUE(map.metadata.exit_room_count == 0);
    ASSERT_TRUE(map.metadata.boss_room_count == 0);
    ASSERT_TRUE(map.metadata.treasure_room_count == 0);
    ASSERT_TRUE(map.metadata.shop_room_count == 0);
    ASSERT_TRUE(map.metadata.entrance_exit_distance == -1);
    ASSERT_TRUE(map.metadata.room_adjacency_count == map.metadata.room_count);
    ASSERT_TRUE(map.metadata.room_neighbor_count == map.metadata.corridor_count * 2);

    {
        size_t leaf_from_adjacency;
        size_t degree_sum;
        leaf_from_adjacency = 0;
        degree_sum = 0;

        for (size_t i = 0; i < map.metadata.room_adjacency_count; ++i) {
            const dg_room_adjacency_span_t *span = &map.metadata.room_adjacency[i];
            ASSERT_TRUE(span->start_index + span->count <= map.metadata.room_neighbor_count);
            degree_sum += span->count;
            if (span->count == 1) {
                leaf_from_adjacency += 1;
            }
        }

        ASSERT_TRUE(degree_sum == map.metadata.room_neighbor_count);
        ASSERT_TRUE(leaf_from_adjacency == map.metadata.leaf_room_count);
    }

    for (size_t i = 0; i < map.metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *corridor = &map.metadata.corridors[i];
        ASSERT_TRUE(corridor->from_room_id >= 0);
        ASSERT_TRUE(corridor->to_room_id >= 0);
        ASSERT_TRUE((size_t)corridor->from_room_id < map.metadata.room_count);
        ASSERT_TRUE((size_t)corridor->to_room_id < map.metadata.room_count);
        ASSERT_TRUE(corridor->length >= 1);
        ASSERT_TRUE(has_reverse_neighbor(
            &map,
            corridor->from_room_id,
            corridor->to_room_id,
            (int)i
        ));
        ASSERT_TRUE(has_reverse_neighbor(
            &map,
            corridor->to_room_id,
            corridor->from_room_id,
            (int)i
        ));
    }

    special_rooms = count_special_rooms(&map);
    ASSERT_TRUE(map.metadata.special_room_count == special_rooms);
    if (map.metadata.room_count >= 3) {
        ASSERT_TRUE(special_rooms >= 1);
    }

    dg_map_destroy(&map);
    return 0;
}

static int test_organic_cave_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;
    size_t total;

    dg_default_generate_request(&request, DG_ALGORITHM_ORGANIC_CAVE, 70, 35, 1337u);
    request.params.organic.walk_steps = 3000;
    request.params.organic.brush_radius = 1;
    request.params.organic.smoothing_passes = 2;
    request.params.organic.target_floor_coverage = 0.25f;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
    floors = count_walkable_tiles(&map);
    total = (size_t)map.width * (size_t)map.height;
    ASSERT_TRUE(floors > total / 10);
    ASSERT_TRUE(is_connected(&map));
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(map.metadata.room_count == 0);
    ASSERT_TRUE(map.metadata.corridor_count == 0);
    ASSERT_TRUE(map.metadata.special_room_count == 0);
    ASSERT_TRUE(map.metadata.leaf_room_count == 0);
    ASSERT_TRUE(map.metadata.corridor_total_length == 0);
    ASSERT_TRUE(map.metadata.room_adjacency_count == 0);
    ASSERT_TRUE(map.metadata.room_neighbor_count == 0);
    ASSERT_TRUE(map.metadata.entrance_room_count == 0);
    ASSERT_TRUE(map.metadata.exit_room_count == 0);
    ASSERT_TRUE(map.metadata.boss_room_count == 0);
    ASSERT_TRUE(map.metadata.treasure_room_count == 0);
    ASSERT_TRUE(map.metadata.shop_room_count == 0);
    ASSERT_TRUE(map.metadata.entrance_exit_distance == -1);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_ORGANIC_CAVE);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.connected_floor);

    dg_map_destroy(&map);
    return 0;
}

static int test_room_role_constraints(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t entrance_count;
    size_t exit_count;
    size_t boss_count;
    size_t treasure_count;
    size_t shop_count;
    size_t i;

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_CORRIDORS, 90, 45, 2026u);
    request.params.rooms.min_rooms = 10;
    request.params.rooms.max_rooms = 14;
    request.constraints.required_entrance_rooms = 1;
    request.constraints.required_exit_rooms = 1;
    request.constraints.required_boss_rooms = 1;
    request.constraints.required_treasure_rooms = 1;
    request.constraints.required_shop_rooms = 1;
    request.constraints.require_boss_on_leaf = true;
    request.constraints.min_entrance_exit_distance = 3;
    request.constraints.max_generation_attempts = 5;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    entrance_count = count_rooms_with_role(&map, DG_ROOM_ROLE_ENTRANCE);
    exit_count = count_rooms_with_role(&map, DG_ROOM_ROLE_EXIT);
    boss_count = count_rooms_with_role(&map, DG_ROOM_ROLE_BOSS);
    treasure_count = count_rooms_with_role(&map, DG_ROOM_ROLE_TREASURE);
    shop_count = count_rooms_with_role(&map, DG_ROOM_ROLE_SHOP);

    ASSERT_TRUE(entrance_count >= 1);
    ASSERT_TRUE(exit_count >= 1);
    ASSERT_TRUE(boss_count >= 1);
    ASSERT_TRUE(treasure_count >= 1);
    ASSERT_TRUE(shop_count >= 1);
    ASSERT_TRUE(map.metadata.entrance_room_count == entrance_count);
    ASSERT_TRUE(map.metadata.exit_room_count == exit_count);
    ASSERT_TRUE(map.metadata.boss_room_count == boss_count);
    ASSERT_TRUE(map.metadata.treasure_room_count == treasure_count);
    ASSERT_TRUE(map.metadata.shop_room_count == shop_count);
    ASSERT_TRUE(map.metadata.entrance_exit_distance >= request.constraints.min_entrance_exit_distance);

    for (i = 0; i < map.metadata.room_count; ++i) {
        if (map.metadata.rooms[i].role == DG_ROOM_ROLE_BOSS) {
            ASSERT_TRUE(map.metadata.room_adjacency[i].count == 1);
        }
    }

    dg_map_destroy(&map);
    return 0;
}

static int test_role_weights_leaf_vs_hub(void)
{
    dg_generate_request_t request;
    dg_map_t leaf_map = {0};
    dg_map_t hub_map = {0};
    int leaf_entrance_id;
    int hub_entrance_id;

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_CORRIDORS, 100, 60, 4040u);
    request.params.rooms.min_rooms = 10;
    request.params.rooms.max_rooms = 10;
    request.constraints.required_entrance_rooms = 1;
    request.constraints.max_generation_attempts = 4;

    request.constraints.entrance_weights.distance_weight = 0;
    request.constraints.entrance_weights.degree_weight = -4;
    request.constraints.entrance_weights.leaf_bonus = 100;

    ASSERT_STATUS(dg_generate(&request, &leaf_map), DG_STATUS_OK);
    ASSERT_TRUE(leaf_map.metadata.room_count >= 3);
    leaf_entrance_id = find_first_room_with_role(&leaf_map, DG_ROOM_ROLE_ENTRANCE);
    ASSERT_TRUE(leaf_entrance_id >= 0);
    ASSERT_TRUE(leaf_map.metadata.room_adjacency[leaf_entrance_id].count == 1);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_CORRIDORS, 100, 60, 4040u);
    request.params.rooms.min_rooms = 10;
    request.params.rooms.max_rooms = 10;
    request.constraints.required_entrance_rooms = 1;
    request.constraints.max_generation_attempts = 4;
    request.constraints.entrance_weights.distance_weight = 0;
    request.constraints.entrance_weights.degree_weight = 100;
    request.constraints.entrance_weights.leaf_bonus = -25;

    ASSERT_STATUS(dg_generate(&request, &hub_map), DG_STATUS_OK);
    ASSERT_TRUE(hub_map.metadata.room_count >= 3);
    hub_entrance_id = find_first_room_with_role(&hub_map, DG_ROOM_ROLE_ENTRANCE);
    ASSERT_TRUE(hub_entrance_id >= 0);
    ASSERT_TRUE(hub_map.metadata.room_adjacency[hub_entrance_id].count >= 2);
    ASSERT_TRUE(leaf_entrance_id != hub_entrance_id);

    dg_map_destroy(&leaf_map);
    dg_map_destroy(&hub_map);
    return 0;
}

static int test_role_weights_treasure_prefers_far_distance(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    int entrance_room_id;
    int treasure_room_id;
    int *distances;
    int expected_room_id;
    int expected_distance;
    size_t i;

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_CORRIDORS, 100, 60, 5050u);
    request.params.rooms.min_rooms = 10;
    request.params.rooms.max_rooms = 10;
    request.constraints.required_entrance_rooms = 1;
    request.constraints.required_treasure_rooms = 1;
    request.constraints.max_generation_attempts = 4;

    request.constraints.entrance_weights.distance_weight = 0;
    request.constraints.entrance_weights.degree_weight = -4;
    request.constraints.entrance_weights.leaf_bonus = 100;
    request.constraints.treasure_weights.distance_weight = 100;
    request.constraints.treasure_weights.degree_weight = 0;
    request.constraints.treasure_weights.leaf_bonus = 0;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
    ASSERT_TRUE(map.metadata.room_count >= 3);

    entrance_room_id = find_first_room_with_role(&map, DG_ROOM_ROLE_ENTRANCE);
    treasure_room_id = find_first_room_with_role(&map, DG_ROOM_ROLE_TREASURE);
    ASSERT_TRUE(entrance_room_id >= 0);
    ASSERT_TRUE(treasure_room_id >= 0);
    ASSERT_TRUE(entrance_room_id != treasure_room_id);

    distances = (int *)malloc(map.metadata.room_count * sizeof(int));
    ASSERT_TRUE(distances != NULL);
    ASSERT_TRUE(compute_room_distances(&map, entrance_room_id, distances));

    expected_room_id = -1;
    expected_distance = -1;
    for (i = 0; i < map.metadata.room_count; ++i) {
        int distance;

        if ((int)i == entrance_room_id) {
            continue;
        }

        distance = distances[i];
        ASSERT_TRUE(distance >= 0);
        if (distance > expected_distance ||
            (distance == expected_distance &&
             (expected_room_id < 0 || (int)i < expected_room_id))) {
            expected_distance = distance;
            expected_room_id = (int)i;
        }
    }

    ASSERT_TRUE(expected_room_id >= 0);
    ASSERT_TRUE(treasure_room_id == expected_room_id);

    free(distances);
    dg_map_destroy(&map);
    return 0;
}

static int test_impossible_role_constraints_fail(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_CORRIDORS, 40, 24, 777u);
    request.params.rooms.min_rooms = 3;
    request.params.rooms.max_rooms = 4;
    request.constraints.required_entrance_rooms = 2;
    request.constraints.required_exit_rooms = 2;
    request.constraints.required_boss_rooms = 2;
    request.constraints.required_treasure_rooms = 2;
    request.constraints.required_shop_rooms = 2;
    request.constraints.max_generation_attempts = 3;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_GENERATION_FAILED);
    return 0;
}

static int test_forbidden_regions_constraint(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_rect_t forbidden[2];
    size_t floors;
    size_t total;
    float coverage;

    forbidden[0].x = 28;
    forbidden[0].y = 10;
    forbidden[0].width = 12;
    forbidden[0].height = 8;
    forbidden[1].x = -5;
    forbidden[1].y = -5;
    forbidden[1].width = 4;
    forbidden[1].height = 4;

    dg_default_generate_request(&request, DG_ALGORITHM_ORGANIC_CAVE, 70, 35, 77u);
    request.params.organic.walk_steps = 3000;
    request.constraints.min_floor_coverage = 0.08f;
    request.constraints.max_floor_coverage = 0.80f;
    request.constraints.forbidden_regions = forbidden;
    request.constraints.forbidden_region_count = 2;
    request.constraints.max_generation_attempts = 6;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
    ASSERT_TRUE(region_is_non_walkable(&map, &forbidden[0]));
    ASSERT_TRUE(region_is_non_walkable(&map, &forbidden[1]));

    floors = count_walkable_tiles(&map);
    total = (size_t)map.width * (size_t)map.height;
    coverage = (float)floors / (float)total;
    ASSERT_TRUE(coverage >= request.constraints.min_floor_coverage);
    ASSERT_TRUE(coverage <= request.constraints.max_floor_coverage);

    dg_map_destroy(&map);
    return 0;
}

static int test_impossible_constraints_fail(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_CORRIDORS, 60, 30, 99u);
    request.params.rooms.min_rooms = 2;
    request.params.rooms.max_rooms = 3;
    request.constraints.min_room_count = 10;
    request.constraints.max_generation_attempts = 2;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_GENERATION_FAILED);
    return 0;
}

static int test_invalid_constraints_fail_fast(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_CORRIDORS, 60, 30, 10u);
    request.constraints.min_floor_coverage = 0.8f;
    request.constraints.max_floor_coverage = 0.2f;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_CORRIDORS, 60, 30, 11u);
    request.constraints.min_entrance_exit_distance = 4;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);
    return 0;
}

int main(void)
{
    size_t i;
    int failures;

    struct test_case {
        const char *name;
        int (*run)(void);
    };

    const struct test_case tests[] = {
        {"map_basics", test_map_basics},
        {"rng_reproducibility", test_rng_reproducibility},
        {"rooms_and_corridors_generation", test_rooms_and_corridors_generation},
        {"organic_cave_generation", test_organic_cave_generation},
        {"room_role_constraints", test_room_role_constraints},
        {"role_weights_leaf_vs_hub", test_role_weights_leaf_vs_hub},
        {"role_weights_treasure_prefers_far_distance", test_role_weights_treasure_prefers_far_distance},
        {"forbidden_regions_constraint", test_forbidden_regions_constraint},
        {"impossible_constraints_fail", test_impossible_constraints_fail},
        {"impossible_role_constraints_fail", test_impossible_role_constraints_fail},
        {"invalid_constraints_fail_fast", test_invalid_constraints_fail_fast},
    };

    failures = 0;
    for (i = 0; i < (sizeof(tests) / sizeof(tests[0])); ++i) {
        int result = tests[i].run();
        if (result == 0) {
            fprintf(stdout, "[PASS] %s\n", tests[i].name);
        } else {
            fprintf(stderr, "[FAIL] %s\n", tests[i].name);
            failures += 1;
        }
    }

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    fprintf(stdout, "%zu test(s) passed\n", sizeof(tests) / sizeof(tests[0]));
    return 0;
}
