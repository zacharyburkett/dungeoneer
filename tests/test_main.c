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

static bool maps_have_same_generation_request_snapshot(const dg_map_t *a, const dg_map_t *b)
{
    const dg_generation_request_snapshot_t *sa;
    const dg_generation_request_snapshot_t *sb;
    size_t i;

    if (a == NULL || b == NULL) {
        return false;
    }

    sa = &a->metadata.generation_request;
    sb = &b->metadata.generation_request;
    if (sa->present != sb->present) {
        return false;
    }
    if (sa->present == 0) {
        return true;
    }

    if (sa->width != sb->width ||
        sa->height != sb->height ||
        sa->seed != sb->seed ||
        sa->algorithm_id != sb->algorithm_id ||
        sa->process.method_count != sb->process.method_count ||
        sa->room_types.definition_count != sb->room_types.definition_count ||
        sa->room_types.policy.strict_mode != sb->room_types.policy.strict_mode ||
        sa->room_types.policy.allow_untyped_rooms != sb->room_types.policy.allow_untyped_rooms ||
        sa->room_types.policy.default_type_id != sb->room_types.policy.default_type_id) {
        return false;
    }

    switch ((dg_algorithm_t)sa->algorithm_id) {
    case DG_ALGORITHM_BSP_TREE:
        if (sa->params.bsp.min_rooms != sb->params.bsp.min_rooms ||
            sa->params.bsp.max_rooms != sb->params.bsp.max_rooms ||
            sa->params.bsp.room_min_size != sb->params.bsp.room_min_size ||
            sa->params.bsp.room_max_size != sb->params.bsp.room_max_size) {
            return false;
        }
        break;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        if (sa->params.drunkards_walk.wiggle_percent !=
            sb->params.drunkards_walk.wiggle_percent) {
            return false;
        }
        break;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        if (sa->params.rooms_and_mazes.min_rooms != sb->params.rooms_and_mazes.min_rooms ||
            sa->params.rooms_and_mazes.max_rooms != sb->params.rooms_and_mazes.max_rooms ||
            sa->params.rooms_and_mazes.room_min_size != sb->params.rooms_and_mazes.room_min_size ||
            sa->params.rooms_and_mazes.room_max_size != sb->params.rooms_and_mazes.room_max_size ||
            sa->params.rooms_and_mazes.maze_wiggle_percent !=
                sb->params.rooms_and_mazes.maze_wiggle_percent ||
            sa->params.rooms_and_mazes.min_room_connections !=
                sb->params.rooms_and_mazes.min_room_connections ||
            sa->params.rooms_and_mazes.max_room_connections !=
                sb->params.rooms_and_mazes.max_room_connections ||
            sa->params.rooms_and_mazes.ensure_full_connectivity !=
                sb->params.rooms_and_mazes.ensure_full_connectivity ||
            sa->params.rooms_and_mazes.dead_end_prune_steps !=
                sb->params.rooms_and_mazes.dead_end_prune_steps) {
            return false;
        }
        break;
    default:
        return false;
    }

    if ((sa->process.method_count > 0 &&
         (sa->process.methods == NULL || sb->process.methods == NULL))) {
        return false;
    }

    for (i = 0; i < sa->process.method_count; ++i) {
        const dg_snapshot_process_method_t *ma = &sa->process.methods[i];
        const dg_snapshot_process_method_t *mb = &sb->process.methods[i];

        if (ma->type != mb->type) {
            return false;
        }

        switch ((dg_process_method_type_t)ma->type) {
        case DG_PROCESS_METHOD_SCALE:
            if (ma->params.scale.factor != mb->params.scale.factor) {
                return false;
            }
            break;
        case DG_PROCESS_METHOD_ROOM_SHAPE:
            if (ma->params.room_shape.mode != mb->params.room_shape.mode ||
                ma->params.room_shape.organicity != mb->params.room_shape.organicity) {
                return false;
            }
            break;
        case DG_PROCESS_METHOD_PATH_SMOOTH:
            if (ma->params.path_smooth.strength != mb->params.path_smooth.strength) {
                return false;
            }
            break;
        default:
            return false;
        }
    }

    if ((sa->room_types.definition_count > 0 &&
         (sa->room_types.definitions == NULL || sb->room_types.definitions == NULL))) {
        return false;
    }

    for (i = 0; i < sa->room_types.definition_count; ++i) {
        const dg_snapshot_room_type_definition_t *da = &sa->room_types.definitions[i];
        const dg_snapshot_room_type_definition_t *db = &sb->room_types.definitions[i];

        if (da->type_id != db->type_id ||
            da->enabled != db->enabled ||
            da->min_count != db->min_count ||
            da->max_count != db->max_count ||
            da->target_count != db->target_count ||
            da->constraints.area_min != db->constraints.area_min ||
            da->constraints.area_max != db->constraints.area_max ||
            da->constraints.degree_min != db->constraints.degree_min ||
            da->constraints.degree_max != db->constraints.degree_max ||
            da->constraints.border_distance_min != db->constraints.border_distance_min ||
            da->constraints.border_distance_max != db->constraints.border_distance_max ||
            da->constraints.graph_depth_min != db->constraints.graph_depth_min ||
            da->constraints.graph_depth_max != db->constraints.graph_depth_max ||
            da->preferences.weight != db->preferences.weight ||
            da->preferences.larger_room_bias != db->preferences.larger_room_bias ||
            da->preferences.higher_degree_bias != db->preferences.higher_degree_bias ||
            da->preferences.border_distance_bias != db->preferences.border_distance_bias) {
            return false;
        }
    }

    return true;
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
            ra->role != rb->role ||
            ra->type_id != rb->type_id) {
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

    return maps_have_same_generation_request_snapshot(a, b);
}

