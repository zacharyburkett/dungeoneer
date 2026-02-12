#ifndef DUNGEONEER_MAP_H
#define DUNGEONEER_MAP_H

#include "dungeoneer/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t dg_room_flags_t;

#define DG_ROOM_FLAG_NONE ((dg_room_flags_t)0u)
#define DG_ROOM_FLAG_SPECIAL ((dg_room_flags_t)1u)
#define DG_ROOM_TYPE_UNASSIGNED UINT32_MAX

typedef enum dg_room_role {
    DG_ROOM_ROLE_NONE = 0,
    DG_ROOM_ROLE_ENTRANCE = 1,
    DG_ROOM_ROLE_EXIT = 2,
    DG_ROOM_ROLE_BOSS = 3,
    DG_ROOM_ROLE_TREASURE = 4,
    DG_ROOM_ROLE_SHOP = 5
} dg_room_role_t;

typedef struct dg_room_metadata {
    int id;
    dg_rect_t bounds;
    dg_room_flags_t flags;
    dg_room_role_t role;
    uint32_t type_id;
} dg_room_metadata_t;

typedef struct dg_corridor_metadata {
    int from_room_id;
    int to_room_id;
    int width;
    int length;
} dg_corridor_metadata_t;

typedef struct dg_room_neighbor {
    int room_id;
    int corridor_index;
} dg_room_neighbor_t;

typedef struct dg_room_adjacency_span {
    size_t start_index;
    size_t count;
} dg_room_adjacency_span_t;

typedef enum dg_map_generation_class {
    DG_MAP_GENERATION_CLASS_UNKNOWN = 0,
    DG_MAP_GENERATION_CLASS_ROOM_LIKE = 1,
    DG_MAP_GENERATION_CLASS_CAVE_LIKE = 2
} dg_map_generation_class_t;

typedef struct dg_snapshot_bsp_config {
    int min_rooms;
    int max_rooms;
    int room_min_size;
    int room_max_size;
} dg_snapshot_bsp_config_t;

typedef struct dg_snapshot_drunkards_walk_config {
    int wiggle_percent;
} dg_snapshot_drunkards_walk_config_t;

typedef struct dg_snapshot_rooms_and_mazes_config {
    int min_rooms;
    int max_rooms;
    int room_min_size;
    int room_max_size;
    int maze_wiggle_percent;
    int min_room_connections;
    int max_room_connections;
    int ensure_full_connectivity;
    int dead_end_prune_steps;
} dg_snapshot_rooms_and_mazes_config_t;

typedef struct dg_snapshot_room_type_constraints {
    int area_min;
    int area_max;
    int degree_min;
    int degree_max;
    int border_distance_min;
    int border_distance_max;
    int graph_depth_min;
    int graph_depth_max;
} dg_snapshot_room_type_constraints_t;

typedef struct dg_snapshot_room_type_preferences {
    int weight;
    int larger_room_bias;
    int higher_degree_bias;
    int border_distance_bias;
} dg_snapshot_room_type_preferences_t;

typedef struct dg_snapshot_room_type_definition {
    uint32_t type_id;
    int enabled;
    int min_count;
    int max_count;
    int target_count;
    dg_snapshot_room_type_constraints_t constraints;
    dg_snapshot_room_type_preferences_t preferences;
} dg_snapshot_room_type_definition_t;

typedef struct dg_snapshot_room_type_assignment_policy {
    int strict_mode;
    int allow_untyped_rooms;
    uint32_t default_type_id;
} dg_snapshot_room_type_assignment_policy_t;

typedef struct dg_snapshot_room_type_assignment_config {
    dg_snapshot_room_type_definition_t *definitions;
    size_t definition_count;
    dg_snapshot_room_type_assignment_policy_t policy;
} dg_snapshot_room_type_assignment_config_t;

typedef struct dg_snapshot_process_scale_config {
    int factor;
} dg_snapshot_process_scale_config_t;

typedef struct dg_snapshot_process_room_shape_config {
    int mode;
    int organicity;
} dg_snapshot_process_room_shape_config_t;

typedef struct dg_snapshot_process_path_smooth_config {
    int strength;
    int inner_enabled;
    int outer_enabled;
} dg_snapshot_process_path_smooth_config_t;

typedef struct dg_snapshot_process_method {
    int type;
    union {
        dg_snapshot_process_scale_config_t scale;
        dg_snapshot_process_room_shape_config_t room_shape;
        dg_snapshot_process_path_smooth_config_t path_smooth;
    } params;
} dg_snapshot_process_method_t;

