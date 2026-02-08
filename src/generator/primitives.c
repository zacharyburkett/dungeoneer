#include "internal.h"

#include <stddef.h>

int dg_min_int(int a, int b)
{
    return a < b ? a : b;
}

int dg_max_int(int a, int b)
{
    return a > b ? a : b;
}

int dg_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

bool dg_is_walkable_tile(dg_tile_t tile)
{
    return tile == DG_TILE_FLOOR || tile == DG_TILE_DOOR;
}

size_t dg_tile_index(const dg_map_t *map, int x, int y)
{
    return ((size_t)y * (size_t)map->width) + (size_t)x;
}

bool dg_rect_is_valid(const dg_rect_t *rect)
{
    return rect != NULL && rect->width > 0 && rect->height > 0;
}

bool dg_rects_overlap(const dg_rect_t *a, const dg_rect_t *b)
{
    long long a_left;
    long long a_top;
    long long a_right;
    long long a_bottom;
    long long b_left;
    long long b_top;
    long long b_right;
    long long b_bottom;

    a_left = (long long)a->x;
    a_top = (long long)a->y;
    a_right = (long long)a->x + (long long)a->width;
    a_bottom = (long long)a->y + (long long)a->height;
    b_left = (long long)b->x;
    b_top = (long long)b->y;
    b_right = (long long)b->x + (long long)b->width;
    b_bottom = (long long)b->y + (long long)b->height;

    if (a_right <= b_left || b_right <= a_left) {
        return false;
    }

    if (a_bottom <= b_top || b_bottom <= a_top) {
        return false;
    }

    return true;
}

bool dg_rects_overlap_with_padding(const dg_rect_t *a, const dg_rect_t *b, int padding)
{
    dg_rect_t expanded;

    expanded.x = a->x - padding;
    expanded.y = a->y - padding;
    expanded.width = a->width + (padding * 2);
    expanded.height = a->height + (padding * 2);

    return dg_rects_overlap(&expanded, b);
}

bool dg_clamp_region_to_map(
    const dg_map_t *map,
    const dg_rect_t *region,
    int *out_x0,
    int *out_y0,
    int *out_x1,
    int *out_y1
)
{
    long long x0_ll;
    long long y0_ll;
    long long x1_ll;
    long long y1_ll;
    int x0;
    int y0;
    int x1;
    int y1;

    if (map == NULL || map->tiles == NULL || !dg_rect_is_valid(region)) {
        return false;
    }

    x0_ll = (long long)region->x;
    y0_ll = (long long)region->y;
    x1_ll = (long long)region->x + (long long)region->width;
    y1_ll = (long long)region->y + (long long)region->height;

    if (x1_ll <= 0 || y1_ll <= 0) {
        return false;
    }

    if (x0_ll >= (long long)map->width || y0_ll >= (long long)map->height) {
        return false;
    }

    x0 = dg_max_int((int)x0_ll, 0);
    y0 = dg_max_int((int)y0_ll, 0);
    x1 = dg_min_int((int)x1_ll, map->width);
    y1 = dg_min_int((int)y1_ll, map->height);

    if (x0 >= x1 || y0 >= y1) {
        return false;
    }

    *out_x0 = x0;
    *out_y0 = y0;
    *out_x1 = x1;
    *out_y1 = y1;
    return true;
}

void dg_paint_outer_walls(dg_map_t *map)
{
    int x;
    int y;

    if (map == NULL || map->tiles == NULL) {
        return;
    }

    for (x = 0; x < map->width; ++x) {
        (void)dg_map_set_tile(map, x, 0, DG_TILE_WALL);
        (void)dg_map_set_tile(map, x, map->height - 1, DG_TILE_WALL);
    }

    for (y = 0; y < map->height; ++y) {
        (void)dg_map_set_tile(map, 0, y, DG_TILE_WALL);
        (void)dg_map_set_tile(map, map->width - 1, y, DG_TILE_WALL);
    }
}

bool dg_has_outer_walls(const dg_map_t *map)
{
    int x;
    int y;

    if (map == NULL || map->tiles == NULL) {
        return false;
    }

    for (x = 0; x < map->width; ++x) {
        if (dg_map_get_tile(map, x, 0) != DG_TILE_WALL) {
            return false;
        }
        if (dg_map_get_tile(map, x, map->height - 1) != DG_TILE_WALL) {
            return false;
        }
    }

    for (y = 0; y < map->height; ++y) {
        if (dg_map_get_tile(map, 0, y) != DG_TILE_WALL) {
            return false;
        }
        if (dg_map_get_tile(map, map->width - 1, y) != DG_TILE_WALL) {
            return false;
        }
    }

    return true;
}

void dg_carve_brush(dg_map_t *map, int cx, int cy, int radius, dg_tile_t tile)
{
    int dx;
    int dy;
    int radius_sq;

    if (map == NULL || map->tiles == NULL) {
        return;
    }

    if (radius < 0) {
        radius = 0;
    }

    radius_sq = radius * radius;
    for (dy = -radius; dy <= radius; ++dy) {
        for (dx = -radius; dx <= radius; ++dx) {
            int nx = cx + dx;
            int ny = cy + dy;
            int distance_sq = (dx * dx) + (dy * dy);

            if (distance_sq > radius_sq) {
                continue;
            }

            if (!dg_map_in_bounds(map, nx, ny)) {
                continue;
            }

            (void)dg_map_set_tile(map, nx, ny, tile);
        }
    }
}