static size_t count_rooms_with_type_id(const dg_map_t *map, uint32_t type_id)
{
    size_t i;
    size_t count;

    if (map == NULL) {
        return 0;
    }

    count = 0;
    for (i = 0; i < map->metadata.room_count; ++i) {
        if (map->metadata.rooms[i].type_id == type_id) {
            count += 1;
        }
    }

    return count;
}

static size_t count_rooms_with_assigned_type(const dg_map_t *map)
{
    size_t i;
    size_t count;

    if (map == NULL) {
        return 0;
    }

    count = 0;
    for (i = 0; i < map->metadata.room_count; ++i) {
        if (map->metadata.rooms[i].type_id != DG_ROOM_TYPE_UNASSIGNED) {
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

static bool point_is_inside_any_room(const dg_map_t *map, int x, int y)
{
    size_t i;

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_rect_t *room = &map->metadata.rooms[i].bounds;
        if (
            x >= room->x &&
            y >= room->y &&
            x < room->x + room->width &&
            y < room->y + room->height
        ) {
            return true;
        }
    }

    return false;
}

static size_t count_non_room_dead_ends(const dg_map_t *map)
{
    size_t count;
    int x;
    int y;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    count = 0;
    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            int neighbors;
            int d;
            dg_tile_t tile;

            if (point_is_inside_any_room(map, x, y)) {
                continue;
            }

            tile = dg_map_get_tile(map, x, y);
            if (!is_walkable(tile)) {
                continue;
            }

            neighbors = 0;
            for (d = 0; d < 4; ++d) {
                int nx = x + directions[d][0];
                int ny = y + directions[d][1];
                if (is_walkable(dg_map_get_tile(map, nx, ny))) {
                    neighbors += 1;
                }
            }

            if (neighbors <= 1) {
                count += 1;
            }
        }
    }

    return count;
}

static size_t count_non_room_isolated_walkable_tiles(const dg_map_t *map)
{
    size_t count;
    int x;
    int y;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    count = 0;
    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            int neighbors;
            int d;
            dg_tile_t tile;

            if (point_is_inside_any_room(map, x, y)) {
                continue;
            }

            tile = dg_map_get_tile(map, x, y);
            if (!is_walkable(tile)) {
                continue;
            }

            neighbors = 0;
            for (d = 0; d < 4; ++d) {
                int nx = x + directions[d][0];
                int ny = y + directions[d][1];
                if (is_walkable(dg_map_get_tile(map, nx, ny))) {
                    neighbors += 1;
                }
            }

            if (neighbors == 0) {
                count += 1;
            }
        }
    }

    return count;
}

