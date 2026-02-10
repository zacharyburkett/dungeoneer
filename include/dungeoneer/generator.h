#ifndef DUNGEONEER_GENERATOR_H
#define DUNGEONEER_GENERATOR_H

#include "dungeoneer/map.h"
#include "dungeoneer/rng.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dg_algorithm {
    DG_ALGORITHM_BSP_TREE = 0,
    DG_ALGORITHM_DRUNKARDS_WALK = 1,
    DG_ALGORITHM_ROOMS_AND_MAZES = 2
} dg_algorithm_t;

typedef struct dg_bsp_config {
    int min_rooms;
    int max_rooms;
    int room_min_size;
    int room_max_size;
} dg_bsp_config_t;

typedef struct dg_drunkards_walk_config {
    /*
     * Probability (0..100) of changing direction at each step.
     * Higher values produce noisier/wigglier paths.
     */
    int wiggle_percent;
} dg_drunkards_walk_config_t;

typedef struct dg_rooms_and_mazes_config {
    int min_rooms;
    int max_rooms;
    int room_min_size;
    int room_max_size;
    /*
     * Probability (0..100) of changing direction while carving mazes.
     * 0 keeps long straight corridors when possible; 100 is highly wiggly.
     */
    int maze_wiggle_percent;
    int min_room_connections;
    int max_room_connections;
    int ensure_full_connectivity;
    /*
     * Dead-end pruning passes after room connections:
     *   0  = disabled
     *   >0 = max passes
     *   -1 = until stable (no more dead-ends)
     */
    int dead_end_prune_steps;
} dg_rooms_and_mazes_config_t;

typedef enum dg_room_shape_mode {
    DG_ROOM_SHAPE_RECTANGULAR = 0,
    DG_ROOM_SHAPE_ORGANIC = 1
} dg_room_shape_mode_t;

typedef enum dg_process_method_type {
    DG_PROCESS_METHOD_SCALE = 0,
    DG_PROCESS_METHOD_ROOM_SHAPE = 1,
    DG_PROCESS_METHOD_PATH_SMOOTH = 2
} dg_process_method_type_t;

typedef struct dg_process_scale_config {
    /*
     * Tile upscaling factor:
     *   1 = no scaling
     *   >1 = nearest-neighbor upscaling
     */
    int factor;
} dg_process_scale_config_t;

typedef struct dg_process_room_shape_config {
    /*
     * Room shape post-process:
     *   RECTANGULAR = preserve layout room rectangles
     *   ORGANIC     = carve noisier room interiors while preserving room connections
     */
    dg_room_shape_mode_t mode;
    /*
     * Organic room roughness (0..100).
     * Higher values create more irregular room boundaries.
     */
    int organicity;
} dg_process_room_shape_config_t;

typedef struct dg_process_path_smooth_config {
    /*
     * Number of smoothing passes (0..12).
     * 0 disables the effect for this step.
     */
    int strength;
} dg_process_path_smooth_config_t;

typedef struct dg_process_method {
    dg_process_method_type_t type;
    union {
        dg_process_scale_config_t scale;
        dg_process_room_shape_config_t room_shape;
        dg_process_path_smooth_config_t path_smooth;
    } params;
} dg_process_method_t;

typedef struct dg_process_config {
    const dg_process_method_t *methods;
    size_t method_count;
} dg_process_config_t;

typedef struct dg_room_type_constraints {
    /*
     * Constraint ranges:
     *   min >= 0
     *   max == -1 means unbounded, otherwise max >= min
     */
    int area_min;
    int area_max;
    int degree_min;
    int degree_max;
    int border_distance_min;
    int border_distance_max;
    int graph_depth_min;
    int graph_depth_max;
} dg_room_type_constraints_t;

typedef struct dg_room_type_preferences {
    /*
     * Relative soft-priority bias in scoring. Higher means more likely.
     * Must be >= 0.
     */
    int weight;
    /*
     * Directional biases in [-100, 100].
     * Positive values prefer larger/higher/farther, negative the opposite.
     */
    int larger_room_bias;
    int higher_degree_bias;
    int border_distance_bias;
} dg_room_type_preferences_t;

typedef struct dg_room_type_definition {
    uint32_t type_id;
    int enabled;
    /*
     * Quotas:
     *   min_count >= 0
     *   max_count == -1 means unbounded, otherwise max_count >= min_count
     *   target_count == -1 means unset, otherwise must be in [min_count, max_count]
     */
    int min_count;
    int max_count;
    int target_count;
    dg_room_type_constraints_t constraints;
    dg_room_type_preferences_t preferences;
} dg_room_type_definition_t;

typedef struct dg_room_type_assignment_policy {
    /*
     * strict_mode:
     *   1 = fail generation when constraints are infeasible
     *   0 = best effort (future behavior)
     */
    int strict_mode;
    /*
     * allow_untyped_rooms:
     *   1 = assignment may leave rooms untyped
     *   0 = all rooms must receive a type (future behavior)
     */
    int allow_untyped_rooms;
    uint32_t default_type_id;
} dg_room_type_assignment_policy_t;

typedef struct dg_room_type_assignment_config {
    const dg_room_type_definition_t *definitions;
    size_t definition_count;
    dg_room_type_assignment_policy_t policy;
} dg_room_type_assignment_config_t;

typedef struct dg_generate_request {
    int width;
    int height;
    uint64_t seed;
    dg_algorithm_t algorithm;
    union {
        dg_bsp_config_t bsp;
        dg_drunkards_walk_config_t drunkards_walk;
        dg_rooms_and_mazes_config_t rooms_and_mazes;
    } params;
    dg_process_config_t process;
    dg_room_type_assignment_config_t room_types;
} dg_generate_request_t;

void dg_default_bsp_config(dg_bsp_config_t *config);
void dg_default_drunkards_walk_config(dg_drunkards_walk_config_t *config);
void dg_default_rooms_and_mazes_config(dg_rooms_and_mazes_config_t *config);
void dg_default_process_method(dg_process_method_t *method, dg_process_method_type_t type);
void dg_default_process_config(dg_process_config_t *config);
void dg_default_room_type_constraints(dg_room_type_constraints_t *constraints);
void dg_default_room_type_preferences(dg_room_type_preferences_t *preferences);
void dg_default_room_type_definition(dg_room_type_definition_t *definition, uint32_t type_id);
void dg_default_room_type_assignment_policy(dg_room_type_assignment_policy_t *policy);
void dg_default_room_type_assignment_config(dg_room_type_assignment_config_t *config);
void dg_default_generate_request(
    dg_generate_request_t *request,
    dg_algorithm_t algorithm,
    int width,
    int height,
    uint64_t seed
);

dg_map_generation_class_t dg_algorithm_generation_class(dg_algorithm_t algorithm);

/*
 * `out_map` is expected to be zero-initialized or previously destroyed.
 * Call `dg_map_destroy` when done with the returned map.
 */
dg_status_t dg_generate(const dg_generate_request_t *request, dg_map_t *out_map);

#ifdef __cplusplus
}
#endif

#endif
