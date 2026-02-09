#include "dungeoneer/dungeoneer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool maps_have_same_tiles(const dg_map_t *a, const dg_map_t *b)
{
    size_t cell_count;

    if (a == NULL || b == NULL || a->tiles == NULL || b->tiles == NULL) {
        return false;
    }

    if (a->width != b->width || a->height != b->height) {
        return false;
    }

    cell_count = (size_t)a->width * (size_t)a->height;
    return memcmp(a->tiles, b->tiles, cell_count * sizeof(dg_tile_t)) == 0;
}

static bool maps_have_same_metadata(const dg_map_t *a, const dg_map_t *b)
{
    size_t i;

    if (a == NULL || b == NULL) {
        return false;
    }

    if (a->metadata.seed != b->metadata.seed ||
        a->metadata.algorithm_id != b->metadata.algorithm_id ||
        a->metadata.generation_class != b->metadata.generation_class ||
        a->metadata.room_count != b->metadata.room_count ||
        a->metadata.corridor_count != b->metadata.corridor_count ||
        a->metadata.room_adjacency_count != b->metadata.room_adjacency_count ||
        a->metadata.room_neighbor_count != b->metadata.room_neighbor_count ||
        a->metadata.walkable_tile_count != b->metadata.walkable_tile_count ||
        a->metadata.wall_tile_count != b->metadata.wall_tile_count ||
        a->metadata.special_room_count != b->metadata.special_room_count ||
        a->metadata.entrance_room_count != b->metadata.entrance_room_count ||
        a->metadata.exit_room_count != b->metadata.exit_room_count ||
        a->metadata.boss_room_count != b->metadata.boss_room_count ||
        a->metadata.treasure_room_count != b->metadata.treasure_room_count ||
        a->metadata.shop_room_count != b->metadata.shop_room_count ||
        a->metadata.leaf_room_count != b->metadata.leaf_room_count ||
        a->metadata.corridor_total_length != b->metadata.corridor_total_length ||
        a->metadata.entrance_exit_distance != b->metadata.entrance_exit_distance ||
        a->metadata.connected_component_count != b->metadata.connected_component_count ||
        a->metadata.largest_component_size != b->metadata.largest_component_size ||
        a->metadata.connected_floor != b->metadata.connected_floor ||
        a->metadata.generation_attempts != b->metadata.generation_attempts) {
        return false;
    }

    if ((a->metadata.room_count > 0 && (a->metadata.rooms == NULL || b->metadata.rooms == NULL)) ||
        (a->metadata.corridor_count > 0 &&
         (a->metadata.corridors == NULL || b->metadata.corridors == NULL)) ||
        (a->metadata.room_adjacency_count > 0 &&
         (a->metadata.room_adjacency == NULL || b->metadata.room_adjacency == NULL)) ||
        (a->metadata.room_neighbor_count > 0 &&
         (a->metadata.room_neighbors == NULL || b->metadata.room_neighbors == NULL))) {
        return false;
    }

    for (i = 0; i < a->metadata.room_count; ++i) {
        const dg_room_metadata_t *ra = &a->metadata.rooms[i];
        const dg_room_metadata_t *rb = &b->metadata.rooms[i];
        if (ra->id != rb->id ||
            ra->bounds.x != rb->bounds.x ||
            ra->bounds.y != rb->bounds.y ||
            ra->bounds.width != rb->bounds.width ||
            ra->bounds.height != rb->bounds.height ||
            ra->flags != rb->flags ||
            ra->role != rb->role) {
            return false;
        }
    }

    for (i = 0; i < a->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *ca = &a->metadata.corridors[i];
        const dg_corridor_metadata_t *cb = &b->metadata.corridors[i];
        if (ca->from_room_id != cb->from_room_id ||
            ca->to_room_id != cb->to_room_id ||
            ca->width != cb->width ||
            ca->length != cb->length) {
            return false;
        }
    }

    for (i = 0; i < a->metadata.room_adjacency_count; ++i) {
        const dg_room_adjacency_span_t *sa = &a->metadata.room_adjacency[i];
        const dg_room_adjacency_span_t *sb = &b->metadata.room_adjacency[i];
        if (sa->start_index != sb->start_index || sa->count != sb->count) {
            return false;
        }
    }

    for (i = 0; i < a->metadata.room_neighbor_count; ++i) {
        const dg_room_neighbor_t *na = &a->metadata.room_neighbors[i];
        const dg_room_neighbor_t *nb = &b->metadata.room_neighbors[i];
        if (na->room_id != nb->room_id || na->corridor_index != nb->corridor_index) {
            return false;
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

static bool rects_overlap_with_padding(const dg_rect_t *a, const dg_rect_t *b, int padding)
{
    long long a_left;
    long long a_top;
    long long a_right;
    long long a_bottom;
    long long b_left;
    long long b_top;
    long long b_right;
    long long b_bottom;

    a_left = (long long)a->x - (long long)padding;
    a_top = (long long)a->y - (long long)padding;
    a_right = (long long)a->x + (long long)a->width + (long long)padding;
    a_bottom = (long long)a->y + (long long)a->height + (long long)padding;

    b_left = (long long)b->x;
    b_top = (long long)b->y;
    b_right = (long long)b->x + (long long)b->width;
    b_bottom = (long long)b->y + (long long)b->height;

    if (a_right <= b_left || b_right <= a_left) {
        return false;
    }
    if (a_bottom <= b_top || b_bottom <= a_top) {
        return false;
    }

    return true;
}

static bool rooms_have_min_wall_separation(const dg_map_t *map)
{
    size_t i;
    size_t j;

    for (i = 0; i < map->metadata.room_count; ++i) {
        for (j = i + 1; j < map->metadata.room_count; ++j) {
            if (rects_overlap_with_padding(&map->metadata.rooms[i].bounds, &map->metadata.rooms[j].bounds, 1)) {
                return false;
            }
        }
    }

    return true;
}

static bool corridors_have_unique_room_pairs(const dg_map_t *map)
{
    size_t i;
    size_t j;

    for (i = 0; i < map->metadata.corridor_count; ++i) {
        int ai = map->metadata.corridors[i].from_room_id;
        int bi = map->metadata.corridors[i].to_room_id;
        int min_i = (ai < bi) ? ai : bi;
        int max_i = (ai < bi) ? bi : ai;

        for (j = i + 1; j < map->metadata.corridor_count; ++j) {
            int aj = map->metadata.corridors[j].from_room_id;
            int bj = map->metadata.corridors[j].to_room_id;
            int min_j = (aj < bj) ? aj : bj;
            int max_j = (aj < bj) ? bj : aj;

            if (min_i == min_j && max_i == max_j) {
                return false;
            }
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
    ASSERT_STATUS(dg_map_add_room(&map, &room, DG_ROOM_FLAG_NONE), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_add_corridor(&map, 0, 0, 1, 3), DG_STATUS_OK);
    ASSERT_TRUE(map.metadata.room_count == 1);
    ASSERT_TRUE(map.metadata.corridor_count == 1);

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

static int test_bsp_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;
    size_t i;

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 96, 54, 42u);
    request.params.bsp.min_rooms = 10;
    request.params.bsp.max_rooms = 10;
    request.params.bsp.room_min_size = 4;
    request.params.bsp.room_max_size = 11;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_BSP_TREE);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE);
    ASSERT_TRUE(map.metadata.room_count == 10);
    ASSERT_TRUE(map.metadata.corridor_count == map.metadata.room_count - 1);
    ASSERT_TRUE(map.metadata.connected_floor);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.generation_attempts == 1);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(is_connected(&map));

    for (i = 0; i < map.metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map.metadata.rooms[i];
        ASSERT_TRUE(room->bounds.width >= request.params.bsp.room_min_size);
        ASSERT_TRUE(room->bounds.width <= request.params.bsp.room_max_size);
        ASSERT_TRUE(room->bounds.height >= request.params.bsp.room_min_size);
        ASSERT_TRUE(room->bounds.height <= request.params.bsp.room_max_size);
        ASSERT_TRUE(room->role == DG_ROOM_ROLE_NONE);
        ASSERT_TRUE(room->flags == DG_ROOM_FLAG_NONE);
    }

    dg_map_destroy(&map);
    return 0;
}

static int test_bsp_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 1337u);
    request.params.bsp.min_rooms = 9;
    request.params.bsp.max_rooms = 13;
    request.params.bsp.room_min_size = 4;
    request.params.bsp.room_max_size = 10;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_drunkards_walk_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;

    dg_default_generate_request(&request, DG_ALGORITHM_DRUNKARDS_WALK, 96, 54, 4242u);
    request.params.drunkards_walk.wiggle_percent = 70;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_DRUNKARDS_WALK);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_CAVE_LIKE);
    ASSERT_TRUE(map.metadata.room_count == 0);
    ASSERT_TRUE(map.metadata.corridor_count == 0);
    ASSERT_TRUE(map.metadata.connected_floor);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(is_connected(&map));

    dg_map_destroy(&map);
    return 0;
}

