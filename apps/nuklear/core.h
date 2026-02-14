#ifndef DUNGEONEER_NUKLEAR_CORE_H
#define DUNGEONEER_NUKLEAR_CORE_H

#include "dungeoneer/dungeoneer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nk_context;
struct nk_image;

#define DG_NUKLEAR_MAX_ROOM_TYPES 8
#define DG_NUKLEAR_MAX_PROCESS_METHODS 16
#define DG_NUKLEAR_MAX_EDGE_OPENINGS 16
#define DG_NUKLEAR_LAYOUT_SPLITTER_NONE 0
#define DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_VERTICAL 1
#define DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_HORIZONTAL 2
#define DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_TOP 3
#define DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_BOTTOM 4
#define DG_NUKLEAR_WORKFLOW_LAYOUT 0
#define DG_NUKLEAR_WORKFLOW_ROOMS 1
#define DG_NUKLEAR_WORKFLOW_PROCESS 2

typedef struct dg_nuklear_room_type_ui {
    char label[24];
    int type_id;
    int enabled;
    int min_count;
    int max_count;
    int target_count;
    char template_map_path[DG_ROOM_TEMPLATE_PATH_MAX];
    dg_map_edge_opening_query_t template_opening_query;
    int template_required_opening_matches;
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

typedef struct dg_nuklear_edge_opening_ui {
    int side;
    int start;
    int end;
    int role;
} dg_nuklear_edge_opening_ui_t;

typedef struct dg_nuklear_app {
    dg_map_t map;
    bool has_map;
    int generation_class_index;
    int algorithm_index;
    int width;
    int height;
    char seed_text[32];
    dg_bsp_config_t bsp_config;
    dg_drunkards_walk_config_t drunkards_walk_config;
    dg_cellular_automata_config_t cellular_automata_config;
    dg_value_noise_config_t value_noise_config;
    dg_rooms_and_mazes_config_t rooms_and_mazes_config;
    dg_room_graph_config_t room_graph_config;
    dg_worm_caves_config_t worm_caves_config;
    dg_simplex_noise_config_t simplex_noise_config;
    dg_process_method_t process_methods[DG_NUKLEAR_MAX_PROCESS_METHODS];
    int process_enabled;
    int process_method_count;
    int process_add_method_type_index;
    int process_selected_index;
    int controls_workflow_tab;
    float preview_zoom;
    float preview_center_x;
    float preview_center_y;
    int preview_show_grid;
    float layout_side_left_ratio;
    float layout_side_map_ratio;
    float layout_side_global_ratio;
    float layout_stacked_controls_ratio;
    float layout_stacked_global_ratio;
    float layout_stacked_metadata_ratio;
    int layout_active_splitter;
    int layout_hover_splitter;
    unsigned char *preview_image_pixels;
    int preview_image_width;
    int preview_image_height;
    unsigned char *preview_tile_colors;
    int preview_tile_color_width;
    int preview_tile_color_height;
    int preview_tile_colors_valid;
    uint64_t last_live_config_hash;
    int last_live_config_hash_valid;
    int room_types_enabled;
    int room_type_count;
    int room_type_strict_mode;
    int room_type_allow_untyped;
    int room_type_default_type_id;
    dg_nuklear_room_type_ui_t room_type_slots[DG_NUKLEAR_MAX_ROOM_TYPES];
    int edge_opening_count;
    dg_nuklear_edge_opening_ui_t edge_openings[DG_NUKLEAR_MAX_EDGE_OPENINGS];
    char file_path[256];
    char status_text[256];
} dg_nuklear_app_t;

typedef bool (*dg_nuklear_preview_upload_rgba8_fn)(
    void *user_data,
    int width,
    int height,
    const unsigned char *pixels,
    struct nk_image *out_image
);

typedef struct dg_nuklear_preview_renderer {
    void *user_data;
    dg_nuklear_preview_upload_rgba8_fn upload_rgba8;
} dg_nuklear_preview_renderer_t;

void dg_nuklear_app_init(dg_nuklear_app_t *app);
void dg_nuklear_app_shutdown(dg_nuklear_app_t *app);
void dg_nuklear_app_draw(
    struct nk_context *ctx,
    dg_nuklear_app_t *app,
    int screen_width,
    int screen_height,
    const dg_nuklear_preview_renderer_t *preview_renderer
);

#ifdef __cplusplus
}
#endif

#endif
