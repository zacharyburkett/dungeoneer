#ifndef DUNGEONEER_GENERATOR_H
#define DUNGEONEER_GENERATOR_H

#include "dungeoneer/map.h"
#include "dungeoneer/rng.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dg_algorithm {
    DG_ALGORITHM_BSP_TREE = 0
} dg_algorithm_t;

typedef struct dg_bsp_config {
    int min_rooms;
    int max_rooms;
    int room_min_size;
    int room_max_size;
} dg_bsp_config_t;

typedef struct dg_generate_request {
    int width;
    int height;
    uint64_t seed;
    dg_bsp_config_t bsp;
} dg_generate_request_t;

void dg_default_bsp_config(dg_bsp_config_t *config);
void dg_default_generate_request(
    dg_generate_request_t *request,
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
