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
} dg_generate_request_t;

void dg_default_bsp_config(dg_bsp_config_t *config);
void dg_default_drunkards_walk_config(dg_drunkards_walk_config_t *config);
void dg_default_rooms_and_mazes_config(dg_rooms_and_mazes_config_t *config);
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
