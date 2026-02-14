#include "internal.h"

#include <limits.h>
#include <stdlib.h>

static void dg_clear_process_step_diagnostics(dg_map_t *map)
{
    if (map == NULL) {
        return;
    }

    free(map->metadata.diagnostics.process_steps);
    map->metadata.diagnostics.process_steps = NULL;
    map->metadata.diagnostics.process_step_count = 0;
}

static bool dg_mul_size_checked(size_t a, size_t b, size_t *out)
{
    if (out == NULL) {
        return false;
    }

    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }

    if (a > (SIZE_MAX / b)) {
        return false;
    }

    *out = a * b;
    return true;
}

static bool dg_mul_int_checked(int value, int factor, int *out)
{
    if (out == NULL || value < 0 || factor < 0) {
        return false;
    }

    if (factor == 0) {
        *out = 0;
        return true;
    }

    if (value > (INT_MAX / factor)) {
        return false;
    }

    *out = value * factor;
    return true;
}

static bool dg_point_in_rect(const dg_rect_t *rect, int x, int y)
{
    if (rect == NULL) {
        return false;
    }

    return x >= rect->x &&
           y >= rect->y &&
           x < rect->x + rect->width &&
           y < rect->y + rect->height;
}

static bool dg_point_in_any_room_bounds(const dg_map_t *map, int x, int y)
{
    size_t i;

    if (map == NULL || map->metadata.rooms == NULL) {
        return false;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_rect_t *room = &map->metadata.rooms[i].bounds;

        if (x >= room->x &&
            y >= room->y &&
            x < room->x + room->width &&
            y < room->y + room->height) {
            return true;
        }
    }

    return false;
}

static bool dg_is_corridor_floor(const dg_map_t *map, int x, int y)
{
    if (map == NULL || !dg_map_in_bounds(map, x, y)) {
        return false;
    }

    return dg_is_walkable_tile(dg_map_get_tile(map, x, y)) &&
           !dg_point_in_any_room_bounds(map, x, y);
}

static bool dg_is_corridor_border_wall_candidate(const dg_map_t *map, int x, int y)
{
    static const int k_dirs[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    int d;

    if (map == NULL || !dg_map_in_bounds(map, x, y)) {
        return false;
    }

    if (dg_map_get_tile(map, x, y) != DG_TILE_WALL) {
        return false;
    }

    if (dg_point_in_any_room_bounds(map, x, y)) {
        return false;
    }

    for (d = 0; d < 4; ++d) {
        int nx = x + k_dirs[d][0];
        int ny = y + k_dirs[d][1];

        if (dg_is_corridor_floor(map, nx, ny)) {
            return true;
        }
    }

    return false;
}

static dg_status_t dg_apply_corridor_roughen_pass(
    dg_map_t *map,
    int strength,
    dg_corridor_roughen_mode_t mode,
    dg_rng_t *rng,
    size_t *out_carved_count
)
{
    unsigned char *candidate_mask;
    unsigned char *random_field;
    size_t carved_count;
    size_t candidate_count;
    size_t cell_count;
    int x;
    int y;

    if (map == NULL || map->tiles == NULL || rng == NULL || out_carved_count == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_carved_count = 0;
    if (strength < 0 || strength > 100) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (mode != DG_CORRIDOR_ROUGHEN_UNIFORM && mode != DG_CORRIDOR_ROUGHEN_ORGANIC) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (strength == 0 || map->width < 3 || map->height < 3) {
        return DG_STATUS_OK;
    }

    carved_count = 0;
    cell_count = (size_t)map->width * (size_t)map->height;
    candidate_mask = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
    if (candidate_mask == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    candidate_count = 0;
    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            size_t index;

            if (!dg_is_corridor_border_wall_candidate(map, x, y)) {
                continue;
            }

            index = dg_tile_index(map, x, y);
            candidate_mask[index] = 1u;
            candidate_count += 1;
        }
    }

    if (candidate_count == 0) {
        free(candidate_mask);
        return DG_STATUS_OK;
    }

    if (mode == DG_CORRIDOR_ROUGHEN_UNIFORM) {
        for (y = 1; y < map->height - 1; ++y) {
            for (x = 1; x < map->width - 1; ++x) {
                size_t index = dg_tile_index(map, x, y);

                if (candidate_mask[index] == 0u) {
                    continue;
                }

                if (dg_rng_range(rng, 0, 99) < strength) {
                    map->tiles[index] = DG_TILE_FLOOR;
                    carved_count += 1;
                }
            }
        }

        free(candidate_mask);
        *out_carved_count = carved_count;
        return DG_STATUS_OK;
    }

    random_field = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
    if (random_field == NULL) {
        free(candidate_mask);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            size_t index = dg_tile_index(map, x, y);

            if (candidate_mask[index] == 0u) {
                continue;
            }

            random_field[index] = (unsigned char)dg_rng_range(rng, 0, 100);
        }
    }

    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            static const int k_dirs[4][2] = {
                {1, 0},
                {-1, 0},
                {0, 1},
                {0, -1}
            };
            size_t index = dg_tile_index(map, x, y);
            int dy;
            int dx;
            int sum;
            int weight;
            int corridor_neighbors;
            int threshold;
            int averaged;
            int d;

            if (candidate_mask[index] == 0u) {
                continue;
            }

            sum = (int)random_field[index] * 3;
            weight = 3;

            for (dy = -1; dy <= 1; ++dy) {
                for (dx = -1; dx <= 1; ++dx) {
                    size_t nindex;

                    if (dx == 0 && dy == 0) {
                        continue;
                    }

                    nindex = dg_tile_index(map, x + dx, y + dy);
                    if (candidate_mask[nindex] != 0u) {
                        sum += (int)random_field[nindex];
                        weight += 1;
                    }
                }
            }

            corridor_neighbors = 0;
            for (d = 0; d < 4; ++d) {
                int nx = x + k_dirs[d][0];
                int ny = y + k_dirs[d][1];

                if (dg_is_corridor_floor(map, nx, ny)) {
                    corridor_neighbors += 1;
                }
            }

            averaged = sum / weight;
            threshold = strength + (corridor_neighbors * 8) + dg_rng_range(rng, -8, 8);
            threshold = dg_clamp_int(threshold, 0, 100);

            if (averaged <= threshold) {
                map->tiles[index] = DG_TILE_FLOOR;
                carved_count += 1;
            }
        }
    }

    free(random_field);
    free(candidate_mask);
    *out_carved_count = carved_count;
    return DG_STATUS_OK;
}

