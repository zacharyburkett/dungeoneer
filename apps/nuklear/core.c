#include "core.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "nuklear_features.h"
#include "nuklear.h"

static void dg_nuklear_set_status(dg_nuklear_app_t *app, const char *format, ...)
{
    va_list args;

    if (app == NULL || format == NULL) {
        return;
    }

    va_start(args, format);
    (void)vsnprintf(app->status_text, sizeof(app->status_text), format, args);
    va_end(args);
}

static bool dg_nuklear_parse_u64(const char *text, uint64_t *out_value)
{
    char *end;
    unsigned long long parsed;

    if (text == NULL || out_value == NULL) {
        return false;
    }

    errno = 0;
    end = NULL;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }

    *out_value = (uint64_t)parsed;
    return true;
}

static dg_algorithm_t dg_nuklear_algorithm_from_index(int algorithm_index)
{
    if (algorithm_index == 2) {
        return DG_ALGORITHM_ROOMS_AND_MAZES;
    }
    if (algorithm_index == 1) {
        return DG_ALGORITHM_DRUNKARDS_WALK;
    }
    return DG_ALGORITHM_BSP_TREE;
}

static const char *dg_nuklear_algorithm_name(dg_algorithm_t algorithm)
{
    switch (algorithm) {
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        return "rooms_and_mazes";
    case DG_ALGORITHM_DRUNKARDS_WALK:
        return "drunkards_walk";
    case DG_ALGORITHM_BSP_TREE:
    default:
        return "bsp_tree";
    }
}

static float dg_nuklear_min_float(float a, float b)
{
    return (a < b) ? a : b;
}

static struct nk_color dg_nuklear_tile_color(dg_tile_t tile)
{
    switch (tile) {
    case DG_TILE_WALL:
        return nk_rgb(48, 54, 66);
    case DG_TILE_FLOOR:
        return nk_rgb(188, 196, 173);
    case DG_TILE_DOOR:
        return nk_rgb(224, 176, 85);
    case DG_TILE_VOID:
    default:
        return nk_rgb(18, 22, 28);
    }
}

static void dg_nuklear_destroy_map(dg_nuklear_app_t *app)
{
    if (app == NULL) {
        return;
    }

    if (app->has_map) {
        dg_map_destroy(&app->map);
        app->map = (dg_map_t){0};
        app->has_map = false;
    }
}

static void dg_nuklear_reset_algorithm_defaults(dg_nuklear_app_t *app, dg_algorithm_t algorithm)
{
    dg_generate_request_t defaults;

    if (app == NULL) {
        return;
    }

    dg_default_generate_request(&defaults, algorithm, app->width, app->height, 1u);
    if (algorithm == DG_ALGORITHM_DRUNKARDS_WALK) {
        app->drunkards_walk_config = defaults.params.drunkards_walk;
    } else if (algorithm == DG_ALGORITHM_ROOMS_AND_MAZES) {
        app->rooms_and_mazes_config = defaults.params.rooms_and_mazes;
    } else {
        app->bsp_config = defaults.params.bsp;
    }
}

static void dg_nuklear_generate_map(dg_nuklear_app_t *app)
{
    dg_generate_request_t request;
    dg_map_t generated;
    uint64_t seed;
    dg_algorithm_t algorithm;
    dg_status_t status;

    if (app == NULL) {
        return;
    }

    if (!dg_nuklear_parse_u64(app->seed_text, &seed)) {
        dg_nuklear_set_status(app, "Invalid seed.");
        return;
    }

    if (app->width < 8 || app->height < 8) {
        dg_nuklear_set_status(app, "Width/height must be >= 8.");
        return;
    }

    algorithm = dg_nuklear_algorithm_from_index(app->algorithm_index);
    dg_default_generate_request(&request, algorithm, app->width, app->height, seed);

    if (algorithm == DG_ALGORITHM_DRUNKARDS_WALK) {
        request.params.drunkards_walk = app->drunkards_walk_config;
    } else if (algorithm == DG_ALGORITHM_ROOMS_AND_MAZES) {
        request.params.rooms_and_mazes = app->rooms_and_mazes_config;
    } else {
        request.params.bsp = app->bsp_config;
    }

    generated = (dg_map_t){0};
    status = dg_generate(&request, &generated);
    if (status != DG_STATUS_OK) {
        dg_nuklear_set_status(app, "Generate failed: %s", dg_status_string(status));
        return;
    }

    dg_nuklear_destroy_map(app);
    app->map = generated;
    app->has_map = true;

    dg_nuklear_set_status(
        app,
        "Generated %dx%d %s map.",
        app->map.width,
        app->map.height,
        dg_nuklear_algorithm_name(algorithm)
    );
}