static int test_drunkards_walk_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_DRUNKARDS_WALK, 88, 48, 7070u);
    request.params.drunkards_walk.wiggle_percent = 45;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_drunkards_wiggle_affects_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 500u; seed < 560u; ++seed) {
        dg_generate_request_t request_low;
        dg_generate_request_t request_high;
        dg_map_t low = {0};
        dg_map_t high = {0};

        dg_default_generate_request(&request_low, DG_ALGORITHM_DRUNKARDS_WALK, 80, 44, seed);
        request_low.params.drunkards_walk.wiggle_percent = 5;

        dg_default_generate_request(&request_high, DG_ALGORITHM_DRUNKARDS_WALK, 80, 44, seed);
        request_high.params.drunkards_walk.wiggle_percent = 95;

        ASSERT_STATUS(dg_generate(&request_low, &low), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&request_high, &high), DG_STATUS_OK);

        if (!maps_have_same_tiles(&low, &high)) {
            found_difference = true;
        }

        dg_map_destroy(&low);
        dg_map_destroy(&high);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_rooms_and_mazes_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;
    size_t i;

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 96, 54, 2026u);
    request.params.rooms_and_mazes.min_rooms = 10;
    request.params.rooms_and_mazes.max_rooms = 14;
    request.params.rooms_and_mazes.room_min_size = 4;
    request.params.rooms_and_mazes.room_max_size = 10;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_ROOMS_AND_MAZES);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE);
    ASSERT_TRUE(map.metadata.room_count >= (size_t)request.params.rooms_and_mazes.min_rooms);
    ASSERT_TRUE(map.metadata.room_count <= (size_t)request.params.rooms_and_mazes.max_rooms);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(rooms_have_min_wall_separation(&map));
    ASSERT_TRUE(corridors_have_unique_room_pairs(&map));

    for (i = 0; i < map.metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map.metadata.rooms[i];
        ASSERT_TRUE(room->bounds.width >= request.params.rooms_and_mazes.room_min_size);
        ASSERT_TRUE(room->bounds.width <= request.params.rooms_and_mazes.room_max_size);
        ASSERT_TRUE(room->bounds.height >= request.params.rooms_and_mazes.room_min_size);
        ASSERT_TRUE(room->bounds.height <= request.params.rooms_and_mazes.room_max_size);
        ASSERT_TRUE(room->role == DG_ROOM_ROLE_NONE);
        ASSERT_TRUE(room->flags == DG_ROOM_FLAG_NONE);
    }

    dg_map_destroy(&map);
    return 0;
}

