#ifndef DUNGEONEER_IO_H
#define DUNGEONEER_IO_H

#include "dungeoneer/map.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Writes a complete map snapshot (tiles + metadata) to disk.
 * File format is binary, versioned, and intended for dungeoneer runtime use.
 */
dg_status_t dg_map_save_file(const dg_map_t *map, const char *path);

/*
 * Loads a complete map snapshot from disk.
 * `out_map` must be zero-initialized or previously destroyed.
 */
dg_status_t dg_map_load_file(const char *path, dg_map_t *out_map);

#ifdef __cplusplus
}
#endif

#endif