typedef struct dg_snapshot_process_config {
    dg_snapshot_process_method_t *methods;
    size_t method_count;
} dg_snapshot_process_config_t;

typedef struct dg_snapshot_traversal_constraints {
    int max_jump_up;
    int max_jump_across;
    int max_drop_down;
    int require_grounded_connectivity;
} dg_snapshot_traversal_constraints_t;

typedef struct dg_generation_request_snapshot {
    int present;
    int width;
    int height;
    uint64_t seed;
    int algorithm_id;
    int perspective;
    union {
        dg_snapshot_bsp_config_t bsp;
        dg_snapshot_drunkards_walk_config_t drunkards_walk;
        dg_snapshot_rooms_and_mazes_config_t rooms_and_mazes;
    } params;
    dg_snapshot_traversal_constraints_t traversal;
    dg_snapshot_process_config_t process;
    dg_snapshot_room_type_assignment_config_t room_types;
} dg_generation_request_snapshot_t;

typedef struct dg_process_step_diagnostics {
    int method_type;
    size_t walkable_before;
    size_t walkable_after;
    int64_t walkable_delta;
    size_t components_before;
    size_t components_after;
    int64_t components_delta;
    int connected_before;
    int connected_after;
} dg_process_step_diagnostics_t;

typedef struct dg_room_type_quota_diagnostics {
    uint32_t type_id;
    int enabled;
    int min_count;
    int max_count;
    int target_count;
    size_t assigned_count;
    int min_satisfied;
    int max_satisfied;
    int target_satisfied;
} dg_room_type_quota_diagnostics_t;

typedef struct dg_generation_diagnostics {
    dg_process_step_diagnostics_t *process_steps;
    size_t process_step_count;

    size_t typed_room_count;
    size_t untyped_room_count;

    dg_room_type_quota_diagnostics_t *room_type_quotas;
    size_t room_type_count;
    size_t room_type_min_miss_count;
    size_t room_type_max_excess_count;
    size_t room_type_target_miss_count;
} dg_generation_diagnostics_t;

typedef struct dg_map_metadata {
    uint64_t seed;
    int algorithm_id;
    dg_map_generation_class_t generation_class;

    dg_room_metadata_t *rooms;
    size_t room_count;
    size_t room_capacity;

    dg_corridor_metadata_t *corridors;
    size_t corridor_count;
    size_t corridor_capacity;

    /*
     * Room graph represented as adjacency spans into `room_neighbors`.
     * For room i, neighbors are in:
     *   room_neighbors[room_adjacency[i].start_index ... +count)
     */
    dg_room_adjacency_span_t *room_adjacency;
    size_t room_adjacency_count;
    dg_room_neighbor_t *room_neighbors;
    size_t room_neighbor_count;

    size_t walkable_tile_count;
    size_t wall_tile_count;
    size_t special_room_count;
    size_t entrance_room_count;
    size_t exit_room_count;
    size_t boss_room_count;
    size_t treasure_room_count;
    size_t shop_room_count;
    size_t leaf_room_count;
    size_t corridor_total_length;
    int entrance_exit_distance;
    size_t connected_component_count;
    size_t largest_component_size;
    bool connected_floor;
    size_t generation_attempts;
    dg_generation_diagnostics_t diagnostics;
    dg_generation_request_snapshot_t generation_request;
} dg_map_metadata_t;

typedef struct dg_map {
    int width;
    int height;
    dg_tile_t *tiles;
    dg_map_metadata_t metadata;
} dg_map_t;

dg_status_t dg_map_init(dg_map_t *map, int width, int height, dg_tile_t initial_tile);
void dg_map_destroy(dg_map_t *map);

dg_status_t dg_map_fill(dg_map_t *map, dg_tile_t tile);
dg_status_t dg_map_set_tile(dg_map_t *map, int x, int y, dg_tile_t tile);
dg_tile_t dg_map_get_tile(const dg_map_t *map, int x, int y);
bool dg_map_in_bounds(const dg_map_t *map, int x, int y);

void dg_map_clear_metadata(dg_map_t *map);
dg_status_t dg_map_add_room(dg_map_t *map, const dg_rect_t *bounds, dg_room_flags_t flags);
dg_status_t dg_map_add_corridor(
    dg_map_t *map,
    int from_room_id,
    int to_room_id,
    int width,
    int length
);

#ifdef __cplusplus
}
#endif

#endif
