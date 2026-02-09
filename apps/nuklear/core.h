#ifndef DUNGEONEER_NUKLEAR_CORE_H
#define DUNGEONEER_NUKLEAR_CORE_H

#include "dungeoneer/dungeoneer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nk_context;

#define DG_NUKLEAR_MAX_FORBIDDEN_REGIONS 16

typedef struct dg_nuklear_app {
    dg_map_t map;
    bool has_map;
    int algorithm_index;
    int routing_index;
    int width;
    int height;
    char seed_text[32];
    dg_rooms_corridors_config_t rooms_config;
    dg_organic_cave_config_t organic_config;
    dg_generation_constraints_t constraints;
    dg_rect_t forbidden_regions[DG_NUKLEAR_MAX_FORBIDDEN_REGIONS];
    int forbidden_region_count;
    int selected_forbidden_region;
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
