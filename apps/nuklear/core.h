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
    int width;
    int height;
    char seed_text[32];
    dg_bsp_config_t bsp_config;
    dg_drunkards_walk_config_t drunkards_walk_config;
    dg_rooms_and_mazes_config_t rooms_and_mazes_config;
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