static size_t count_non_room_diagonal_touches_to_room_tiles(const dg_map_t *map)
{
    size_t count;
    int x;
    int y;
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

    count = 0;
    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            int d;
            dg_tile_t tile;
            bool orthogonally_adjacent_to_room;

            if (point_is_inside_any_room(map, x, y)) {
                continue;
            }

            tile = dg_map_get_tile(map, x, y);
            if (!is_walkable(tile)) {
                continue;
            }

            orthogonally_adjacent_to_room = false;
            for (d = 0; d < 4; ++d) {
                int nx = x + cardinals[d][0];
                int ny = y + cardinals[d][1];
                if (point_is_inside_any_room(map, nx, ny) && is_walkable(dg_map_get_tile(map, nx, ny))) {
                    orthogonally_adjacent_to_room = true;
                    break;
                }
            }
            if (orthogonally_adjacent_to_room) {
                continue;
            }

            for (d = 0; d < 4; ++d) {
                int nx = x + diagonals[d][0];
                int ny = y + diagonals[d][1];
                if (!point_is_inside_any_room(map, nx, ny)) {
                    continue;
                }
                if (is_walkable(dg_map_get_tile(map, nx, ny))) {
                    count += 1;
                    break;
                }
            }
        }
    }

    return count;
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
        ASSERT_TRUE(room->type_id == DG_ROOM_TYPE_UNASSIGNED);
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
    ASSERT_TRUE(count_non_room_diagonal_touches_to_room_tiles(&map) == 0);
    ASSERT_TRUE(map.metadata.connected_floor);

    for (i = 0; i < map.metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map.metadata.rooms[i];
        ASSERT_TRUE(room->bounds.width >= request.params.rooms_and_mazes.room_min_size);
        ASSERT_TRUE(room->bounds.width <= request.params.rooms_and_mazes.room_max_size);
        ASSERT_TRUE(room->bounds.height >= request.params.rooms_and_mazes.room_min_size);
        ASSERT_TRUE(room->bounds.height <= request.params.rooms_and_mazes.room_max_size);
        ASSERT_TRUE(room->role == DG_ROOM_ROLE_NONE);
        ASSERT_TRUE(room->type_id == DG_ROOM_TYPE_UNASSIGNED);
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

static int test_rooms_and_mazes_pruning_control(void)
{
    uint64_t seed;
    bool found_seed_with_pruning_effect;

    found_seed_with_pruning_effect = false;
    for (seed = 1000u; seed < 1200u; ++seed) {
        dg_generate_request_t request_off;
        dg_generate_request_t request_full;
        dg_map_t map_off = {0};
        dg_map_t map_full = {0};
        size_t dead_ends_off;
        size_t dead_ends_full;

        dg_default_generate_request(&request_off, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        request_off.params.rooms_and_mazes.min_rooms = 9;
        request_off.params.rooms_and_mazes.max_rooms = 14;
        request_off.params.rooms_and_mazes.room_min_size = 4;
        request_off.params.rooms_and_mazes.room_max_size = 10;
        request_off.params.rooms_and_mazes.dead_end_prune_steps = 0;

        request_full = request_off;
        request_full.params.rooms_and_mazes.dead_end_prune_steps = -1;

        ASSERT_STATUS(dg_generate(&request_off, &map_off), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&request_full, &map_full), DG_STATUS_OK);

        dead_ends_off = count_non_room_dead_ends(&map_off);
        dead_ends_full = count_non_room_dead_ends(&map_full);

        if (dead_ends_off > 0 && dead_ends_full == 0) {
            found_seed_with_pruning_effect = true;
            ASSERT_TRUE(!maps_have_same_tiles(&map_off, &map_full));
        }

        dg_map_destroy(&map_off);
        dg_map_destroy(&map_full);

        if (found_seed_with_pruning_effect) {
            break;
        }
    }

    ASSERT_TRUE(found_seed_with_pruning_effect);
    return 0;
}

static int test_rooms_and_mazes_wiggle_affects_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 3200u; seed < 3280u; ++seed) {
        dg_generate_request_t request_low;
        dg_generate_request_t request_high;
        dg_map_t low = {0};
        dg_map_t high = {0};

        dg_default_generate_request(&request_low, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        request_low.params.rooms_and_mazes.min_rooms = 9;
        request_low.params.rooms_and_mazes.max_rooms = 14;
        request_low.params.rooms_and_mazes.room_min_size = 4;
        request_low.params.rooms_and_mazes.room_max_size = 10;
        request_low.params.rooms_and_mazes.dead_end_prune_steps = 0;
        request_low.params.rooms_and_mazes.ensure_full_connectivity = 0;
        request_low.params.rooms_and_mazes.maze_wiggle_percent = 0;

        request_high = request_low;
        request_high.params.rooms_and_mazes.maze_wiggle_percent = 100;

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

static int test_rooms_and_mazes_unpruned_has_no_isolated_seed_tiles(void)
{
    uint64_t seed;

    for (seed = 2000u; seed < 2120u; ++seed) {
        dg_generate_request_t request;
        dg_map_t map = {0};
        size_t isolated_count;

        dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        request.params.rooms_and_mazes.min_rooms = 9;
        request.params.rooms_and_mazes.max_rooms = 14;
        request.params.rooms_and_mazes.room_min_size = 4;
        request.params.rooms_and_mazes.room_max_size = 10;
        request.params.rooms_and_mazes.dead_end_prune_steps = 0;

        ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
        isolated_count = count_non_room_isolated_walkable_tiles(&map);
        dg_map_destroy(&map);

        ASSERT_TRUE(isolated_count == 0);
    }

    return 0;
}

static int test_post_process_scaling(void)
{
    dg_generate_request_t base_request;
    dg_generate_request_t scaled_request;
    dg_map_t base_map = {0};
    dg_map_t scaled_map = {0};
    dg_process_method_t scaled_methods[1];
    int factor;

    dg_default_generate_request(&base_request, DG_ALGORITHM_BSP_TREE, 72, 42, 123456u);
    base_request.params.bsp.min_rooms = 8;
    base_request.params.bsp.max_rooms = 10;
    base_request.params.bsp.room_min_size = 4;
    base_request.params.bsp.room_max_size = 9;

    scaled_request = base_request;
    factor = 3;
    dg_default_process_method(&scaled_methods[0], DG_PROCESS_METHOD_SCALE);
    scaled_methods[0].params.scale.factor = factor;
    scaled_request.process.methods = scaled_methods;
    scaled_request.process.method_count = 1;

    ASSERT_STATUS(dg_generate(&base_request, &base_map), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&scaled_request, &scaled_map), DG_STATUS_OK);

    ASSERT_TRUE(scaled_map.width == base_map.width * factor);
    ASSERT_TRUE(scaled_map.height == base_map.height * factor);
    ASSERT_TRUE(scaled_map.metadata.room_count == base_map.metadata.room_count);
    ASSERT_TRUE(scaled_map.metadata.corridor_count == base_map.metadata.corridor_count);
    ASSERT_TRUE(scaled_map.metadata.generation_request.process.method_count == 1);
    ASSERT_TRUE(
        scaled_map.metadata.generation_request.process.methods[0].type ==
        (int)DG_PROCESS_METHOD_SCALE
    );
    ASSERT_TRUE(
        scaled_map.metadata.generation_request.process.methods[0].params.scale.factor == factor
    );
    ASSERT_TRUE(scaled_map.metadata.rooms[0].bounds.width == base_map.metadata.rooms[0].bounds.width * factor);
    ASSERT_TRUE(scaled_map.metadata.rooms[0].bounds.height == base_map.metadata.rooms[0].bounds.height * factor);

    dg_map_destroy(&base_map);
    dg_map_destroy(&scaled_map);
    return 0;
}

static int test_post_process_room_shape_changes_layout(void)
{
    dg_generate_request_t rectangular_request;
    dg_generate_request_t organic_request;
    dg_map_t rectangular_map = {0};
    dg_map_t organic_map = {0};
    dg_process_method_t organic_methods[1];

    dg_default_generate_request(&rectangular_request, DG_ALGORITHM_BSP_TREE, 80, 48, 222333u);
    rectangular_request.params.bsp.min_rooms = 10;
    rectangular_request.params.bsp.max_rooms = 12;
    rectangular_request.params.bsp.room_min_size = 5;
    rectangular_request.params.bsp.room_max_size = 10;

    organic_request = rectangular_request;
    dg_default_process_method(&organic_methods[0], DG_PROCESS_METHOD_ROOM_SHAPE);
    organic_methods[0].params.room_shape.mode = DG_ROOM_SHAPE_ORGANIC;
    organic_methods[0].params.room_shape.organicity = 65;
    organic_request.process.methods = organic_methods;
    organic_request.process.method_count = 1;

    ASSERT_STATUS(dg_generate(&rectangular_request, &rectangular_map), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&organic_request, &organic_map), DG_STATUS_OK);

    ASSERT_TRUE(!maps_have_same_tiles(&rectangular_map, &organic_map));
    ASSERT_TRUE(organic_map.metadata.generation_request.process.method_count == 1);
    ASSERT_TRUE(
        organic_map.metadata.generation_request.process.methods[0].type ==
        (int)DG_PROCESS_METHOD_ROOM_SHAPE
    );
    ASSERT_TRUE(
        organic_map.metadata.generation_request.process.methods[0].params.room_shape.mode ==
        (int)DG_ROOM_SHAPE_ORGANIC
    );

    dg_map_destroy(&rectangular_map);
    dg_map_destroy(&organic_map);
    return 0;
}

