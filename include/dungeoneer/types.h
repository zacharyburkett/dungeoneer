#ifndef DUNGEONEER_TYPES_H
#define DUNGEONEER_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dg_status {
    DG_STATUS_OK = 0,
    DG_STATUS_INVALID_ARGUMENT = 1,
    DG_STATUS_ALLOCATION_FAILED = 2,
    DG_STATUS_GENERATION_FAILED = 3
} dg_status_t;

typedef struct dg_point {
    int x;
    int y;
} dg_point_t;

typedef struct dg_rect {
    int x;
    int y;
    int width;
    int height;
} dg_rect_t;

typedef enum dg_tile {
    DG_TILE_VOID = 0,
    DG_TILE_WALL = 1,
    DG_TILE_FLOOR = 2,
    DG_TILE_DOOR = 3
} dg_tile_t;

#ifdef __cplusplus
}
#endif

#endif
