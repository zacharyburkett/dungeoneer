#include "dungeoneer/rng.h"

#include <inttypes.h>
#include <stddef.h>

static uint64_t dg_rng_next_u64(dg_rng_t *rng)
{
    uint64_t x;

    x = rng->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng->state = x;
    return x * UINT64_C(2685821657736338717);
}

void dg_rng_seed(dg_rng_t *rng, uint64_t seed)
{
    if (rng == NULL) {
        return;
    }

    if (seed == 0) {
        seed = UINT64_C(0x9E3779B97F4A7C15);
    }

    rng->state = seed;
}

uint32_t dg_rng_next_u32(dg_rng_t *rng)
{
    if (rng == NULL) {
        return 0;
    }

    if (rng->state == 0) {
        dg_rng_seed(rng, UINT64_C(0x9E3779B97F4A7C15));
    }

    return (uint32_t)(dg_rng_next_u64(rng) >> 32);
}

int dg_rng_range(dg_rng_t *rng, int min_inclusive, int max_inclusive)
{
    int tmp;
    uint64_t span;
    uint64_t value;

    if (rng == NULL) {
        return min_inclusive;
    }

    if (min_inclusive > max_inclusive) {
        tmp = min_inclusive;
        min_inclusive = max_inclusive;
        max_inclusive = tmp;
    }

    span = (uint64_t)((int64_t)max_inclusive - (int64_t)min_inclusive) + 1u;
    if (span == 0u) {
        return min_inclusive;
    }

    value = (uint64_t)dg_rng_next_u32(rng);
    return min_inclusive + (int)(value % span);
}

float dg_rng_next_f32(dg_rng_t *rng)
{
    return (float)dg_rng_next_u32(rng) / (float)UINT32_MAX;
}
