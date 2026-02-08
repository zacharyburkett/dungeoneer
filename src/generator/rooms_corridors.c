#include "internal.h"

static int dg_abs_int(int value)
{
    return value < 0 ? -value : value;
}

static dg_corridor_routing_t dg_normalize_corridor_routing(dg_corridor_routing_t routing)
{
    switch (routing) {
    case DG_CORRIDOR_ROUTING_RANDOM:
    case DG_CORRIDOR_ROUTING_HORIZONTAL_FIRST:
    case DG_CORRIDOR_ROUTING_VERTICAL_FIRST:
        return routing;
    default:
        return DG_CORRIDOR_ROUTING_RANDOM;
    }
}

static void dg_carve_room(dg_map_t *map, const dg_rect_t *room)
{
    int x;
    int y;

    for (y = room->y; y < room->y + room->height; ++y) {
        for (x = room->x; x < room->x + room->width; ++x) {
            (void)dg_map_set_tile(map, x, y, DG_TILE_FLOOR);
        }
    }
}

static bool dg_can_place_room(
    const dg_map_t *map,
    const dg_rect_t *candidate,
    int spacing,
    const dg_generation_constraints_t *constraints
)
{
    size_t i;

    if (candidate->x < 1 || candidate->y < 1) {
        return false;
    }

    if ((long long)candidate->x + (long long)candidate->width > (long long)map->width - 1) {
        return false;
    }

    if ((long long)candidate->y + (long long)candidate->height > (long long)map->height - 1) {
        return false;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        if (dg_rects_overlap_with_padding(candidate, &map->metadata.rooms[i].bounds, spacing)) {
            return false;
        }
    }

    if (dg_room_overlaps_forbidden_regions(constraints, candidate)) {
        return false;
    }

    return true;
}

static dg_point_t dg_room_center(const dg_rect_t *room)
{
    dg_point_t center;
    center.x = room->x + room->width / 2;
    center.y = room->y + room->height / 2;
    return center;
}

static void dg_carve_horizontal_path(dg_map_t *map, int x0, int x1, int y, int corridor_width)
{
    int x;
    int start;
    int end;
    int radius;

    start = dg_min_int(x0, x1);
    end = dg_max_int(x0, x1);
    radius = corridor_width / 2;

    for (x = start; x <= end; ++x) {
        dg_carve_brush(map, x, y, radius, DG_TILE_FLOOR);
    }
}

static void dg_carve_vertical_path(dg_map_t *map, int x, int y0, int y1, int corridor_width)
{
    int y;
    int start;
    int end;
    int radius;

    start = dg_min_int(y0, y1);
    end = dg_max_int(y0, y1);
    radius = corridor_width / 2;

    for (y = start; y <= end; ++y) {
        dg_carve_brush(map, x, y, radius, DG_TILE_FLOOR);
    }
}

static bool dg_route_horizontal_first(dg_rng_t *rng, dg_corridor_routing_t routing)
{
    switch (routing) {
    case DG_CORRIDOR_ROUTING_HORIZONTAL_FIRST:
        return true;
    case DG_CORRIDOR_ROUTING_VERTICAL_FIRST:
        return false;
    case DG_CORRIDOR_ROUTING_RANDOM:
    default:
        return (dg_rng_next_u32(rng) & 1u) != 0u;
    }
}

static void dg_connect_points(
    dg_map_t *map,
    dg_rng_t *rng,
    dg_point_t a,
    dg_point_t b,
    int corridor_width,
    dg_corridor_routing_t routing
)
{
    bool horizontal_first;

    horizontal_first = dg_route_horizontal_first(rng, routing);
    if (horizontal_first) {
        dg_carve_horizontal_path(map, a.x, b.x, a.y, corridor_width);
        dg_carve_vertical_path(map, b.x, a.y, b.y, corridor_width);
    } else {
        dg_carve_vertical_path(map, a.x, a.y, b.y, corridor_width);
        dg_carve_horizontal_path(map, a.x, b.x, b.y, corridor_width);
    }
}

dg_status_t dg_generate_rooms_and_corridors_impl(
    const dg_generate_request_t *request,
    dg_map_t *map,
    dg_rng_t *rng
)
{
    dg_rooms_corridors_config_t config;
    int min_rooms;
    int max_rooms;
    int room_min_size;
    int room_max_size;
    int max_attempts;
    int corridor_width;
    dg_corridor_routing_t corridor_routing;
    int target_rooms;
    int attempt;
    int max_room_extent;
    dg_status_t status;

    if (request == NULL || map == NULL || rng == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_map_fill(map, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return status;
    }
    dg_map_clear_metadata(map);

    config = request->params.rooms;
    min_rooms = dg_max_int(config.min_rooms, 1);
    max_rooms = dg_max_int(config.max_rooms, min_rooms);
    room_min_size = dg_max_int(config.room_min_size, 3);
    room_max_size = dg_max_int(config.room_max_size, room_min_size);
    max_attempts = dg_max_int(config.max_placement_attempts, max_rooms * 8);
    corridor_width = dg_clamp_int(config.corridor_width, 1, 9);
    corridor_routing = dg_normalize_corridor_routing(config.corridor_routing);
    max_room_extent = dg_min_int(map->width - 2, map->height - 2);

    if (max_room_extent < room_min_size) {
        return DG_STATUS_GENERATION_FAILED;
    }

    room_max_size = dg_clamp_int(room_max_size, room_min_size, max_room_extent);
    target_rooms = dg_rng_range(rng, min_rooms, max_rooms);

    for (attempt = 0; attempt < max_attempts && (int)map->metadata.room_count < target_rooms; ++attempt) {
        dg_rect_t candidate;
        int max_x;
        int max_y;
        dg_room_flags_t flags;

        candidate.width = dg_rng_range(rng, room_min_size, room_max_size);
        candidate.height = dg_rng_range(rng, room_min_size, room_max_size);
        max_x = map->width - candidate.width - 1;
        max_y = map->height - candidate.height - 1;
        if (max_x < 1 || max_y < 1) {
            continue;
        }

        candidate.x = dg_rng_range(rng, 1, max_x);
        candidate.y = dg_rng_range(rng, 1, max_y);
        if (!dg_can_place_room(map, &candidate, 1, &request->constraints)) {
            continue;
        }

        dg_carve_room(map, &candidate);
        flags = DG_ROOM_FLAG_NONE;
        if (config.classify_room != NULL) {
            flags = config.classify_room(
                (int)map->metadata.room_count,
                &candidate,
                config.classify_room_user_data
            );
        }

        status = dg_map_add_room(map, &candidate, flags);
        if (status != DG_STATUS_OK) {
            return status;
        }
    }

    if (map->metadata.room_count == 0) {
        return DG_STATUS_GENERATION_FAILED;
    }

    if (map->metadata.room_count > 1) {
        size_t i;
        for (i = 1; i < map->metadata.room_count; ++i) {
            dg_point_t a;
            dg_point_t b;
            int corridor_length;

            a = dg_room_center(&map->metadata.rooms[i - 1].bounds);
            b = dg_room_center(&map->metadata.rooms[i].bounds);
            dg_connect_points(map, rng, a, b, corridor_width, corridor_routing);

            corridor_length = dg_abs_int(a.x - b.x) + dg_abs_int(a.y - b.y) + 1;

            status = dg_map_add_corridor(
                map,
                map->metadata.rooms[i - 1].id,
                map->metadata.rooms[i].id,
                corridor_width,
                corridor_length
            );
            if (status != DG_STATUS_OK) {
                return status;
            }
        }
    }

    return DG_STATUS_OK;
}
