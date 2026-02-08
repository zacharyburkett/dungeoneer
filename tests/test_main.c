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
    ASSERT_TRUE(map.metadata.room_count == 1);
    ASSERT_TRUE((map.metadata.rooms[0].flags & DG_ROOM_FLAG_SPECIAL) != 0);

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
    size_t i;

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

    special_rooms = 0;
    for (i = 0; i < map.metadata.room_count; ++i) {
        if ((map.metadata.rooms[i].flags & DG_ROOM_FLAG_SPECIAL) != 0) {
            special_rooms += 1;
        }
    }
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

    dg_map_destroy(&map);
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