static void dg_nuklear_save_map(dg_nuklear_app_t *app)
{
    dg_status_t status;

    if (app == NULL) {
        return;
    }

    if (!app->has_map) {
        dg_nuklear_set_status(app, "No map to save.");
        return;
    }

    status = dg_map_save_file(&app->map, app->file_path);
    if (status != DG_STATUS_OK) {
        dg_nuklear_set_status(app, "Save failed: %s", dg_status_string(status));
        return;
    }

    dg_nuklear_set_status(app, "Saved map to %s", app->file_path);
}

static void dg_nuklear_load_map(dg_nuklear_app_t *app)
{
    dg_map_t loaded;
    dg_status_t status;

    if (app == NULL) {
        return;
    }

    loaded = (dg_map_t){0};
    status = dg_map_load_file(app->file_path, &loaded);
    if (status != DG_STATUS_OK) {
        dg_nuklear_set_status(app, "Load failed: %s", dg_status_string(status));
        return;
    }

    dg_nuklear_destroy_map(app);
    app->map = loaded;
    app->has_map = true;
    dg_nuklear_set_status(app, "Loaded map from %s", app->file_path);
}

static void dg_nuklear_draw_map(
    struct nk_context *ctx,
    const dg_nuklear_app_t *app,
    float suggested_height
)
{
    struct nk_rect preview_bounds;
    enum nk_widget_layout_states widget_state;
    struct nk_command_buffer *canvas;

    if (ctx == NULL || app == NULL) {
        return;
    }

    if (suggested_height < 120.0f) {
        suggested_height = 120.0f;
    }

    if (app->has_map) {
        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Map preview", NK_TEXT_LEFT);
    } else {
        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "No map loaded. Click Generate or Load.", NK_TEXT_LEFT);
    }

    nk_layout_row_dynamic(ctx, suggested_height, 1);
    widget_state = nk_widget(&preview_bounds, ctx);
    if (widget_state == NK_WIDGET_INVALID) {
        return;
    }

    canvas = nk_window_get_canvas(ctx);
    nk_fill_rect(canvas, preview_bounds, 0.0f, nk_rgb(20, 24, 31));
    nk_stroke_rect(canvas, preview_bounds, 0.0f, 1.0f, nk_rgb(85, 96, 112));

    if (app->has_map && app->map.tiles != NULL && app->map.width > 0 && app->map.height > 0) {
        int x;
        int y;
        float tile_size;
        float draw_width;
        float draw_height;
        float origin_x;
        float origin_y;

        tile_size = dg_nuklear_min_float(
            preview_bounds.w / (float)app->map.width,
            preview_bounds.h / (float)app->map.height
        );

        if (tile_size < 1.0f) {
            return;
        }

        tile_size = (float)((int)tile_size);
        draw_width = tile_size * (float)app->map.width;
        draw_height = tile_size * (float)app->map.height;
        origin_x = preview_bounds.x + (preview_bounds.w - draw_width) * 0.5f;
        origin_y = preview_bounds.y + (preview_bounds.h - draw_height) * 0.5f;

        for (y = 0; y < app->map.height; ++y) {
            for (x = 0; x < app->map.width; ++x) {
                dg_tile_t tile;
                struct nk_color color;
                struct nk_rect r;

                tile = dg_map_get_tile(&app->map, x, y);
                color = dg_nuklear_tile_color(tile);
                r = nk_rect(
                    origin_x + tile_size * (float)x,
                    origin_y + tile_size * (float)y,
                    tile_size,
                    tile_size
                );
                nk_fill_rect(canvas, r, 0.0f, color);
            }
        }
    }
}

