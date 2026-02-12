#ifndef DUNGEONEER_GENERATOR_INTERNAL_H
#define DUNGEONEER_GENERATOR_INTERNAL_H

#include "dungeoneer/generator.h"

typedef struct dg_connectivity_stats {
    size_t walkable_count;
    size_t component_count;
    size_t largest_component_size;
    bool connected_floor;
} dg_connectivity_stats_t;

int dg_min_int(int a, int b);
int dg_max_int(int a, int b);
int dg_clamp_int(int value, int min_value, int max_value);

bool dg_is_walkable_tile(dg_tile_t tile);
size_t dg_tile_index(const dg_map_t *map, int x, int y);

bool dg_rect_is_valid(const dg_rect_t *rect);
bool dg_rects_overlap(const dg_rect_t *a, const dg_rect_t *b);
bool dg_rects_overlap_with_padding(const dg_rect_t *a, const dg_rect_t *b, int padding);

void dg_paint_outer_walls(dg_map_t *map);
bool dg_has_outer_walls(const dg_map_t *map);
void dg_carve_brush(dg_map_t *map, int cx, int cy, int radius, dg_tile_t tile);

size_t dg_count_walkable_tiles(const dg_map_t *map);
dg_status_t dg_enforce_single_connected_region(dg_map_t *map);
dg_status_t dg_analyze_connectivity(const dg_map_t *map, dg_connectivity_stats_t *out_stats);
dg_status_t dg_analyze_connectivity_for_request(
    const dg_map_t *map,
    dg_generation_perspective_t perspective,
    const dg_traversal_constraints_t *traversal,
    dg_connectivity_stats_t *out_stats
);
dg_status_t dg_enforce_side_view_grounded_connectivity(
    dg_map_t *map,
    const dg_traversal_constraints_t *traversal
);
dg_status_t dg_smooth_walkable_regions(
    dg_map_t *map,
    int smoothing_passes,
    int inner_enabled,
    int outer_enabled
);

dg_status_t dg_populate_runtime_metadata(
    dg_map_t *map,
    uint64_t seed,
    int algorithm_id,
    dg_map_generation_class_t generation_class,
    dg_generation_perspective_t perspective,
    const dg_traversal_constraints_t *traversal,
    size_t generation_attempts
);

void dg_init_empty_map(dg_map_t *map);

dg_status_t dg_generate_bsp_tree_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
);
dg_status_t dg_generate_drunkards_walk_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
);
dg_status_t dg_generate_rooms_and_mazes_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
);
dg_status_t dg_apply_post_processes(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
);
dg_status_t dg_apply_room_type_assignment(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
);

#endif
