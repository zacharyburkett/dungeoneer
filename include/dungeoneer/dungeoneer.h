#ifndef DUNGEONEER_DUNGEONEER_H
#define DUNGEONEER_DUNGEONEER_H

#include "dungeoneer/generator.h"
#include "dungeoneer/map.h"
#include "dungeoneer/rng.h"
#include "dungeoneer/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DUNGEONEER_VERSION_MAJOR 0
#define DUNGEONEER_VERSION_MINOR 1
#define DUNGEONEER_VERSION_PATCH 0

const char *dg_status_string(dg_status_t status);

#ifdef __cplusplus
}
#endif

#endif