static void dg_nuklear_draw_metadata(struct nk_context *ctx, const dg_nuklear_app_t *app)
{
    char line[128];
    double average_room_degree;

    if (ctx == NULL || app == NULL) {
        return;
    }

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Metadata", NK_TEXT_LEFT);

    if (!app->has_map) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "No map loaded.", NK_TEXT_LEFT);
        return;
    }

    nk_layout_row_dynamic(ctx, 18.0f, 1);

    average_room_degree = 0.0;
    if (app->map.metadata.room_count > 0) {
        average_room_degree = (double)app->map.metadata.room_neighbor_count /
                              (double)app->map.metadata.room_count;
    }

    (void)snprintf(
        line,
        sizeof(line),
        "algorithm: %s",
        dg_nuklear_algorithm_name((dg_algorithm_t)app->map.metadata.algorithm_id)
    );
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(line, sizeof(line), "size: %dx%d", app->map.width, app->map.height);
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(line, sizeof(line), "seed: %llu", (unsigned long long)app->map.metadata.seed);
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(
        line,
        sizeof(line),
        "tiles: walkable=%llu wall=%llu",
        (unsigned long long)app->map.metadata.walkable_tile_count,
        (unsigned long long)app->map.metadata.wall_tile_count
    );
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(
        line,
        sizeof(line),
        "rooms: total=%llu leaf=%llu",
        (unsigned long long)app->map.metadata.room_count,
        (unsigned long long)app->map.metadata.leaf_room_count
    );
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(
        line,
        sizeof(line),
        "corridors: count=%llu length=%llu",
        (unsigned long long)app->map.metadata.corridor_count,
        (unsigned long long)app->map.metadata.corridor_total_length
    );
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(line, sizeof(line), "room graph: avg degree=%.2f", average_room_degree);
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(
        line,
        sizeof(line),
        "connectivity: connected=%s components=%llu",
        app->map.metadata.connected_floor ? "yes" : "no",
        (unsigned long long)app->map.metadata.connected_component_count
    );
    nk_label(ctx, line, NK_TEXT_LEFT);
}

static void dg_nuklear_draw_generation_settings(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    static const char *algorithms[] = {"BSP Tree", "Drunkard's Walk", "Rooms + Mazes"};
    int previous_algorithm_index;

    previous_algorithm_index = app->algorithm_index;

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Algorithm", NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    app->algorithm_index = nk_combo(
        ctx,
        algorithms,
        (int)(sizeof(algorithms) / sizeof(algorithms[0])),
        app->algorithm_index,
        25,
        nk_vec2(280, 120)
    );

    if (app->algorithm_index != previous_algorithm_index) {
        dg_nuklear_set_status(
            app,
            "Selected algorithm: %s",
            dg_nuklear_algorithm_name(dg_nuklear_algorithm_from_index(app->algorithm_index))
        );
    }

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Width", 8, &app->width, 512, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Height", 8, &app->height, 512, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Seed", NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    (void)nk_edit_string_zero_terminated(
        ctx,
        NK_EDIT_FIELD,
        app->seed_text,
        (int)sizeof(app->seed_text),
        nk_filter_decimal
    );
}

static void dg_nuklear_draw_bsp_settings(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Min Rooms", 1, &app->bsp_config.min_rooms, 256, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Max Rooms", 1, &app->bsp_config.max_rooms, 256, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Room Min Size", 3, &app->bsp_config.room_min_size, 64, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Room Max Size", 3, &app->bsp_config.room_max_size, 64, 1, 0.25f);

    if (app->bsp_config.max_rooms < app->bsp_config.min_rooms) {
        app->bsp_config.max_rooms = app->bsp_config.min_rooms;
    }

    if (app->bsp_config.room_max_size < app->bsp_config.room_min_size) {
        app->bsp_config.room_max_size = app->bsp_config.room_min_size;
    }

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Reset BSP Defaults")) {
        dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_BSP_TREE);
        dg_nuklear_set_status(app, "BSP defaults restored.");
    }
}

