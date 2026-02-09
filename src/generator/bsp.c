#include "internal.h"

#include <stdlib.h>

typedef struct dg_bsp_node {
    dg_rect_t bounds;
    int left;
    int right;
    int room_id;
    bool is_leaf;
} dg_bsp_node_t;

static int dg_bsp_min_int(int a, int b)
{
    return a < b ? a : b;
}

static dg_point_t dg_room_center(const dg_room_metadata_t *room)
{
    dg_point_t center;
    center.x = room->bounds.x + room->bounds.width / 2;
    center.y = room->bounds.y + room->bounds.height / 2;
    return center;
}

static void dg_carve_room(dg_map_t *map, const dg_rect_t *room)
{
    int y;
    int x;

    for (y = room->y; y < room->y + room->height; ++y) {
        for (x = room->x; x < room->x + room->width; ++x) {
            (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
        }
    }
}

static void dg_carve_horizontal_path(dg_map_t *map, int x0, int x1, int y)
{
    int x;
    int start;
    int end;

    start = dg_min_int(x0, x1);
    end = dg_max_int(x0, x1);
    for (x = start; x <= end; ++x) {
        (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
    }
}

static void dg_carve_vertical_path(dg_map_t *map, int x, int y0, int y1)
{
    int y;
    int start;
    int end;

    start = dg_min_int(y0, y1);
    end = dg_max_int(y0, y1);
    for (y = start; y <= end; ++y) {
        (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
    }
}

static dg_status_t dg_connect_rooms(dg_map_t *map, dg_rng_t *rng, int room_a, int room_b)
{
    const dg_room_metadata_t *a;
    const dg_room_metadata_t *b;
    dg_point_t ca;
    dg_point_t cb;
    bool horizontal_first;
    int corridor_length;

    if (room_a < 0 || room_b < 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if ((size_t)room_a >= map->metadata.room_count || (size_t)room_b >= map->metadata.room_count) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (room_a == room_b) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    a = &map->metadata.rooms[room_a];
    b = &map->metadata.rooms[room_b];
    ca = dg_room_center(a);
    cb = dg_room_center(b);

    horizontal_first = (dg_rng_next_u32(rng) & 1u) != 0u;
    if (horizontal_first) {
        dg_carve_horizontal_path(map, ca.x, cb.x, ca.y);
        dg_carve_vertical_path(map, cb.x, ca.y, cb.y);
    } else {
        dg_carve_vertical_path(map, ca.x, ca.y, cb.y);
        dg_carve_horizontal_path(map, ca.x, cb.x, cb.y);
    }

    corridor_length = 1 + (int)labs(ca.x - cb.x) + (int)labs(ca.y - cb.y);
    return dg_map_add_corridor(map, room_a, room_b, 1, corridor_length);
}

static bool dg_node_can_split(const dg_bsp_node_t *node, int min_leaf_width, int min_leaf_height)
{
    bool can_split_vertical;
    bool can_split_horizontal;

    can_split_vertical = node->bounds.width >= min_leaf_width * 2;
    can_split_horizontal = node->bounds.height >= min_leaf_height * 2;
    return can_split_vertical || can_split_horizontal;
}

static dg_status_t dg_split_leaf(
    dg_bsp_node_t *nodes,
    size_t *node_count,
    size_t node_capacity,
    int leaf_index,
    int min_leaf_width,
    int min_leaf_height,
    dg_rng_t *rng
)
{
    dg_bsp_node_t *leaf;
    bool can_split_vertical;
    bool can_split_horizontal;
    bool split_vertical;
    int split_coord;
    dg_bsp_node_t left_child;
    dg_bsp_node_t right_child;

    if (nodes == NULL || node_count == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (leaf_index < 0 || (size_t)leaf_index >= *node_count) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (*node_count + 2 > node_capacity) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    leaf = &nodes[leaf_index];
    if (!leaf->is_leaf) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    can_split_vertical = leaf->bounds.width >= min_leaf_width * 2;
    can_split_horizontal = leaf->bounds.height >= min_leaf_height * 2;
    if (!can_split_vertical && !can_split_horizontal) {
        return DG_STATUS_GENERATION_FAILED;
    }

    if (can_split_vertical && can_split_horizontal) {
        if (leaf->bounds.width > leaf->bounds.height) {
            split_vertical = true;
        } else if (leaf->bounds.height > leaf->bounds.width) {
            split_vertical = false;
        } else {
            split_vertical = (dg_rng_next_u32(rng) & 1u) != 0u;
        }
    } else {
        split_vertical = can_split_vertical;
    }

    if (split_vertical) {
        int min_split = leaf->bounds.x + min_leaf_width;
        int max_split = leaf->bounds.x + leaf->bounds.width - min_leaf_width;
        split_coord = dg_rng_range(rng, min_split, max_split);

        left_child.bounds = (dg_rect_t){
            leaf->bounds.x,
            leaf->bounds.y,
            split_coord - leaf->bounds.x,
            leaf->bounds.height
        };
        right_child.bounds = (dg_rect_t){
            split_coord,
            leaf->bounds.y,
            (leaf->bounds.x + leaf->bounds.width) - split_coord,
            leaf->bounds.height
        };
    } else {
        int min_split = leaf->bounds.y + min_leaf_height;
        int max_split = leaf->bounds.y + leaf->bounds.height - min_leaf_height;
        split_coord = dg_rng_range(rng, min_split, max_split);

        left_child.bounds = (dg_rect_t){
            leaf->bounds.x,
            leaf->bounds.y,
            leaf->bounds.width,
            split_coord - leaf->bounds.y
        };
        right_child.bounds = (dg_rect_t){
            leaf->bounds.x,
            split_coord,
            leaf->bounds.width,
            (leaf->bounds.y + leaf->bounds.height) - split_coord
        };
    }

    left_child.left = -1;
    left_child.right = -1;
    left_child.room_id = -1;
    left_child.is_leaf = true;

    right_child.left = -1;
    right_child.right = -1;
    right_child.room_id = -1;
    right_child.is_leaf = true;

    leaf->left = (int)(*node_count);
    leaf->right = (int)(*node_count + 1);
    leaf->is_leaf = false;

    nodes[*node_count] = left_child;
    nodes[*node_count + 1] = right_child;
    *node_count += 2;

    return DG_STATUS_OK;
}

static dg_status_t dg_place_room_in_leaf(
    dg_map_t *map,
    dg_bsp_node_t *leaf,
    const dg_bsp_config_t *config,
    dg_rng_t *rng
)
{
    int max_room_width;
    int max_room_height;
    int room_width;
    int room_height;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    dg_rect_t room;
    dg_status_t status;

    max_room_width = dg_bsp_min_int(config->room_max_size, leaf->bounds.width - 2);
    max_room_height = dg_bsp_min_int(config->room_max_size, leaf->bounds.height - 2);

    if (max_room_width < config->room_min_size || max_room_height < config->room_min_size) {
        return DG_STATUS_GENERATION_FAILED;
    }

    room_width = dg_rng_range(rng, config->room_min_size, max_room_width);
    room_height = dg_rng_range(rng, config->room_min_size, max_room_height);

    min_x = leaf->bounds.x + 1;
    max_x = leaf->bounds.x + leaf->bounds.width - room_width - 1;
    min_y = leaf->bounds.y + 1;
    max_y = leaf->bounds.y + leaf->bounds.height - room_height - 1;

    if (max_x < min_x || max_y < min_y) {
        return DG_STATUS_GENERATION_FAILED;
    }

    room.x = dg_rng_range(rng, min_x, max_x);
    room.y = dg_rng_range(rng, min_y, max_y);
    room.width = room_width;
    room.height = room_height;

    dg_carve_room(map, &room);
    status = dg_map_add_room(map, &room, DG_ROOM_FLAG_NONE);
    if (status != DG_STATUS_OK) {
        return status;
    }

    leaf->room_id = (int)map->metadata.room_count - 1;
    return DG_STATUS_OK;
}

static dg_status_t dg_connect_tree_recursive(
    dg_map_t *map,
    dg_rng_t *rng,
    dg_bsp_node_t *nodes,
    int node_index,
    int *out_room_id
)
{
    dg_status_t status;
    int left_room;
    int right_room;

    if (nodes[node_index].is_leaf) {
        if (nodes[node_index].room_id < 0) {
            return DG_STATUS_GENERATION_FAILED;
        }

        *out_room_id = nodes[node_index].room_id;
        return DG_STATUS_OK;
    }

    status = dg_connect_tree_recursive(map, rng, nodes, nodes[node_index].left, &left_room);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_connect_tree_recursive(map, rng, nodes, nodes[node_index].right, &right_room);
    if (status != DG_STATUS_OK) {
        return status;
    }

    status = dg_connect_rooms(map, rng, left_room, right_room);
    if (status != DG_STATUS_OK) {
        return status;
    }

    if ((dg_rng_next_u32(rng) & 1u) != 0u) {
        *out_room_id = left_room;
    } else {
        *out_room_id = right_room;
    }

    return DG_STATUS_OK;
}

dg_status_t dg_generate_bsp_tree_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    const dg_bsp_config_t *config;
    int target_rooms;
    int min_leaf_size;
    dg_bsp_node_t *nodes;
    size_t node_capacity;
    size_t node_count;
    size_t i;
    int *split_candidates;
    size_t split_candidate_count;
    int *leaf_indices;
    size_t leaf_count;
    dg_status_t status;
    int representative_room;

    if (request == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    config = &request->params.bsp;
    target_rooms = dg_rng_range(rng, config->min_rooms, config->max_rooms);
    min_leaf_size = config->room_min_size + 2;

    if (map->width - 2 < min_leaf_size || map->height - 2 < min_leaf_size) {
        return DG_STATUS_GENERATION_FAILED;
    }

    node_capacity = (size_t)target_rooms * 8u + 8u;
    nodes = (dg_bsp_node_t *)calloc(node_capacity, sizeof(dg_bsp_node_t));
    split_candidates = (int *)malloc(node_capacity * sizeof(int));
    leaf_indices = (int *)malloc(node_capacity * sizeof(int));
    if (nodes == NULL || split_candidates == NULL || leaf_indices == NULL) {
        free(nodes);
        free(split_candidates);
        free(leaf_indices);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    nodes[0].bounds = (dg_rect_t){1, 1, map->width - 2, map->height - 2};
    nodes[0].left = -1;
    nodes[0].right = -1;
    nodes[0].room_id = -1;
    nodes[0].is_leaf = true;
    node_count = 1;

    while (true) {
        split_candidate_count = 0;
        leaf_count = 0;

        for (i = 0; i < node_count; ++i) {
            if (!nodes[i].is_leaf) {
                continue;
            }

            leaf_indices[leaf_count++] = (int)i;
            if (dg_node_can_split(&nodes[i], min_leaf_size, min_leaf_size)) {
                split_candidates[split_candidate_count++] = (int)i;
            }
        }

        if ((int)leaf_count >= target_rooms) {
            break;
        }

        if (split_candidate_count == 0) {
            break;
        }

        {
            int chosen = split_candidates[dg_rng_range(rng, 0, (int)split_candidate_count - 1)];
            status = dg_split_leaf(
                nodes,
                &node_count,
                node_capacity,
                chosen,
                min_leaf_size,
                min_leaf_size,
                rng
            );
            if (status != DG_STATUS_OK) {
                free(nodes);
                free(split_candidates);
                free(leaf_indices);
                return status;
            }
        }
    }

    leaf_count = 0;
    for (i = 0; i < node_count; ++i) {
        if (nodes[i].is_leaf) {
            leaf_indices[leaf_count++] = (int)i;
        }
    }

    if ((int)leaf_count < config->min_rooms) {
        free(nodes);
        free(split_candidates);
        free(leaf_indices);
        return DG_STATUS_GENERATION_FAILED;
    }

    for (i = 0; i < leaf_count; ++i) {
        status = dg_place_room_in_leaf(map, &nodes[leaf_indices[i]], config, rng);
        if (status != DG_STATUS_OK) {
            free(nodes);
            free(split_candidates);
            free(leaf_indices);
            return status;
        }
    }

    status = dg_connect_tree_recursive(map, rng, nodes, 0, &representative_room);
    if (status != DG_STATUS_OK) {
        free(nodes);
        free(split_candidates);
        free(leaf_indices);
        return status;
    }

    (void)representative_room;
    free(nodes);
    free(split_candidates);
    free(leaf_indices);
    return DG_STATUS_OK;
}