static int test_rooms_and_mazes_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, 9151u);
    request.params.rooms_and_mazes.min_rooms = 8;
    request.params.rooms_and_mazes.max_rooms = 12;
    request.params.rooms_and_mazes.room_min_size = 4;
    request.params.rooms_and_mazes.room_max_size = 9;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_map_serialization_roundtrip(void)
{
    const char *path;
    dg_generate_request_t request;
    dg_map_t original = {0};
    dg_map_t loaded = {0};

    path = "dungeoneer_test_roundtrip.dgmap";

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 6060u);
    request.params.bsp.min_rooms = 9;
    request.params.bsp.max_rooms = 12;

    ASSERT_STATUS(dg_generate(&request, &original), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_save_file(&original, path), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_load_file(path, &loaded), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&original, &loaded));
    ASSERT_TRUE(maps_have_same_metadata(&original, &loaded));

    dg_map_destroy(&original);
    dg_map_destroy(&loaded);
    (void)remove(path);
    return 0;
}

static int test_map_load_rejects_invalid_magic(void)
{
    const char *path;
    FILE *file;
    dg_map_t loaded = {0};
    const char bad_data[] = "NOT_DGMP";

    path = "dungeoneer_test_bad_magic.dgmap";
    file = fopen(path, "wb");
    ASSERT_TRUE(file != NULL);
    ASSERT_TRUE(fwrite(bad_data, 1, sizeof(bad_data), file) == sizeof(bad_data));
    ASSERT_TRUE(fclose(file) == 0);

    ASSERT_STATUS(dg_map_load_file(path, &loaded), DG_STATUS_UNSUPPORTED_FORMAT);
    (void)remove(path);
    return 0;
}

static int test_invalid_generate_request(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 7, 48, 1u);
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.params.bsp.min_rooms = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.params.bsp.min_rooms = 10;
    request.params.bsp.max_rooms = 9;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.params.bsp.room_min_size = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.params.bsp.room_min_size = 8;
    request.params.bsp.room_max_size = 4;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_DRUNKARDS_WALK, 80, 48, 1u);
    request.params.drunkards_walk.wiggle_percent = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_DRUNKARDS_WALK, 80, 48, 1u);
    request.params.drunkards_walk.wiggle_percent = 101;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.min_rooms = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.min_rooms = 10;
    request.params.rooms_and_mazes.max_rooms = 9;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.room_min_size = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.room_min_size = 9;
    request.params.rooms_and_mazes.room_max_size = 8;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    return 0;
}

static int test_bsp_generation_failure_for_tiny_map(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 16, 16, 99u);
    request.params.bsp.min_rooms = 6;
    request.params.bsp.max_rooms = 8;
    request.params.bsp.room_min_size = 10;
    request.params.bsp.room_max_size = 12;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_GENERATION_FAILED);
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
        {"bsp_generation", test_bsp_generation},
        {"bsp_determinism", test_bsp_determinism},
        {"drunkards_walk_generation", test_drunkards_walk_generation},
        {"drunkards_walk_determinism", test_drunkards_walk_determinism},
        {"drunkards_wiggle_affects_layout", test_drunkards_wiggle_affects_layout},
        {"rooms_and_mazes_generation", test_rooms_and_mazes_generation},
        {"rooms_and_mazes_determinism", test_rooms_and_mazes_determinism},
        {"map_serialization_roundtrip", test_map_serialization_roundtrip},
        {"map_load_rejects_invalid_magic", test_map_load_rejects_invalid_magic},
        {"invalid_generate_request", test_invalid_generate_request},
        {"bsp_generation_failure_for_tiny_map", test_bsp_generation_failure_for_tiny_map},
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
