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

typedef dg_room_flags_t (*dg_room_classifier_fn)(int room_index, const dg_rect_t *bounds, void *user_data);

typedef struct dg_rooms_corridors_config {
    int min_rooms;
    int max_rooms;
    int room_min_size;
    int room_max_size;
    int max_placement_attempts;
    int corridor_width;
    dg_room_classifier_fn classify_room;
    void *classify_room_user_data;
} dg_rooms_corridors_config_t;

typedef struct dg_organic_cave_config {
    int walk_steps;
    int brush_radius;
    int smoothing_passes;
    float target_floor_coverage;
} dg_organic_cave_config_t;

typedef struct dg_generation_constraints {
    bool require_connected_floor;
    bool enforce_outer_walls;
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
