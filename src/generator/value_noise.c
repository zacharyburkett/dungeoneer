#include "internal.h"

#include <stdint.h>
#include <stdlib.h>

static int dg_max_int_local(int a, int b)
{
    return (a > b) ? a : b;
}

static double dg_lerp_double(double a, double b, double t)
{
    return a + (b - a) * t;
}

static double dg_sample_value_noise(
    const double *lattice,
    int lattice_width,
    int gx,
    int gy,
    double fx,
    double fy
)
{
    double v00 = lattice[gy * lattice_width + gx];
    double v10 = lattice[gy * lattice_width + (gx + 1)];
    double v01 = lattice[(gy + 1) * lattice_width + gx];
    double v11 = lattice[(gy + 1) * lattice_width + (gx + 1)];
    double ix0 = dg_lerp_double(v00, v10, fx);
    double ix1 = dg_lerp_double(v01, v11, fx);

    return dg_lerp_double(ix0, ix1, fy);
}

dg_status_t dg_generate_value_noise_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    const dg_value_noise_config_t *config;
    dg_status_t status;
    size_t cell_count;
    double *accum;
    double total_amplitude;
    double amplitude;
    double persistence;
    double threshold;
    int octave;
    int x;
    int y;

    if (request == NULL || map == NULL || map->tiles == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    config = &request->params.value_noise;

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    cell_count = (size_t)map->width * (size_t)map->height;
    accum = (double *)calloc(cell_count, sizeof(*accum));
    if (accum == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    total_amplitude = 0.0;
    amplitude = 1.0;
    persistence = (double)config->persistence_percent / 100.0;

    for (octave = 0; octave < config->octaves; ++octave) {
        int cell_size;
        int lattice_width;
        int lattice_height;
        size_t lattice_count;
        double *lattice;

        cell_size = config->feature_size >> octave;
        cell_size = dg_max_int_local(cell_size, 1);

        lattice_width = (map->width / cell_size) + 3;
        lattice_height = (map->height / cell_size) + 3;
        lattice_count = (size_t)lattice_width * (size_t)lattice_height;
        lattice = (double *)malloc(lattice_count * sizeof(*lattice));
        if (lattice == NULL) {
            free(accum);
            return DG_STATUS_ALLOCATION_FAILED;
        }

        for (y = 0; y < lattice_height; ++y) {
            for (x = 0; x < lattice_width; ++x) {
                uint32_t rv = dg_rng_next_u32(rng);
                lattice[y * lattice_width + x] = (double)rv / (double)UINT32_MAX;
            }
        }

        for (y = 0; y < map->height; ++y) {
            for (x = 0; x < map->width; ++x) {
                int gx = x / cell_size;
                int gy = y / cell_size;
                double fx = (double)(x % cell_size) / (double)cell_size;
                double fy = (double)(y % cell_size) / (double)cell_size;
                double sample =
                    dg_sample_value_noise(lattice, lattice_width, gx, gy, fx, fy);

                accum[dg_tile_index(map, x, y)] += sample * amplitude;
            }
        }

        free(lattice);
        total_amplitude += amplitude;
        amplitude *= persistence;
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

    status = dg_enforce_single_connected_region(map);
    if (status != DG_STATUS_OK) {
        return status;
    }

    if (dg_count_walkable_tiles(map) == 0u) {
        return DG_STATUS_GENERATION_FAILED;
    }

    return DG_STATUS_OK;
}