static int test_post_process_path_smoothing_changes_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 4000u; seed < 4060u; ++seed) {
        dg_generate_request_t base_request;
        dg_generate_request_t smooth_request;
        dg_map_t base_map = {0};
        dg_map_t smooth_map = {0};
        dg_process_method_t smooth_methods[1];

        dg_default_generate_request(&base_request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        base_request.params.rooms_and_mazes.min_rooms = 10;
        base_request.params.rooms_and_mazes.max_rooms = 16;
        base_request.params.rooms_and_mazes.room_min_size = 4;
        base_request.params.rooms_and_mazes.room_max_size = 10;
        base_request.params.rooms_and_mazes.dead_end_prune_steps = 0;

        smooth_request = base_request;
        dg_default_process_method(&smooth_methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
        smooth_methods[0].params.path_smooth.strength = 2;
        smooth_request.process.methods = smooth_methods;
        smooth_request.process.method_count = 1;

        ASSERT_STATUS(dg_generate(&base_request, &base_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&smooth_request, &smooth_map), DG_STATUS_OK);

        ASSERT_TRUE(smooth_map.metadata.walkable_tile_count >= base_map.metadata.walkable_tile_count);
        ASSERT_TRUE(
            smooth_map.metadata.connected_component_count <= base_map.metadata.connected_component_count
        );

        if (!maps_have_same_tiles(&base_map, &smooth_map)) {
            ASSERT_TRUE(smooth_map.metadata.generation_request.process.method_count == 1);
            ASSERT_TRUE(
                smooth_map.metadata.generation_request.process.methods[0].type ==
                (int)DG_PROCESS_METHOD_PATH_SMOOTH
            );
            ASSERT_TRUE(
                smooth_map.metadata.generation_request.process.methods[0].params.path_smooth.strength ==
                2
            );
            found_difference = true;
        }

        dg_map_destroy(&base_map);
        dg_map_destroy(&smooth_map);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_generation_request_snapshot_populated(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definitions[2];
    dg_process_method_t process_methods[3];
    const dg_generation_request_snapshot_t *snapshot;

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, 515151u);
    request.params.rooms_and_mazes.min_rooms = 11;
    request.params.rooms_and_mazes.max_rooms = 16;
    request.params.rooms_and_mazes.room_min_size = 5;
    request.params.rooms_and_mazes.room_max_size = 9;
    request.params.rooms_and_mazes.maze_wiggle_percent = 25;
    request.params.rooms_and_mazes.min_room_connections = 1;
    request.params.rooms_and_mazes.max_room_connections = 2;
    request.params.rooms_and_mazes.ensure_full_connectivity = 0;
    request.params.rooms_and_mazes.dead_end_prune_steps = 8;
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_ROOM_SHAPE);
    process_methods[0].params.room_shape.mode = DG_ROOM_SHAPE_ORGANIC;
    process_methods[0].params.room_shape.organicity = 60;
    dg_default_process_method(&process_methods[1], DG_PROCESS_METHOD_SCALE);
    process_methods[1].params.scale.factor = 2;
    dg_default_process_method(&process_methods[2], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[2].params.path_smooth.strength = 3;
    request.process.methods = process_methods;
    request.process.method_count = 3;

    dg_default_room_type_definition(&definitions[0], 701u);
    definitions[0].min_count = 2;
    definitions[0].preferences.higher_degree_bias = 20;
    dg_default_room_type_definition(&definitions[1], 702u);
    definitions[1].min_count = 1;
    definitions[1].preferences.border_distance_bias = 35;

    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.strict_mode = 1;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 701u;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    snapshot = &map.metadata.generation_request;
    ASSERT_TRUE(snapshot->present == 1);
    ASSERT_TRUE(snapshot->width == request.width);
    ASSERT_TRUE(snapshot->height == request.height);
    ASSERT_TRUE(snapshot->seed == request.seed);
    ASSERT_TRUE(snapshot->algorithm_id == (int)request.algorithm);
    ASSERT_TRUE(snapshot->process.method_count == request.process.method_count);
    ASSERT_TRUE(snapshot->process.methods != NULL);
    ASSERT_TRUE(snapshot->process.methods[0].type == (int)DG_PROCESS_METHOD_ROOM_SHAPE);
    ASSERT_TRUE(snapshot->process.methods[0].params.room_shape.mode == (int)DG_ROOM_SHAPE_ORGANIC);
    ASSERT_TRUE(snapshot->process.methods[0].params.room_shape.organicity == 60);
    ASSERT_TRUE(snapshot->process.methods[1].type == (int)DG_PROCESS_METHOD_SCALE);
    ASSERT_TRUE(snapshot->process.methods[1].params.scale.factor == 2);
    ASSERT_TRUE(snapshot->process.methods[2].type == (int)DG_PROCESS_METHOD_PATH_SMOOTH);
    ASSERT_TRUE(snapshot->process.methods[2].params.path_smooth.strength == 3);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.min_rooms == request.params.rooms_and_mazes.min_rooms);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.max_rooms == request.params.rooms_and_mazes.max_rooms);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.room_min_size ==
                request.params.rooms_and_mazes.room_min_size);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.room_max_size ==
                request.params.rooms_and_mazes.room_max_size);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.maze_wiggle_percent ==
                request.params.rooms_and_mazes.maze_wiggle_percent);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.min_room_connections ==
                request.params.rooms_and_mazes.min_room_connections);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.max_room_connections ==
                request.params.rooms_and_mazes.max_room_connections);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.ensure_full_connectivity ==
                request.params.rooms_and_mazes.ensure_full_connectivity);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.dead_end_prune_steps ==
                request.params.rooms_and_mazes.dead_end_prune_steps);

    ASSERT_TRUE(snapshot->room_types.definition_count == request.room_types.definition_count);
    ASSERT_TRUE(snapshot->room_types.policy.strict_mode == request.room_types.policy.strict_mode);
    ASSERT_TRUE(snapshot->room_types.policy.allow_untyped_rooms ==
                request.room_types.policy.allow_untyped_rooms);
    ASSERT_TRUE(snapshot->room_types.policy.default_type_id ==
                request.room_types.policy.default_type_id);
    ASSERT_TRUE(snapshot->room_types.definitions != NULL);
    ASSERT_TRUE(snapshot->room_types.definitions[0].type_id == definitions[0].type_id);
    ASSERT_TRUE(snapshot->room_types.definitions[0].min_count == definitions[0].min_count);
    ASSERT_TRUE(snapshot->room_types.definitions[1].type_id == definitions[1].type_id);
    ASSERT_TRUE(snapshot->room_types.definitions[1].min_count == definitions[1].min_count);

    dg_map_destroy(&map);
    return 0;
}

