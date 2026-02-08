#ifndef DUNGEONEER_RNG_H
#define DUNGEONEER_RNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dg_rng {
    uint64_t state;
} dg_rng_t;

void dg_rng_seed(dg_rng_t *rng, uint64_t seed);
uint32_t dg_rng_next_u32(dg_rng_t *rng);
int dg_rng_range(dg_rng_t *rng, int min_inclusive, int max_inclusive);
float dg_rng_next_f32(dg_rng_t *rng);

#ifdef __cplusplus
}
#endif

#endif