static void dg_nuklear_draw_drunkards_settings(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Wiggle (%)",
        0,
        &app->drunkards_walk_config.wiggle_percent,
        100,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Reset Drunkard Defaults")) {
        dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_DRUNKARDS_WALK);
        dg_nuklear_set_status(app, "Drunkard's Walk defaults restored.");
    }
}

static void dg_nuklear_draw_rooms_and_mazes_settings(
    struct nk_context *ctx,
    dg_nuklear_app_t *app
)
{
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Min Rooms",
        1,
        &app->rooms_and_mazes_config.min_rooms,
        256,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Max Rooms",
        1,
        &app->rooms_and_mazes_config.max_rooms,
        256,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Room Min Size",
        3,
        &app->rooms_and_mazes_config.room_min_size,
        64,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Room Max Size",
        3,
        &app->rooms_and_mazes_config.room_max_size,
        64,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Prune Steps (-1=All)",
        -1,
        &app->rooms_and_mazes_config.dead_end_prune_steps,
        10000,
        1,
        0.25f
    );

    if (app->rooms_and_mazes_config.max_rooms < app->rooms_and_mazes_config.min_rooms) {
        app->rooms_and_mazes_config.max_rooms = app->rooms_and_mazes_config.min_rooms;
    }

    if (app->rooms_and_mazes_config.room_max_size < app->rooms_and_mazes_config.room_min_size) {
        app->rooms_and_mazes_config.room_max_size = app->rooms_and_mazes_config.room_min_size;
    }

    if (app->rooms_and_mazes_config.dead_end_prune_steps < -1) {
        app->rooms_and_mazes_config.dead_end_prune_steps = -1;
    }

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Reset Rooms+Mazes Defaults")) {
        dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ROOMS_AND_MAZES);
        dg_nuklear_set_status(app, "Rooms + Mazes defaults restored.");
    }
}

static void dg_nuklear_draw_save_load(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "File Path", NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    (void)nk_edit_string_zero_terminated(
        ctx,
        NK_EDIT_FIELD,
        app->file_path,
        (int)sizeof(app->file_path),
        nk_filter_default
    );

    nk_layout_row_dynamic(ctx, 32.0f, 2);
    if (nk_button_label(ctx, "Save")) {
        dg_nuklear_save_map(app);
    }
    if (nk_button_label(ctx, "Load")) {
        dg_nuklear_load_map(app);
    }
}

static void dg_nuklear_draw_controls(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    dg_algorithm_t algorithm;

    if (ctx == NULL || app == NULL) {
        return;
    }

    algorithm = dg_nuklear_algorithm_from_index(app->algorithm_index);

    nk_layout_row_dynamic(ctx, 34.0f, 2);
    if (nk_button_label(ctx, "Generate")) {
        dg_nuklear_generate_map(app);
    }
    if (nk_button_label(ctx, "Clear Map")) {
        dg_nuklear_destroy_map(app);
        dg_nuklear_set_status(app, "Cleared map.");
    }

    if (nk_tree_push(ctx, NK_TREE_TAB, "Generation", NK_MAXIMIZED)) {
        dg_nuklear_draw_generation_settings(ctx, app);
        nk_tree_pop(ctx);
    }

    if (algorithm == DG_ALGORITHM_DRUNKARDS_WALK) {
        if (nk_tree_push(ctx, NK_TREE_TAB, "Drunkard Settings", NK_MAXIMIZED)) {
            dg_nuklear_draw_drunkards_settings(ctx, app);
            nk_tree_pop(ctx);
        }
    } else if (algorithm == DG_ALGORITHM_ROOMS_AND_MAZES) {
        if (nk_tree_push(ctx, NK_TREE_TAB, "Rooms + Mazes Settings", NK_MAXIMIZED)) {
            dg_nuklear_draw_rooms_and_mazes_settings(ctx, app);
            nk_tree_pop(ctx);
        }
    } else {
        if (nk_tree_push(ctx, NK_TREE_TAB, "BSP Settings", NK_MAXIMIZED)) {
            dg_nuklear_draw_bsp_settings(ctx, app);
            nk_tree_pop(ctx);
        }
    }

    if (nk_tree_push(ctx, NK_TREE_TAB, "Save / Load", NK_MINIMIZED)) {
        dg_nuklear_draw_save_load(ctx, app);
        nk_tree_pop(ctx);
    }

    nk_layout_row_dynamic(ctx, 60.0f, 1);
    nk_label_wrap(ctx, app->status_text);
}