static int test_map_serialization_roundtrip(void)
{
    const char *path;
    dg_generate_request_t request;
    dg_map_t original = {0};
    dg_map_t loaded = {0};
    dg_room_type_definition_t definitions[2];
    dg_process_method_t process_methods[3];

    path = "dungeoneer_test_roundtrip.dgmap";

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 6060u);
    request.params.bsp.min_rooms = 9;
    request.params.bsp.max_rooms = 12;
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_ROOM_SHAPE);
    process_methods[0].params.room_shape.mode = DG_ROOM_SHAPE_ORGANIC;
    process_methods[0].params.room_shape.organicity = 55;
    dg_default_process_method(&process_methods[1], DG_PROCESS_METHOD_SCALE);
    process_methods[1].params.scale.factor = 2;
    dg_default_process_method(&process_methods[2], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[2].params.path_smooth.strength = 2;
    request.process.methods = process_methods;
    request.process.method_count = 3;
    dg_default_room_type_definition(&definitions[0], 51u);
    definitions[0].min_count = 2;
    dg_default_room_type_definition(&definitions[1], 52u);
    definitions[1].min_count = 2;
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.strict_mode = 1;

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

static int test_room_type_config_scaffold(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definitions[2];

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 8080u);
    ASSERT_TRUE(request.room_types.definitions == NULL);
    ASSERT_TRUE(request.room_types.definition_count == 0);
    ASSERT_TRUE(request.room_types.policy.strict_mode == 0);
    ASSERT_TRUE(request.room_types.policy.allow_untyped_rooms == 1);
    ASSERT_TRUE(request.room_types.policy.default_type_id == 0u);

    dg_default_room_type_definition(&definitions[0], 10u);
    definitions[0].min_count = 1;
    definitions[0].target_count = 2;

    dg_default_room_type_definition(&definitions[1], 20u);
    definitions[1].preferences.weight = 3;
    definitions[1].constraints.area_min = 12;
    definitions[1].constraints.area_max = 150;

    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 10u;
    request.params.bsp.min_rooms = 8;
    request.params.bsp.max_rooms = 12;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
    ASSERT_TRUE(map.metadata.room_count > 0);
    ASSERT_TRUE(count_rooms_with_assigned_type(&map) == map.metadata.room_count);
    ASSERT_TRUE(count_rooms_with_type_id(&map, 10u) >= 1);

    dg_map_destroy(&map);
    return 0;
}

