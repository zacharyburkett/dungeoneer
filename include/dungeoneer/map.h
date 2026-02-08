#ifndef DUNGEONEER_MAP_H
#define DUNGEONEER_MAP_H

#include "dungeoneer/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t dg_room_flags_t;

#define DG_ROOM_FLAG_NONE ((dg_room_flags_t)0u)
#define DG_ROOM_FLAG_SPECIAL ((dg_room_flags_t)1u)

typedef struct dg_room_metadata {
    int id;
    dg_rect_t bounds;
    dg_room_flags_t flags;
} dg_room_metadata_t;

typedef struct dg_map_metadata {
    dg_room_metadata_t *rooms;
    size_t room_count;
    size_t room_capacity;
} dg_map_metadata_t;

typedef struct dg_map {
    int width;
    int height;
    dg_tile_t *tiles;
    dg_map_metadata_t metadata;
} dg_map_t;

dg_status_t dg_map_init(dg_map_t *map, int width, int height, dg_tile_t initial_tile);
void dg_map_destroy(dg_map_t *map);

dg_status_t dg_map_fill(dg_map_t *map, dg_tile_t tile);
dg_status_t dg_map_set_tile(dg_map_t *map, int x, int y, dg_tile_t tile);
dg_tile_t dg_map_get_tile(const dg_map_t *map, int x, int y);
bool dg_map_in_bounds(const dg_map_t *map, int x, int y);

void dg_map_clear_metadata(dg_map_t *map);
dg_status_t dg_map_add_room(dg_map_t *map, const dg_rect_t *bounds, dg_room_flags_t flags);

#ifdef __cplusplus
}
#endif

#endif
