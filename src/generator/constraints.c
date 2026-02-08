#include "internal.h"

bool dg_room_overlaps_forbidden_regions(
    const dg_generation_constraints_t *constraints,
    const dg_rect_t *room
)
{
    size_t i;

    if (constraints == NULL || constraints->forbidden_regions == NULL) {
        return false;
    }

    for (i = 0; i < constraints->forbidden_region_count; ++i) {
        const dg_rect_t *region = &constraints->forbidden_regions[i];
        if (!dg_rect_is_valid(region)) {
            continue;
        }
        if (dg_rects_overlap(room, region)) {
            return true;
        }
    }

    return false;
}

void dg_apply_forbidden_regions(const dg_generation_constraints_t *constraints, dg_map_t *map)
{
    size_t i;

    if (constraints == NULL || map == NULL || map->tiles == NULL) {
        return;
    }

    for (i = 0; i < constraints->forbidden_region_count; ++i) {
        const dg_rect_t *region = &constraints->forbidden_regions[i];
        int x0;
        int y0;
        int x1;
        int y1;
        int x;
        int y;

        if (!dg_clamp_region_to_map(map, region, &x0, &y0, &x1, &y1)) {
            continue;
        }

        for (y = y0; y < y1; ++y) {
            for (x = x0; x < x1; ++x) {
                (void)dg_map_set_tile(map, x, y, DG_TILE_WALL);
            }
        }
    }
}

bool dg_forbidden_regions_are_clear(const dg_map_t *map, const dg_generation_constraints_t *constraints)
{
    size_t i;

    if (constraints == NULL || map == NULL || map->tiles == NULL) {
        return false;
    }

    for (i = 0; i < constraints->forbidden_region_count; ++i) {
        const dg_rect_t *region = &constraints->forbidden_regions[i];
        int x0;
        int y0;
        int x1;
        int y1;
        int x;
        int y;

        if (!dg_clamp_region_to_map(map, region, &x0, &y0, &x1, &y1)) {
            continue;
        }

        for (y = y0; y < y1; ++y) {
            for (x = x0; x < x1; ++x) {
                if (dg_is_walkable_tile(dg_map_get_tile(map, x, y))) {
                    return false;
                }
            }
        }
    }

    return true;
}

dg_status_t dg_validate_constraints(const dg_generation_constraints_t *constraints)
{
    if (constraints == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->min_floor_coverage < 0.0f || constraints->min_floor_coverage > 1.0f) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->max_floor_coverage < 0.0f || constraints->max_floor_coverage > 1.0f) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->max_floor_coverage < constraints->min_floor_coverage) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->min_room_count < 0 || constraints->max_room_count < 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->min_special_rooms < 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (
        constraints->max_room_count > 0 &&
        constraints->min_room_count > 0 &&
        constraints->max_room_count < constraints->min_room_count
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->forbidden_region_count > 0 && constraints->forbidden_regions == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (constraints->max_generation_attempts < 1) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

bool dg_constraints_satisfied(const dg_generate_request_t *request, const dg_map_t *map)
{
    const dg_generation_constraints_t *constraints;
    size_t total_cells;
    float floor_coverage;
    const float epsilon = 0.0001f;

    if (request == NULL || map == NULL || map->tiles == NULL) {
        return false;
    }

    constraints = &request->constraints;
    total_cells = (size_t)map->width * (size_t)map->height;
    if (total_cells == 0) {
        return false;
    }

    floor_coverage = (float)map->metadata.walkable_tile_count / (float)total_cells;

    if ((floor_coverage + epsilon) < constraints->min_floor_coverage) {
        return false;
    }

    if ((floor_coverage - epsilon) > constraints->max_floor_coverage) {
        return false;
    }

    if (constraints->min_room_count > 0 && (int)map->metadata.room_count < constraints->min_room_count) {
        return false;
    }

    if (constraints->max_room_count > 0 && (int)map->metadata.room_count > constraints->max_room_count) {
        return false;
    }

    if (constraints->min_special_rooms > 0 &&
        (int)map->metadata.special_room_count < constraints->min_special_rooms) {
        return false;
    }

    if (constraints->require_connected_floor && !map->metadata.connected_floor) {
        return false;
    }

    if (constraints->enforce_outer_walls && !dg_has_outer_walls(map)) {
        return false;
    }

    if (!dg_forbidden_regions_are_clear(map, constraints)) {
        return false;
    }

    return true;
}
