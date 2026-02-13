#ifndef DUNGEONEER_IO_H
#define DUNGEONEER_IO_H

#include "dungeoneer/map.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Writes generation configuration to disk.
 * The saved file contains only the config needed to reproduce a map.
 */
dg_status_t dg_map_save_file(const dg_map_t *map, const char *path);

/*
 * Loads generation configuration and regenerates the map on demand.
 * `out_map` must be zero-initialized or previously destroyed.
 */
dg_status_t dg_map_load_file(const char *path, dg_map_t *out_map);

#ifdef __cplusplus
}
#endif

#endif
