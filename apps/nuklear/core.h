#ifndef DUNGEONEER_NUKLEAR_CORE_H
#define DUNGEONEER_NUKLEAR_CORE_H

#include "dungeoneer/dungeoneer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nk_context;

typedef struct dg_nuklear_app {
    dg_map_t map;
    bool has_map;
    int algorithm_index;
    int routing_index;
    float target_floor_coverage;
    char width_text[16];
    char height_text[16];
    char seed_text[32];
    char min_rooms_text[16];
    char max_rooms_text[16];
    char corridor_width_text[16];
    char walk_steps_text[32];
    char brush_radius_text[16];
    char smoothing_passes_text[16];
    char file_path[256];
    char status_text[256];
} dg_nuklear_app_t;

void dg_nuklear_app_init(dg_nuklear_app_t *app);
void dg_nuklear_app_shutdown(dg_nuklear_app_t *app);
void dg_nuklear_app_draw(
    struct nk_context *ctx,
    dg_nuklear_app_t *app,
    int screen_width,
    int screen_height
);

#ifdef __cplusplus
}
#endif

#endif