static int test_room_type_assignment_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};
    dg_room_type_definition_t definitions[2];

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 7007u);
    request.params.bsp.min_rooms = 10;
    request.params.bsp.max_rooms = 10;

    dg_default_room_type_definition(&definitions[0], 100u);
    definitions[0].preferences.weight = 3;
    definitions[0].preferences.higher_degree_bias = 40;
    definitions[0].min_count = 2;

    dg_default_room_type_definition(&definitions[1], 200u);
    definitions[1].preferences.weight = 2;
    definitions[1].preferences.border_distance_bias = 35;
    definitions[1].min_count = 2;

    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.strict_mode = 1;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 100u;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_room_type_assignment_minimums(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definitions[2];

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 8123u);
    request.params.bsp.min_rooms = 12;
    request.params.bsp.max_rooms = 12;

    dg_default_room_type_definition(&definitions[0], 31u);
    definitions[0].min_count = 3;
    definitions[0].preferences.larger_room_bias = 30;

    dg_default_room_type_definition(&definitions[1], 32u);
    definitions[1].min_count = 4;
    definitions[1].preferences.higher_degree_bias = 25;

    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.strict_mode = 1;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
    ASSERT_TRUE(count_rooms_with_type_id(&map, 31u) >= 3);
    ASSERT_TRUE(count_rooms_with_type_id(&map, 32u) >= 4);

    dg_map_destroy(&map);
    return 0;
}