void dg_nuklear_app_init(dg_nuklear_app_t *app)
{
    if (app == NULL) {
        return;
    }

    *app = (dg_nuklear_app_t){0};
    app->algorithm_index = 0;
    app->width = 80;
    app->height = 40;

    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_BSP_TREE);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_DRUNKARDS_WALK);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ROOMS_AND_MAZES);

    (void)snprintf(app->seed_text, sizeof(app->seed_text), "1337");
    (void)snprintf(app->file_path, sizeof(app->file_path), "dungeon.dgmap");
    (void)snprintf(app->status_text, sizeof(app->status_text), "Ready.");
}

void dg_nuklear_app_shutdown(dg_nuklear_app_t *app)
{
    dg_nuklear_destroy_map(app);
}

void dg_nuklear_app_draw(
    struct nk_context *ctx,
    dg_nuklear_app_t *app,
    int screen_width,
    int screen_height
)
{
    const float margin = 10.0f;
    const float side_metadata_height = 220.0f;
    const float stacked_metadata_height = 180.0f;
    float left_width = 420.0f;
    float right_x;
    float right_width;
    float map_height;
    struct nk_rect controls_rect;
    struct nk_rect map_rect;
    struct nk_rect metadata_rect;

    if (ctx == NULL || app == NULL || screen_width <= 0 || screen_height <= 0) {
        return;
    }

    if (screen_width < 980 || screen_height < 640) {
        float controls_height;

        controls_height = (float)screen_height * 0.50f;
        if (controls_height < 220.0f) {
            controls_height = 220.0f;
        }

        map_height = (float)screen_height - controls_height - stacked_metadata_height - (margin * 4.0f);
        if (map_height < 120.0f) {
            map_height = 120.0f;
            controls_height = (float)screen_height - stacked_metadata_height - map_height - (margin * 4.0f);
            if (controls_height < 180.0f) {
                controls_height = 180.0f;
            }
        }

        controls_rect = nk_rect(
            margin,
            margin,
            (float)screen_width - (margin * 2.0f),
            controls_height
        );

        map_rect = nk_rect(
            margin,
            margin * 2.0f + controls_height,
            (float)screen_width - (margin * 2.0f),
            map_height
        );

        metadata_rect = nk_rect(
            margin,
            margin * 3.0f + controls_height + map_height,
            (float)screen_width - (margin * 2.0f),
            stacked_metadata_height
        );
    } else {
        if (left_width > (float)screen_width * 0.45f) {
            left_width = (float)screen_width * 0.45f;
        }
        if (left_width < 340.0f) {
            left_width = 340.0f;
        }

        right_x = margin + left_width + margin;
        right_width = (float)screen_width - right_x - margin;
        if (right_width < 260.0f) {
            right_width = 260.0f;
            left_width = (float)screen_width - right_width - (margin * 3.0f);
        }

        map_height = (float)screen_height - (margin * 3.0f) - side_metadata_height;
        if (map_height < 200.0f) {
            map_height = 200.0f;
        }

        controls_rect = nk_rect(
            margin,
            margin,
            left_width,
            (float)screen_height - margin * 2.0f
        );
        map_rect = nk_rect(right_x, margin, right_width, map_height);
        metadata_rect = nk_rect(
            right_x,
            margin * 2.0f + map_height,
            right_width,
            side_metadata_height
        );
    }

    if (nk_begin(
            ctx,
            "Dungeoneer Editor",
            controls_rect,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE
        )) {
        dg_nuklear_draw_controls(ctx, app);
    }
    nk_end(ctx);

    if (nk_begin(
            ctx,
            "Map Preview",
            map_rect,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE
        )) {
        dg_nuklear_draw_map(ctx, app, map_rect.h - 50.0f);
    }
    nk_end(ctx);

    if (nk_begin(
            ctx,
            "Map Metadata",
            metadata_rect,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE
        )) {
        dg_nuklear_draw_metadata(ctx, app);
    }
    nk_end(ctx);
}