static dg_status_t dg_apply_corridor_roughen(
    dg_map_t *map,
    int strength,
    int max_depth,
    dg_corridor_roughen_mode_t mode,
    dg_rng_t *rng
)
{
    int depth;

    if (map == NULL || map->tiles == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (max_depth < 1 || max_depth > 32) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (depth = 0; depth < max_depth; ++depth) {
        size_t carved_count;
        dg_status_t status;

        status = dg_apply_corridor_roughen_pass(
            map,
            strength,
            mode,
            rng,
            &carved_count
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
        if (carved_count == 0) {
            break;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_scale_map_tiles(dg_map_t *map, int factor)
{
    int new_width;
    int new_height;
    size_t old_cell_count;
    size_t new_cell_count;
    dg_tile_t *scaled_tiles;
    int x;
    int y;

    if (map == NULL || map->tiles == NULL || factor < 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (factor == 1) {
        return DG_STATUS_OK;
    }

    if (!dg_mul_int_checked(map->width, factor, &new_width) ||
        !dg_mul_int_checked(map->height, factor, &new_height)) {
        return DG_STATUS_GENERATION_FAILED;
    }

    if (!dg_mul_size_checked((size_t)new_width, (size_t)new_height, &new_cell_count)) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    if (new_cell_count > (SIZE_MAX / sizeof(dg_tile_t))) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    scaled_tiles = (dg_tile_t *)malloc(new_cell_count * sizeof(dg_tile_t));
    if (scaled_tiles == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    old_cell_count = (size_t)map->width * (size_t)map->height;
    for (y = 0; y < map->height; ++y) {
        for (x = 0; x < map->width; ++x) {
            size_t src_index;
            dg_tile_t tile;
            int dx;
            int dy;

            src_index = (size_t)y * (size_t)map->width + (size_t)x;
            if (src_index >= old_cell_count) {
                free(scaled_tiles);
                return DG_STATUS_GENERATION_FAILED;
            }

            tile = map->tiles[src_index];
            for (dy = 0; dy < factor; ++dy) {
                int scaled_y = y * factor + dy;
                for (dx = 0; dx < factor; ++dx) {
                    int scaled_x = x * factor + dx;
                    size_t dst_index = (size_t)scaled_y * (size_t)new_width + (size_t)scaled_x;
                    scaled_tiles[dst_index] = tile;
                }
            }
        }
    }

    free(map->tiles);
    map->tiles = scaled_tiles;
    map->width = new_width;
    map->height = new_height;
    return DG_STATUS_OK;
}

static dg_status_t dg_scale_map_metadata(dg_map_t *map, int factor)
{
    size_t i;

    if (map == NULL || factor < 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (factor == 1) {
        return DG_STATUS_OK;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        dg_room_metadata_t *room = &map->metadata.rooms[i];

        if (!dg_mul_int_checked(room->bounds.x, factor, &room->bounds.x) ||
            !dg_mul_int_checked(room->bounds.y, factor, &room->bounds.y) ||
            !dg_mul_int_checked(room->bounds.width, factor, &room->bounds.width) ||
            !dg_mul_int_checked(room->bounds.height, factor, &room->bounds.height)) {
            return DG_STATUS_GENERATION_FAILED;
        }
    }

    for (i = 0; i < map->metadata.corridor_count; ++i) {
        dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];

        if (!dg_mul_int_checked(corridor->width, factor, &corridor->width) ||
            !dg_mul_int_checked(corridor->length, factor, &corridor->length)) {
            return DG_STATUS_GENERATION_FAILED;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_scale_map(dg_map_t *map, int factor)
{
    dg_status_t status;

    if (map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_scale_map_metadata(map, factor);
    if (status != DG_STATUS_OK) {
        return status;
    }

    return dg_scale_map_tiles(map, factor);
}

typedef struct dg_room_entrance_point {
    dg_point_t point;
    int inward_x;
    int inward_y;
} dg_room_entrance_point_t;

static size_t dg_collect_room_entrances(
    const dg_map_t *map,
    const dg_rect_t *room,
    dg_room_entrance_point_t *out_points,
    size_t out_capacity
)
{
    static const int k_dirs[4][2] = {
        {0, -1},
        {1, 0},
        {0, 1},
        {-1, 0}
    };
    size_t count;
    size_t room_area;
    unsigned char *candidate_mask;
    signed char *candidate_normal_x;
    signed char *candidate_normal_y;
    int *queue;
    int center_x;
    int center_y;
    int local_x;
    int local_y;

    if (map == NULL || room == NULL || out_points == NULL ||
        room->width <= 0 || room->height <= 0) {
        return 0;
    }
    if ((size_t)room->width > (SIZE_MAX / (size_t)room->height)) {
        return 0;
    }
    room_area = (size_t)room->width * (size_t)room->height;
    if (room_area == 0u) {
        return 0;
    }

    candidate_mask = (unsigned char *)calloc(room_area, sizeof(unsigned char));
    candidate_normal_x = (signed char *)calloc(room_area, sizeof(signed char));
    candidate_normal_y = (signed char *)calloc(room_area, sizeof(signed char));
    queue = (int *)malloc(room_area * sizeof(int));
    if (candidate_mask == NULL || candidate_normal_x == NULL || candidate_normal_y == NULL || queue == NULL) {
        free(candidate_mask);
        free(candidate_normal_x);
        free(candidate_normal_y);
        free(queue);
        return 0;
    }

    for (local_y = 0; local_y < room->height; ++local_y) {
        for (local_x = 0; local_x < room->width; ++local_x) {
            int x = room->x + local_x;
            int y = room->y + local_y;
            int d;

            if (!dg_is_walkable_tile(dg_map_get_tile(map, x, y))) {
                continue;
            }

            for (d = 0; d < 4; ++d) {
                int nx = x + k_dirs[d][0];
                int ny = y + k_dirs[d][1];
                size_t index;

                if (!dg_map_in_bounds(map, nx, ny)) {
                    continue;
                }
                if (dg_point_in_rect(room, nx, ny) || dg_point_in_any_room_bounds(map, nx, ny)) {
                    continue;
                }
                if (!dg_is_walkable_tile(dg_map_get_tile(map, nx, ny))) {
                    continue;
                }

                index = (size_t)local_y * (size_t)room->width + (size_t)local_x;
                candidate_mask[index] = 1u;
                candidate_normal_x[index] = (signed char)k_dirs[d][0];
                candidate_normal_y[index] = (signed char)k_dirs[d][1];
                break;
            }
        }
    }

    count = 0;
    center_x = room->x + room->width / 2;
    center_y = room->y + room->height / 2;
    for (local_y = 0; local_y < room->height; ++local_y) {
        for (local_x = 0; local_x < room->width; ++local_x) {
            size_t seed = (size_t)local_y * (size_t)room->width + (size_t)local_x;

            if (candidate_mask[seed] == 0u) {
                continue;
            }

            {
                int head;
                int tail;
                int best_x;
                int best_y;
                int best_metric;
                int best_inward_x;
                int best_inward_y;

                candidate_mask[seed] = 0u;
                head = 0;
                tail = 0;
                queue[tail++] = (int)seed;

                best_x = room->x + local_x;
                best_y = room->y + local_y;
                best_metric = abs(best_x - center_x) + abs(best_y - center_y);
                best_inward_x = -candidate_normal_x[seed];
                best_inward_y = -candidate_normal_y[seed];

                while (head < tail) {
                    int node = queue[head++];
                    int cx = node % room->width;
                    int cy = node / room->width;
                    int gx = room->x + cx;
                    int gy = room->y + cy;
                    int metric = abs(gx - center_x) + abs(gy - center_y);
                    int d;

                    if (metric < best_metric ||
                        (metric == best_metric &&
                         (gy < best_y || (gy == best_y && gx < best_x)))) {
                        best_x = gx;
                        best_y = gy;
                        best_metric = metric;
                        best_inward_x = -candidate_normal_x[(size_t)node];
                        best_inward_y = -candidate_normal_y[(size_t)node];
                    }

                    for (d = 0; d < 4; ++d) {
                        int nx = cx + k_dirs[d][0];
                        int ny = cy + k_dirs[d][1];
                        size_t neighbor;

                        if (nx < 0 || ny < 0 || nx >= room->width || ny >= room->height) {
                            continue;
                        }

                        neighbor = (size_t)ny * (size_t)room->width + (size_t)nx;
                        if (candidate_mask[neighbor] == 0u) {
                            continue;
                        }

                        candidate_mask[neighbor] = 0u;
                        queue[tail++] = (int)neighbor;
                    }
                }

                if (count < out_capacity) {
                    out_points[count].point = (dg_point_t){best_x, best_y};
                    out_points[count].inward_x = best_inward_x;
                    out_points[count].inward_y = best_inward_y;
                    count += 1u;
                }
            }
        }
    }

    free(candidate_mask);
    free(candidate_normal_x);
    free(candidate_normal_y);
    free(queue);
    return count;
}

static uint32_t dg_hash_mix_u32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static uint32_t dg_hash_noise_coords(uint32_t seed, int x, int y)
{
    uint32_t value;

    value = seed ^ ((uint32_t)x * 0x1f123bb5u) ^ ((uint32_t)y * 0x5f356495u);
    return dg_hash_mix_u32(value);
}

static double dg_lerp_double(double a, double b, double t)
{
    return a + (b - a) * t;
}

static double dg_value_noise_2d(uint32_t seed, int x, int y, int cell_size)
{
    int gx;
    int gy;
    int rx;
    int ry;
    double tx;
    double ty;
    double v00;
    double v10;
    double v01;
    double v11;
    double vx0;
    double vx1;

    if (cell_size < 1) {
        cell_size = 1;
    }

    gx = x / cell_size;
    gy = y / cell_size;
    rx = x % cell_size;
    ry = y % cell_size;
    tx = (double)rx / (double)cell_size;
    ty = (double)ry / (double)cell_size;

    v00 = (double)(dg_hash_noise_coords(seed, gx, gy) & 0x00ffffffu) / 16777215.0;
    v10 = (double)(dg_hash_noise_coords(seed, gx + 1, gy) & 0x00ffffffu) / 16777215.0;
    v01 = (double)(dg_hash_noise_coords(seed, gx, gy + 1) & 0x00ffffffu) / 16777215.0;
    v11 = (double)(dg_hash_noise_coords(seed, gx + 1, gy + 1) & 0x00ffffffu) / 16777215.0;

    vx0 = dg_lerp_double(v00, v10, tx);
    vx1 = dg_lerp_double(v01, v11, tx);
    return dg_lerp_double(vx0, vx1, ty);
}

static double dg_fbm_noise_2d(uint32_t seed, int x, int y, int base_cell_size, int octaves)
{
    int octave;
    int cell_size;
    double value;
    double amplitude;
    double amplitude_sum;

    if (base_cell_size < 1) {
        base_cell_size = 1;
    }
    if (octaves < 1) {
        octaves = 1;
    }

    value = 0.0;
    amplitude = 1.0;
    amplitude_sum = 0.0;
    for (octave = 0; octave < octaves; ++octave) {
        double sample;

        cell_size = base_cell_size >> octave;
        if (cell_size < 1) {
            cell_size = 1;
        }

        sample = dg_value_noise_2d(seed + (uint32_t)(octave * 92821), x, y, cell_size);
        value += sample * amplitude;
        amplitude_sum += amplitude;
        amplitude *= 0.55;
    }

    if (amplitude_sum <= 0.0) {
        return 0.0;
    }
    return value / amplitude_sum;
}

static void dg_fill_room_keep_mask(unsigned char *keep_mask, int width, int height, unsigned char value)
{
    int x;
    int y;

    if (keep_mask == NULL || width <= 0 || height <= 0) {
        return;
    }

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            keep_mask[(size_t)y * (size_t)width + (size_t)x] = value;
        }
    }
}

static dg_status_t dg_build_organic_room_keep_mask(
    const dg_rect_t *room,
    int organicity,
    dg_rng_t *rng,
    unsigned char *keep_mask
)
{
    uint32_t noise_seed;
    int base_cell;
    double strength;
    double cx;
    double cy;
    double rx;
    double ry;
    int x;
    int y;

    if (room == NULL || rng == NULL || keep_mask == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    strength = (double)dg_clamp_int(organicity, 0, 100) / 100.0;
    cx = ((double)room->width - 1.0) * 0.5;
    cy = ((double)room->height - 1.0) * 0.5;
    rx = dg_max_int(room->width - 1, 1) * 0.5;
    ry = dg_max_int(room->height - 1, 1) * 0.5;
    noise_seed = dg_rng_next_u32(rng);
    base_cell = dg_clamp_int(dg_min_int(room->width, room->height) / 2, 2, 12);

    dg_fill_room_keep_mask(keep_mask, room->width, room->height, 0u);
    for (y = 0; y < room->height; ++y) {
        for (x = 0; x < room->width; ++x) {
            double nx;
            double ny;
            double ellipse;
            double noise;
            double perturbation;
            double threshold;
            size_t index;

            nx = ((double)x - cx) / rx;
            ny = ((double)y - cy) / ry;
            ellipse = nx * nx + ny * ny;
            noise = dg_fbm_noise_2d(noise_seed, x, y, base_cell, 3);
            perturbation = (noise - 0.5) * (0.25 + 0.55 * strength);
            threshold = 1.0 - (0.08 * strength);
            index = (size_t)y * (size_t)room->width + (size_t)x;
            if (ellipse + perturbation <= threshold) {
                keep_mask[index] = 1u;
            }
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_build_cellular_room_keep_mask(
    const dg_rect_t *room,
    int organicity,
    dg_rng_t *rng,
    unsigned char *keep_mask
)
{
    size_t room_area;
    unsigned char *scratch;
    unsigned char *current;
    unsigned char *next;
    double strength;
    double cx;
    double cy;
    double rx;
    double ry;
    int steps;
    int step;
    int x;
    int y;

    if (room == NULL || rng == NULL || keep_mask == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (!dg_mul_size_checked((size_t)room->width, (size_t)room->height, &room_area)) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    scratch = (unsigned char *)calloc(room_area, sizeof(unsigned char));
    if (scratch == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    strength = (double)dg_clamp_int(organicity, 0, 100) / 100.0;
    cx = ((double)room->width - 1.0) * 0.5;
    cy = ((double)room->height - 1.0) * 0.5;
    rx = dg_max_int(room->width - 1, 1) * 0.5;
    ry = dg_max_int(room->height - 1, 1) * 0.5;
    dg_fill_room_keep_mask(keep_mask, room->width, room->height, 0u);

    for (y = 0; y < room->height; ++y) {
        for (x = 0; x < room->width; ++x) {
            size_t index;
            double nx;
            double ny;
            double ellipse;
            int base_open;
            int center_bonus;
            int chance;

            nx = ((double)x - cx) / rx;
            ny = ((double)y - cy) / ry;
            ellipse = nx * nx + ny * ny;

            base_open = 68 - (int)(strength * 24.0);
            center_bonus = (int)((1.2 - ellipse) * 22.0);
            chance = base_open + center_bonus + dg_rng_range(rng, -12, 12);
            chance = dg_clamp_int(chance, 8, 95);

            index = (size_t)y * (size_t)room->width + (size_t)x;
            keep_mask[index] = (dg_rng_range(rng, 0, 99) < chance) ? 1u : 0u;
        }
    }

    steps = 2 + (dg_clamp_int(organicity, 0, 100) / 30);
    current = keep_mask;
    next = scratch;
    for (step = 0; step < steps; ++step) {
        for (y = 0; y < room->height; ++y) {
            for (x = 0; x < room->width; ++x) {
                int dx;
                int dy;
                int neighbors;
                double nx;
                double ny;
                double ellipse;
                size_t index;

                neighbors = 0;
                for (dy = -1; dy <= 1; ++dy) {
                    for (dx = -1; dx <= 1; ++dx) {
                        int sx;
                        int sy;

                        if (dx == 0 && dy == 0) {
                            continue;
                        }

                        sx = x + dx;
                        sy = y + dy;
                        if (sx < 0 || sy < 0 || sx >= room->width || sy >= room->height) {
                            continue;
                        }
                        if (current[(size_t)sy * (size_t)room->width + (size_t)sx] != 0u) {
                            neighbors += 1;
                        }
                    }
                }

                index = (size_t)y * (size_t)room->width + (size_t)x;
                nx = ((double)x - cx) / rx;
                ny = ((double)y - cy) / ry;
                ellipse = nx * nx + ny * ny;
                if (ellipse < 0.16) {
                    next[index] = 1u;
                } else if (current[index] != 0u) {
                    next[index] = (neighbors >= 3) ? 1u : 0u;
                } else {
                    next[index] = (neighbors >= 5) ? 1u : 0u;
                }
            }
        }

        {
            unsigned char *tmp = current;
            current = next;
            next = tmp;
        }
    }

    if (current != keep_mask) {
        for (y = 0; y < room->height; ++y) {
            for (x = 0; x < room->width; ++x) {
                keep_mask[(size_t)y * (size_t)room->width + (size_t)x] =
                    current[(size_t)y * (size_t)room->width + (size_t)x];
            }
        }
    }

    free(scratch);
    return DG_STATUS_OK;
}

static dg_status_t dg_build_chamfer_room_keep_mask(
    const dg_rect_t *room,
    int organicity,
    unsigned char *keep_mask
)
{
    int radius_max;
    int radius;
    int x;
    int y;

    if (room == NULL || keep_mask == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_fill_room_keep_mask(keep_mask, room->width, room->height, 1u);
    if (room->width < 3 || room->height < 3) {
        return DG_STATUS_OK;
    }

    radius_max = dg_min_int(room->width, room->height) / 3;
    if (radius_max < 1) {
        return DG_STATUS_OK;
    }

    radius = (dg_clamp_int(organicity, 0, 100) * radius_max) / 100;
    if (radius == 0 && organicity > 0) {
        radius = 1;
    }
    if (radius < 1) {
        return DG_STATUS_OK;
    }

    for (y = 0; y < room->height; ++y) {
        for (x = 0; x < room->width; ++x) {
            int remove_tile;

            remove_tile = 0;
            if (x < radius && y < radius) {
                int dx = radius - x;
                int dy = radius - y;
                if (dx * dx + dy * dy > radius * radius) {
                    remove_tile = 1;
                }
            } else if (x >= room->width - radius && y < radius) {
                int local_x = (room->width - 1) - x;
                int dx = radius - local_x;
                int dy = radius - y;
                if (dx * dx + dy * dy > radius * radius) {
                    remove_tile = 1;
                }
            } else if (x < radius && y >= room->height - radius) {
                int local_y = (room->height - 1) - y;
                int dx = radius - x;
                int dy = radius - local_y;
                if (dx * dx + dy * dy > radius * radius) {
                    remove_tile = 1;
                }
            } else if (x >= room->width - radius && y >= room->height - radius) {
                int local_x = (room->width - 1) - x;
                int local_y = (room->height - 1) - y;
                int dx = radius - local_x;
                int dy = radius - local_y;
                if (dx * dx + dy * dy > radius * radius) {
                    remove_tile = 1;
                }
            }

            if (remove_tile != 0) {
                keep_mask[(size_t)y * (size_t)room->width + (size_t)x] = 0u;
            }
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_build_room_keep_mask(
    const dg_rect_t *room,
    dg_room_shape_mode_t mode,
    int organicity,
    dg_rng_t *rng,
    unsigned char *keep_mask
)
{
    if (room == NULL || keep_mask == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    switch (mode) {
    case DG_ROOM_SHAPE_RECTANGULAR:
        dg_fill_room_keep_mask(keep_mask, room->width, room->height, 1u);
        return DG_STATUS_OK;
    case DG_ROOM_SHAPE_ORGANIC:
        return dg_build_organic_room_keep_mask(room, organicity, rng, keep_mask);
    case DG_ROOM_SHAPE_CELLULAR:
        return dg_build_cellular_room_keep_mask(room, organicity, rng, keep_mask);
    case DG_ROOM_SHAPE_CHAMFERED:
        return dg_build_chamfer_room_keep_mask(room, organicity, keep_mask);
    default:
        return DG_STATUS_INVALID_ARGUMENT;
    }
}

static void dg_apply_room_keep_mask(
    dg_map_t *map,
    const dg_rect_t *room,
    const unsigned char *keep_mask,
    const dg_room_entrance_point_t *entrances,
    size_t entrance_count
)
{
    dg_point_t anchor;
    int x;
    int y;
    size_t i;

    if (map == NULL || room == NULL || keep_mask == NULL) {
        return;
    }

    for (y = room->y; y < room->y + room->height; ++y) {
        for (x = room->x; x < room->x + room->width; ++x) {
            map->tiles[dg_tile_index(map, x, y)] = DG_TILE_WALL;
        }
    }

    for (y = 0; y < room->height; ++y) {
        for (x = 0; x < room->width; ++x) {
            size_t index = (size_t)y * (size_t)room->width + (size_t)x;
            if (keep_mask[index] != 0u) {
                map->tiles[dg_tile_index(map, room->x + x, room->y + y)] = DG_TILE_FLOOR;
            }
        }
    }

    anchor.x = room->x + room->width / 2;
    anchor.y = room->y + room->height / 2;
    if (!dg_is_walkable_tile(dg_map_get_tile(map, anchor.x, anchor.y))) {
        int best_dist;
        bool found;

        best_dist = INT_MAX;
        found = false;
        for (y = 0; y < room->height; ++y) {
            for (x = 0; x < room->width; ++x) {
                int tx;
                int ty;
                int dist;
                size_t index;

                index = (size_t)y * (size_t)room->width + (size_t)x;
                if (keep_mask[index] == 0u) {
                    continue;
                }
                tx = room->x + x;
                ty = room->y + y;
                dist = abs(anchor.x - tx) + abs(anchor.y - ty);
                if (!found || dist < best_dist) {
                    anchor.x = tx;
                    anchor.y = ty;
                    best_dist = dist;
                    found = true;
                }
            }
        }
    }
    map->tiles[dg_tile_index(map, anchor.x, anchor.y)] = DG_TILE_FLOOR;

    for (i = 0; i < entrance_count; ++i) {
        int depth;
        int sx;
        int sy;

        map->tiles[dg_tile_index(map, entrances[i].point.x, entrances[i].point.y)] = DG_TILE_FLOOR;
        depth = dg_min_int(2, dg_max_int(1, dg_min_int(room->width, room->height) / 2));
        for (x = 1; x <= depth; ++x) {
            int tx = entrances[i].point.x + entrances[i].inward_x * x;
            int ty = entrances[i].point.y + entrances[i].inward_y * x;

            if (!dg_point_in_rect(room, tx, ty)) {
                break;
            }
            map->tiles[dg_tile_index(map, tx, ty)] = DG_TILE_FLOOR;

            if (x == 1) {
                int lateral_x = entrances[i].inward_x != 0 ? 0 : 1;
                int lateral_y = entrances[i].inward_y != 0 ? 0 : 1;
                int side;

                for (side = -1; side <= 1; side += 2) {
                    int sx_lateral = tx + lateral_x * side;
                    int sy_lateral = ty + lateral_y * side;
                    if (dg_point_in_rect(room, sx_lateral, sy_lateral)) {
                        map->tiles[dg_tile_index(map, sx_lateral, sy_lateral)] = DG_TILE_FLOOR;
                    }
                }
            }
        }

        sx = entrances[i].point.x;
        sy = entrances[i].point.y;
        while (sx != anchor.x) {
            sx += (anchor.x > sx) ? 1 : -1;
            if (dg_point_in_rect(room, sx, sy)) {
                map->tiles[dg_tile_index(map, sx, sy)] = DG_TILE_FLOOR;
            }
        }
        while (sy != anchor.y) {
            sy += (anchor.y > sy) ? 1 : -1;
            if (dg_point_in_rect(room, sx, sy)) {
                map->tiles[dg_tile_index(map, sx, sy)] = DG_TILE_FLOOR;
            }
        }
    }
}

static dg_status_t dg_carve_room_with_shape_mode(
    dg_map_t *map,
    const dg_rect_t *room,
    dg_room_shape_mode_t mode,
    int organicity,
    dg_rng_t *rng
)
{
    size_t room_area;
    unsigned char *keep_mask;
    dg_room_entrance_point_t *entrances;
    size_t entrance_count;
    dg_status_t status;

    if (map == NULL || room == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }
    if (room->width <= 0 || room->height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (mode == DG_ROOM_SHAPE_RECTANGULAR) {
        return DG_STATUS_OK;
    }

    if (!dg_mul_size_checked((size_t)room->width, (size_t)room->height, &room_area)) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    keep_mask = (unsigned char *)calloc(room_area, sizeof(unsigned char));
    entrances = (dg_room_entrance_point_t *)malloc(room_area * sizeof(*entrances));
    if (keep_mask == NULL || entrances == NULL) {
        free(keep_mask);
        free(entrances);
        return DG_STATUS_ALLOCATION_FAILED;
    }

    entrance_count = dg_collect_room_entrances(map, room, entrances, room_area);
    status = dg_build_room_keep_mask(room, mode, organicity, rng, keep_mask);
    if (status != DG_STATUS_OK) {
        free(keep_mask);
        free(entrances);
        return status;
    }

    dg_apply_room_keep_mask(map, room, keep_mask, entrances, entrance_count);

    free(keep_mask);
    free(entrances);
    return DG_STATUS_OK;
}

static dg_status_t dg_apply_room_shapes(
    dg_map_t *map,
    dg_room_shape_mode_t mode,
    int organicity,
    dg_rng_t *rng
)
{
    size_t i;

    if (map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (mode == DG_ROOM_SHAPE_RECTANGULAR) {
        return DG_STATUS_OK;
    }

    if (map->metadata.room_count == 0) {
        return DG_STATUS_OK;
    }

    if (map->metadata.rooms == NULL) {
        return DG_STATUS_GENERATION_FAILED;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        dg_status_t status;

        status = dg_carve_room_with_shape_mode(
            map,
            &map->metadata.rooms[i].bounds,
            mode,
            organicity,
            rng
        );
        if (status != DG_STATUS_OK) {
            return status;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_apply_process_method(
    const dg_process_method_t *method,
    dg_map_t *map,
    dg_rng_t *rng,
    dg_map_generation_class_t generation_class
)
{
    if (method == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    switch (method->type) {
    case DG_PROCESS_METHOD_SCALE:
        return dg_scale_map(map, method->params.scale.factor);
    case DG_PROCESS_METHOD_ROOM_SHAPE:
        if (generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE) {
            return dg_apply_room_shapes(
                map,
                method->params.room_shape.mode,
                method->params.room_shape.organicity,
                rng
            );
        }
        return DG_STATUS_OK;
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        return dg_smooth_walkable_regions(
            map,
            method->params.path_smooth.strength,
            method->params.path_smooth.inner_enabled,
            method->params.path_smooth.outer_enabled
        );
    case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
        return dg_apply_corridor_roughen(
            map,
            method->params.corridor_roughen.strength,
            method->params.corridor_roughen.max_depth,
            method->params.corridor_roughen.mode,
            rng
        );
    default:
        return DG_STATUS_INVALID_ARGUMENT;
    }
}

dg_status_t dg_apply_post_processes(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    dg_map_generation_class_t generation_class;
    dg_process_step_diagnostics_t *process_steps;
    size_t i;

    if (request == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_clear_process_step_diagnostics(map);
    if (request->process.enabled == 0 || request->process.method_count == 0) {
        return DG_STATUS_OK;
    }

    if (request->process.method_count > (SIZE_MAX / sizeof(*process_steps))) {
        return DG_STATUS_ALLOCATION_FAILED;
    }
    process_steps = (dg_process_step_diagnostics_t *)calloc(
        request->process.method_count,
        sizeof(*process_steps)
    );
    if (process_steps == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    map->metadata.diagnostics.process_steps = process_steps;
    map->metadata.diagnostics.process_step_count = request->process.method_count;

    generation_class = dg_algorithm_generation_class(request->algorithm);
    for (i = 0; i < request->process.method_count; ++i) {
        dg_connectivity_stats_t before_stats;
        dg_connectivity_stats_t after_stats;
        size_t walkable_before;
        size_t walkable_after;
        dg_process_step_diagnostics_t *step;
        dg_status_t status;
        step = &process_steps[i];
        step->method_type = (int)request->process.methods[i].type;

        status = dg_analyze_connectivity(map, &before_stats);
        if (status != DG_STATUS_OK) {
            dg_clear_process_step_diagnostics(map);
            return status;
        }
        walkable_before = dg_count_walkable_tiles(map);

        status = dg_apply_process_method(
            &request->process.methods[i],
            map,
            rng,
            generation_class
        );
        if (status != DG_STATUS_OK) {
            dg_clear_process_step_diagnostics(map);
            return status;
        }

        status = dg_analyze_connectivity(map, &after_stats);
        if (status != DG_STATUS_OK) {
            dg_clear_process_step_diagnostics(map);
            return status;
        }
        walkable_after = dg_count_walkable_tiles(map);

        step->walkable_before = walkable_before;
        step->walkable_after = walkable_after;
        if (walkable_after >= walkable_before) {
            step->walkable_delta = (int64_t)(walkable_after - walkable_before);
        } else {
            step->walkable_delta = -((int64_t)(walkable_before - walkable_after));
        }

        step->components_before = before_stats.component_count;
        step->components_after = after_stats.component_count;
        if (after_stats.component_count >= before_stats.component_count) {
            step->components_delta =
                (int64_t)(after_stats.component_count - before_stats.component_count);
        } else {
            step->components_delta =
                -((int64_t)(before_stats.component_count - after_stats.component_count));
        }
        step->connected_before = before_stats.connected_floor ? 1 : 0;
        step->connected_after = after_stats.connected_floor ? 1 : 0;
    }

    return DG_STATUS_OK;
}
