#ifndef DUNGEONEER_NUKLEAR_CORE_H
#define DUNGEONEER_NUKLEAR_CORE_H

#include "dungeoneer/dungeoneer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nk_context;

#define DG_NUKLEAR_MAX_ROOM_TYPES 8
#define DG_NUKLEAR_MAX_PROCESS_METHODS 16

typedef struct dg_nuklear_room_type_ui {
    char label[24];
    int type_id;
    int enabled;
    int min_count;
    int max_count;
    int target_count;
    int area_min;
    int area_max;
    int degree_min;
    int degree_max;
    int border_distance_min;
    int border_distance_max;
    int graph_depth_min;
    int graph_depth_max;
    int weight;
    int larger_room_bias;
    int higher_degree_bias;
    int border_distance_bias;
} dg_nuklear_room_type_ui_t;

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
    dg_process_method_t process_methods[DG_NUKLEAR_MAX_PROCESS_METHODS];
    int process_method_count;
    int process_add_method_type_index;
    float preview_zoom;
    float preview_center_x;
    float preview_center_y;
    int room_types_enabled;
    int room_type_count;
    int room_type_strict_mode;
    int room_type_allow_untyped;
    int room_type_default_type_id;
    dg_nuklear_room_type_ui_t room_type_slots[DG_NUKLEAR_MAX_ROOM_TYPES];
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
