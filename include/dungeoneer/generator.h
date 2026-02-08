#ifndef DUNGEONEER_GENERATOR_H
#define DUNGEONEER_GENERATOR_H

#include "dungeoneer/map.h"
#include "dungeoneer/rng.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dg_algorithm {
    DG_ALGORITHM_ROOMS_AND_CORRIDORS = 0,
    DG_ALGORITHM_ORGANIC_CAVE = 1
} dg_algorithm_t;

typedef enum dg_corridor_routing {
    DG_CORRIDOR_ROUTING_RANDOM = 0,
    DG_CORRIDOR_ROUTING_HORIZONTAL_FIRST = 1,
    DG_CORRIDOR_ROUTING_VERTICAL_FIRST = 2
} dg_corridor_routing_t;

typedef dg_room_flags_t (*dg_room_classifier_fn)(int room_index, const dg_rect_t *bounds, void *user_data);

typedef struct dg_rooms_corridors_config {
    int min_rooms;
    int max_rooms;
    int room_min_size;
    int room_max_size;
    int max_placement_attempts;
    int corridor_width;
    /*
     * Controls orientation of L-shaped corridor carving between room centers.
     */
    dg_corridor_routing_t corridor_routing;
    dg_room_classifier_fn classify_room;
    void *classify_room_user_data;
} dg_rooms_corridors_config_t;

typedef struct dg_organic_cave_config {
    int walk_steps;
    int brush_radius;
    int smoothing_passes;
    float target_floor_coverage;
} dg_organic_cave_config_t;

typedef struct dg_role_placement_weights {
    int distance_weight;
    int degree_weight;
    int leaf_bonus;
} dg_role_placement_weights_t;

typedef struct dg_generation_constraints {
    bool require_connected_floor;
    bool enforce_outer_walls;

    /*
     * Coverage bounds are expressed in [0.0, 1.0].
     * Set to 0.0 / 1.0 for "no extra bound".
     */
    float min_floor_coverage;
    float max_floor_coverage;

    /*
     * Room constraints only apply to algorithms that emit room metadata.
     * Set to 0 to disable.
     */
    int min_room_count;
    int max_room_count;
    int min_special_rooms;

    /*
     * Role counts are minimum required counts.
     * Roles are assigned using room graph metadata when these are non-zero.
     */
    int required_entrance_rooms;
    int required_exit_rooms;
    int required_boss_rooms;
    int required_treasure_rooms;
    int required_shop_rooms;

    /*
     * Minimum graph distance (in room-to-room hops) between entrance and exit
     * when both are required.
     */
    int min_entrance_exit_distance;

    /*
     * If true, all required boss rooms must be leaf rooms in the room graph.
     */
    bool require_boss_on_leaf;

    /*
     * Scoring model for room-role placement:
     * score = (distance_weight * graph_distance_from_entrance)
     *       + (degree_weight * room_degree)
     *       + (leaf_bonus if room is leaf, else 0)
     *
     * Each role can be tuned independently.
     * When both entrance and exit are required, the first entrance/exit pair
     * is chosen by maximum pairwise graph distance; these weights are applied
     * for any remaining entrance/exit slots and all other roles.
     */
    dg_role_placement_weights_t entrance_weights;
    dg_role_placement_weights_t exit_weights;
    dg_role_placement_weights_t boss_weights;
    dg_role_placement_weights_t treasure_weights;
    dg_role_placement_weights_t shop_weights;

    /*
     * Any tiles in forbidden regions are forced to walls in the final result.
     * Regions are non-owning pointers and must remain valid for dg_generate.
     */
    const dg_rect_t *forbidden_regions;
    size_t forbidden_region_count;

    /*
     * Number of generation attempts before failing constraint validation.
     * Must be >= 1; defaults to 1.
     */
    int max_generation_attempts;
} dg_generation_constraints_t;

typedef struct dg_generate_request {
    int width;
    int height;
    uint64_t seed;
    dg_algorithm_t algorithm;
    dg_generation_constraints_t constraints;
    union {
        dg_rooms_corridors_config_t rooms;
        dg_organic_cave_config_t organic;
    } params;
} dg_generate_request_t;

void dg_default_rooms_corridors_config(dg_rooms_corridors_config_t *config);
void dg_default_organic_cave_config(dg_organic_cave_config_t *config);
void dg_default_generate_request(
    dg_generate_request_t *request,
    dg_algorithm_t algorithm,
    int width,
    int height,
    uint64_t seed
);

/*
 * `out_map` is expected to be zero-initialized or previously destroyed.
 * Call `dg_map_destroy` when done with the returned map.
 */
dg_status_t dg_generate(const dg_generate_request_t *request, dg_map_t *out_map);

#ifdef __cplusplus
}
#endif

#endif