static int test_room_type_strict_minimum_infeasible(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definition;

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 9001u);
    dg_default_room_type_definition(&definition, 7u);
    definition.min_count = 1;
    definition.constraints.area_min = 1000000;
    definition.constraints.area_max = -1;

    request.room_types.definitions = &definition;
    request.room_types.definition_count = 1;
    request.room_types.policy.strict_mode = 1;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_GENERATION_FAILED);
    return 0;
}

static int test_room_type_strict_requires_full_coverage(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definition;

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 9002u);
    dg_default_room_type_definition(&definition, 11u);
    definition.min_count = 0;
    definition.constraints.degree_min = 100;
    definition.constraints.degree_max = -1;

    request.room_types.definitions = &definition;
    request.room_types.definition_count = 1;
    request.room_types.policy.strict_mode = 1;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 11u;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_GENERATION_FAILED);
    return 0;
}

static int test_invalid_generate_request(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definitions[2];
    dg_process_method_t process_methods[3];

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

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.maze_wiggle_percent = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.maze_wiggle_percent = 101;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.min_room_connections = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.min_room_connections = 3;
    request.params.rooms_and_mazes.max_room_connections = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.ensure_full_connectivity = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.dead_end_prune_steps = -2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.room_types.definition_count = 1;
    request.room_types.definitions = NULL;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.process.method_count = 1;
    request.process.methods = NULL;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_SCALE);
    process_methods[0].params.scale.factor = 0;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_ROOM_SHAPE);
    process_methods[0].params.room_shape.mode = (dg_room_shape_mode_t)99;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_ROOM_SHAPE);
    process_methods[0].params.room_shape.organicity = -1;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[0].params.path_smooth.strength = -1;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[0].params.path_smooth.strength = 13;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    process_methods[0].type = (dg_process_method_type_t)99;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.room_types.policy.strict_mode = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.room_types.policy.allow_untyped_rooms = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.room_types.policy.allow_untyped_rooms = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_room_type_definition(&definitions[0], 3u);
    dg_default_room_type_definition(&definitions[1], 3u);
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_room_type_definition(&definitions[0], 3u);
    definitions[0].enabled = 2;
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_room_type_definition(&definitions[0], 3u);
    definitions[0].constraints.area_min = 20;
    definitions[0].constraints.area_max = 10;
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_room_type_definition(&definitions[0], 3u);
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 1;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 42u;
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
        {"rooms_and_mazes_pruning_control", test_rooms_and_mazes_pruning_control},
        {"rooms_and_mazes_wiggle_affects_layout", test_rooms_and_mazes_wiggle_affects_layout},
        {"rooms_and_mazes_unpruned_has_no_isolated_seed_tiles",
         test_rooms_and_mazes_unpruned_has_no_isolated_seed_tiles},
        {"post_process_scaling", test_post_process_scaling},
        {"post_process_room_shape_changes_layout", test_post_process_room_shape_changes_layout},
        {"post_process_path_smoothing_changes_layout", test_post_process_path_smoothing_changes_layout},
        {"generation_request_snapshot_populated", test_generation_request_snapshot_populated},
        {"map_serialization_roundtrip", test_map_serialization_roundtrip},
        {"map_load_rejects_invalid_magic", test_map_load_rejects_invalid_magic},
        {"room_type_config_scaffold", test_room_type_config_scaffold},
        {"room_type_assignment_determinism", test_room_type_assignment_determinism},
        {"room_type_assignment_minimums", test_room_type_assignment_minimums},
        {"room_type_strict_minimum_infeasible", test_room_type_strict_minimum_infeasible},
        {"room_type_strict_requires_full_coverage", test_room_type_strict_requires_full_coverage},
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
