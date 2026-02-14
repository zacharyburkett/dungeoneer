#include "internal.h"

#include <stdint.h>
#include <stdlib.h>

static int dg_simplex_fast_floor(double value)
{
    int i;

    i = (int)value;
    return (value < (double)i) ? (i - 1) : i;
}

static double dg_simplex_dot(const int grad[2], double x, double y)
{
    return (double)grad[0] * x + (double)grad[1] * y;
}

static void dg_simplex_build_perm_table(dg_rng_t *rng, uint8_t perm[512])
{
    uint8_t p[256];
    int i;

    for (i = 0; i < 256; ++i) {
        p[i] = (uint8_t)i;
    }

    for (i = 255; i > 0; --i) {
        int j = dg_rng_range(rng, 0, i);
        uint8_t tmp = p[i];
        p[i] = p[j];
        p[j] = tmp;
    }

    for (i = 0; i < 512; ++i) {
        perm[i] = p[i & 255];
    }
}

static double dg_simplex_noise2d(double xin, double yin, const uint8_t perm[512])
{
    static const int grad3[12][2] = {
        {1, 1}, {-1, 1}, {1, -1}, {-1, -1},
        {1, 0}, {-1, 0}, {1, 0}, {-1, 0},
        {0, 1}, {0, -1}, {0, 1}, {0, -1}
    };
    static const double F2 = 0.36602540378443864676;
    static const double G2 = 0.21132486540518711775;
    double n0;
    double n1;
    double n2;
    double s;
    int i;
    int j;
    double t;
    double X0;
    double Y0;
    double x0;
    double y0;
    int i1;
    int j1;
    double x1;
    double y1;
    double x2;
    double y2;
    int ii;
    int jj;
    int gi0;
    int gi1;
    int gi2;

    s = (xin + yin) * F2;
    i = dg_simplex_fast_floor(xin + s);
    j = dg_simplex_fast_floor(yin + s);

    t = (double)(i + j) * G2;
    X0 = (double)i - t;
    Y0 = (double)j - t;
    x0 = xin - X0;
    y0 = yin - Y0;

    if (x0 > y0) {
        i1 = 1;
        j1 = 0;
    } else {
        i1 = 0;
        j1 = 1;
    }

    x1 = x0 - (double)i1 + G2;
    y1 = y0 - (double)j1 + G2;
    x2 = x0 - 1.0 + 2.0 * G2;
    y2 = y0 - 1.0 + 2.0 * G2;

    ii = i & 255;
    jj = j & 255;
    gi0 = perm[ii + perm[jj]] % 12;
    gi1 = perm[ii + i1 + perm[jj + j1]] % 12;
    gi2 = perm[ii + 1 + perm[jj + 1]] % 12;

    t = 0.5 - x0 * x0 - y0 * y0;
    if (t < 0.0) {
        n0 = 0.0;
    } else {
        t *= t;
        n0 = t * t * dg_simplex_dot(grad3[gi0], x0, y0);
    }

    t = 0.5 - x1 * x1 - y1 * y1;
    if (t < 0.0) {
        n1 = 0.0;
    } else {
        t *= t;
        n1 = t * t * dg_simplex_dot(grad3[gi1], x1, y1);
    }

    t = 0.5 - x2 * x2 - y2 * y2;
    if (t < 0.0) {
        n2 = 0.0;
    } else {
        t *= t;
        n2 = t * t * dg_simplex_dot(grad3[gi2], x2, y2);
    }

    return 70.0 * (n0 + n1 + n2);
}

dg_status_t dg_generate_simplex_noise_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    const dg_simplex_noise_config_t *config;
    size_t cell_count;
    double *accum;
    double amplitude;
    double total_amplitude;
    double frequency;
    double persistence;
    double threshold;
    uint8_t perm[512];
    int octave;
    int x;
    int y;
    dg_status_t status;

    if (request == NULL || map == NULL || map->tiles == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    config = &request->params.simplex_noise;
    dg_simplex_build_perm_table(rng, perm);

    cell_count = (size_t)map->width * (size_t)map->height;
    accum = (double *)calloc(cell_count, sizeof(*accum));
    if (accum == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    amplitude = 1.0;
    total_amplitude = 0.0;
    frequency = 1.0 / (double)config->feature_size;
    persistence = (double)config->persistence_percent / 100.0;

    for (octave = 0; octave < config->octaves; ++octave) {
        for (y = 0; y < map->height; ++y) {
            for (x = 0; x < map->width; ++x) {
                size_t index;
                double sample;
                double normalized_sample;

                index = dg_tile_index(map, x, y);
                sample = dg_simplex_noise2d((double)x * frequency, (double)y * frequency, perm);
                normalized_sample = (sample + 1.0) * 0.5;
                if (normalized_sample < 0.0) {
                    normalized_sample = 0.0;
                } else if (normalized_sample > 1.0) {
                    normalized_sample = 1.0;
                }
                accum[index] += normalized_sample * amplitude;
            }
        }

        total_amplitude += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    if (total_amplitude <= 0.0) {
        total_amplitude = 1.0;
    }

    threshold = (double)config->floor_threshold_percent / 100.0;
    for (y = 0; y < map->height; ++y) {
        for (x = 0; x < map->width; ++x) {
            size_t index = dg_tile_index(map, x, y);
            double normalized = accum[index] / total_amplitude;
            if (normalized >= threshold) {
                (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
            }
        }
    }

    free(accum);

    if (dg_count_walkable_tiles(map) == 0u) {
        int cx = map->width / 2;
        int cy = map->height / 2;
        (void)dg_map_set_tile(map, cx, cy, DG_TILE_FLOOR);
    }

    if (config->ensure_connected != 0) {
        status = dg_enforce_single_connected_region(map);
        if (status != DG_STATUS_OK) {
            return status;
        }
    }

    if (dg_count_walkable_tiles(map) == 0u) {
        return DG_STATUS_GENERATION_FAILED;
    }

    return DG_STATUS_OK;
}
