#include "core.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (algorithm_index == 1) {
        return DG_ALGORITHM_ORGANIC_CAVE;
    }
    return DG_ALGORITHM_ROOMS_AND_CORRIDORS;
}

static dg_corridor_routing_t dg_nuklear_routing_from_index(int routing_index)
{
    switch (routing_index) {
    case 1:
        return DG_CORRIDOR_ROUTING_HORIZONTAL_FIRST;
    case 2:
        return DG_CORRIDOR_ROUTING_VERTICAL_FIRST;
    default:
        return DG_CORRIDOR_ROUTING_RANDOM;
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
    const int width = (app != NULL && app->width > 0) ? app->width : 80;
    const int height = (app != NULL && app->height > 0) ? app->height : 40;

    if (app == NULL) {
        return;
    }

    dg_default_generate_request(&defaults, algorithm, width, height, 1u);

    if (algorithm == DG_ALGORITHM_ROOMS_AND_CORRIDORS) {
        app->rooms_config = defaults.params.rooms;
        app->routing_index = (int)app->rooms_config.corridor_routing;
    } else {
        app->organic_config = defaults.params.organic;
    }
}

static void dg_nuklear_reset_constraint_defaults(dg_nuklear_app_t *app)
{
    dg_generate_request_t defaults;
    const int width = (app != NULL && app->width > 0) ? app->width : 80;
    const int height = (app != NULL && app->height > 0) ? app->height : 40;

    if (app == NULL) {
        return;
    }

    dg_default_generate_request(
        &defaults,
        dg_nuklear_algorithm_from_index(app->algorithm_index),
        width,
        height,
        1u
    );

    app->constraints = defaults.constraints;
    app->constraints.forbidden_regions = NULL;
    app->constraints.forbidden_region_count = 0;
}

static void dg_nuklear_remove_forbidden_region(dg_nuklear_app_t *app, int region_index)
{
    int i;

    if (app == NULL || region_index < 0 || region_index >= app->forbidden_region_count) {
        return;
    }

    for (i = region_index + 1; i < app->forbidden_region_count; ++i) {
        app->forbidden_regions[i - 1] = app->forbidden_regions[i];
    }

    app->forbidden_region_count -= 1;
    if (app->forbidden_region_count <= 0) {
        app->forbidden_region_count = 0;
        app->selected_forbidden_region = -1;
    } else if (app->selected_forbidden_region >= app->forbidden_region_count) {
        app->selected_forbidden_region = app->forbidden_region_count - 1;
    }
}

static void dg_nuklear_generate_map(dg_nuklear_app_t *app)
{
    dg_generate_request_t request;
    dg_map_t generated;
    dg_algorithm_t algorithm;
    uint64_t seed;
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

    request.constraints = app->constraints;
    if (app->forbidden_region_count > 0) {
        request.constraints.forbidden_regions = app->forbidden_regions;
        request.constraints.forbidden_region_count = (size_t)app->forbidden_region_count;
    } else {
        request.constraints.forbidden_regions = NULL;
        request.constraints.forbidden_region_count = 0;
    }

    if (algorithm == DG_ALGORITHM_ROOMS_AND_CORRIDORS) {
        request.params.rooms = app->rooms_config;
        request.params.rooms.corridor_routing = dg_nuklear_routing_from_index(app->routing_index);
    } else {
        request.params.organic = app->organic_config;
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
        "Generated %dx%d map in %llu attempt(s).",
        app->map.width,
        app->map.height,
        (unsigned long long)app->map.metadata.generation_attempts
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
        "rooms: total=%llu special=%llu leaf=%llu",
        (unsigned long long)app->map.metadata.room_count,
        (unsigned long long)app->map.metadata.special_room_count,
        (unsigned long long)app->map.metadata.leaf_room_count
    );
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(
        line,
        sizeof(line),
        "roles: E=%llu X=%llu B=%llu T=%llu S=%llu",
        (unsigned long long)app->map.metadata.entrance_room_count,
        (unsigned long long)app->map.metadata.exit_room_count,
        (unsigned long long)app->map.metadata.boss_room_count,
        (unsigned long long)app->map.metadata.treasure_room_count,
        (unsigned long long)app->map.metadata.shop_room_count
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

    (void)snprintf(
        line,
        sizeof(line),
        "connectivity: connected=%s components=%llu",
        app->map.metadata.connected_floor ? "yes" : "no",
        (unsigned long long)app->map.metadata.connected_component_count
    );
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(
        line,
        sizeof(line),
        "entrance-exit distance: %d",
        app->map.metadata.entrance_exit_distance
    );
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(
        line,
        sizeof(line),
        "generation attempts: %llu",
        (unsigned long long)app->map.metadata.generation_attempts
    );
    nk_label(ctx, line, NK_TEXT_LEFT);
}

static void dg_nuklear_draw_generation_settings(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    static const char *algorithms[] = {"Rooms + Corridors", "Organic Cave"};

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Algorithm", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    app->algorithm_index = nk_combo(
        ctx,
        algorithms,
        (int)(sizeof(algorithms) / sizeof(algorithms[0])),
        app->algorithm_index,
        25,
        nk_vec2(300, 120)
    );

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

static void dg_nuklear_draw_algorithm_settings(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    static const char *routing_modes[] = {"Random", "Horizontal First", "Vertical First"};

    if (app->algorithm_index == 0) {
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Min Rooms", 1, &app->rooms_config.min_rooms, 256, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Max Rooms", 1, &app->rooms_config.max_rooms, 256, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Room Min Size", 2, &app->rooms_config.room_min_size, 64, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Room Max Size", 2, &app->rooms_config.room_max_size, 64, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Placement Attempts",
            1,
            &app->rooms_config.max_placement_attempts,
            10000,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Corridor Width", 1, &app->rooms_config.corridor_width, 8, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Corridor Routing", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        app->routing_index = nk_combo(
            ctx,
            routing_modes,
            (int)(sizeof(routing_modes) / sizeof(routing_modes[0])),
            app->routing_index,
            25,
            nk_vec2(300, 120)
        );

        if (app->rooms_config.max_rooms < app->rooms_config.min_rooms) {
            app->rooms_config.max_rooms = app->rooms_config.min_rooms;
        }
        if (app->rooms_config.room_max_size < app->rooms_config.room_min_size) {
            app->rooms_config.room_max_size = app->rooms_config.room_min_size;
        }

        nk_layout_row_dynamic(ctx, 30.0f, 1);
        if (nk_button_label(ctx, "Reset Rooms Defaults")) {
            dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ROOMS_AND_CORRIDORS);
            dg_nuklear_set_status(app, "Rooms + corridors defaults restored.");
        }
    } else {
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Walk Steps", 1, &app->organic_config.walk_steps, 200000, 10, 0.5f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Brush Radius", 1, &app->organic_config.brush_radius, 8, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Smoothing Passes", 0, &app->organic_config.smoothing_passes, 16, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_float(
            ctx,
            "Target Coverage",
            0.05f,
            &app->organic_config.target_floor_coverage,
            0.95f,
            0.01f,
            0.005f
        );

        nk_layout_row_dynamic(ctx, 30.0f, 1);
        if (nk_button_label(ctx, "Reset Organic Defaults")) {
            dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ORGANIC_CAVE);
            dg_nuklear_set_status(app, "Organic defaults restored.");
        }
    }
}

static void dg_nuklear_draw_constraints(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    app->constraints.require_connected_floor = nk_check_label(
        ctx,
        "Require Connected Floor",
        app->constraints.require_connected_floor
    );
    app->constraints.enforce_outer_walls = nk_check_label(
        ctx,
        "Enforce Outer Walls",
        app->constraints.enforce_outer_walls
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_float(
        ctx,
        "Min Floor Coverage",
        0.0f,
        &app->constraints.min_floor_coverage,
        1.0f,
        0.01f,
        0.005f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_float(
        ctx,
        "Max Floor Coverage",
        0.0f,
        &app->constraints.max_floor_coverage,
        1.0f,
        0.01f,
        0.005f
    );

    if (app->constraints.max_floor_coverage < app->constraints.min_floor_coverage) {
        app->constraints.max_floor_coverage = app->constraints.min_floor_coverage;
    }

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Min Rooms", 0, &app->constraints.min_room_count, 256, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Max Rooms", 0, &app->constraints.max_room_count, 256, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Min Special Rooms", 0, &app->constraints.min_special_rooms, 256, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Max Generation Attempts",
        1,
        &app->constraints.max_generation_attempts,
        128,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Reset Constraint Defaults")) {
        dg_nuklear_reset_constraint_defaults(app);
        dg_nuklear_set_status(app, "Constraint defaults restored.");
    }
}

static void dg_nuklear_draw_role_requirements(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Required Entrance Rooms",
        0,
        &app->constraints.required_entrance_rooms,
        64,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Required Exit Rooms",
        0,
        &app->constraints.required_exit_rooms,
        64,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Required Boss Rooms",
        0,
        &app->constraints.required_boss_rooms,
        64,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Required Treasure Rooms",
        0,
        &app->constraints.required_treasure_rooms,
        64,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Required Shop Rooms",
        0,
        &app->constraints.required_shop_rooms,
        64,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Min Entrance-Exit Distance",
        0,
        &app->constraints.min_entrance_exit_distance,
        64,
        1,
        0.25f
    );

    app->constraints.require_boss_on_leaf = nk_check_label(
        ctx,
        "Require Boss On Leaf",
        app->constraints.require_boss_on_leaf
    );
}

static void dg_nuklear_draw_weight_block(
    struct nk_context *ctx,
    const char *label,
    dg_role_placement_weights_t *weights
)
{
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, label, NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Distance Weight",
        -64,
        &weights->distance_weight,
        64,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Degree Weight",
        -64,
        &weights->degree_weight,
        64,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Leaf Bonus",
        -64,
        &weights->leaf_bonus,
        64,
        1,
        0.25f
    );
}

static void dg_nuklear_draw_role_weights(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    dg_nuklear_draw_weight_block(ctx, "Entrance Weights", &app->constraints.entrance_weights);
    dg_nuklear_draw_weight_block(ctx, "Exit Weights", &app->constraints.exit_weights);
    dg_nuklear_draw_weight_block(ctx, "Boss Weights", &app->constraints.boss_weights);
    dg_nuklear_draw_weight_block(ctx, "Treasure Weights", &app->constraints.treasure_weights);
    dg_nuklear_draw_weight_block(ctx, "Shop Weights", &app->constraints.shop_weights);
}

static void dg_nuklear_draw_forbidden_regions(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    char line[128];
    int i;

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(
        ctx,
        "Forbidden regions are forced to walls after generation.",
        NK_TEXT_LEFT
    );

    nk_layout_row_dynamic(ctx, 30.0f, 3);
    if (nk_button_label(ctx, "Add")) {
        if (app->forbidden_region_count >= DG_NUKLEAR_MAX_FORBIDDEN_REGIONS) {
            dg_nuklear_set_status(
                app,
                "Cannot add more forbidden regions (max %d).",
                DG_NUKLEAR_MAX_FORBIDDEN_REGIONS
            );
        } else {
            dg_rect_t *region = &app->forbidden_regions[app->forbidden_region_count];
            *region = (dg_rect_t){0, 0, 8, 8};
            app->selected_forbidden_region = app->forbidden_region_count;
            app->forbidden_region_count += 1;
        }
    }

    if (nk_button_label(ctx, "Remove")) {
        dg_nuklear_remove_forbidden_region(app, app->selected_forbidden_region);
    }

    if (nk_button_label(ctx, "Clear All")) {
        app->forbidden_region_count = 0;
        app->selected_forbidden_region = -1;
    }

    (void)snprintf(
        line,
        sizeof(line),
        "Count: %d / %d",
        app->forbidden_region_count,
        DG_NUKLEAR_MAX_FORBIDDEN_REGIONS
    );
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, line, NK_TEXT_LEFT);

    for (i = 0; i < app->forbidden_region_count; ++i) {
        const dg_rect_t *region = &app->forbidden_regions[i];

        (void)snprintf(
            line,
            sizeof(line),
            "%sRegion %d: x=%d y=%d w=%d h=%d",
            (app->selected_forbidden_region == i) ? "* " : "",
            i,
            region->x,
            region->y,
            region->width,
            region->height
        );

        nk_layout_row_dynamic(ctx, 24.0f, 1);
        if (nk_button_label(ctx, line)) {
            app->selected_forbidden_region = i;
        }
    }

    if (app->selected_forbidden_region >= 0 &&
        app->selected_forbidden_region < app->forbidden_region_count) {
        dg_rect_t *selected = &app->forbidden_regions[app->selected_forbidden_region];

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Selected Region", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "X", -2048, &selected->x, 2048, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Y", -2048, &selected->y, 2048, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Width", 1, &selected->width, 2048, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Height", 1, &selected->height, 2048, 1, 0.25f);
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
    if (ctx == NULL || app == NULL) {
        return;
    }

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

    if (nk_tree_push(ctx, NK_TREE_TAB, "Algorithm Settings", NK_MAXIMIZED)) {
        dg_nuklear_draw_algorithm_settings(ctx, app);
        nk_tree_pop(ctx);
    }

    if (nk_tree_push(ctx, NK_TREE_TAB, "Constraints", NK_MINIMIZED)) {
        dg_nuklear_draw_constraints(ctx, app);
        nk_tree_pop(ctx);
    }

    if (nk_tree_push(ctx, NK_TREE_TAB, "Role Requirements", NK_MINIMIZED)) {
        dg_nuklear_draw_role_requirements(ctx, app);
        nk_tree_pop(ctx);
    }

    if (nk_tree_push(ctx, NK_TREE_TAB, "Role Placement Weights", NK_MINIMIZED)) {
        dg_nuklear_draw_role_weights(ctx, app);
        nk_tree_pop(ctx);
    }

    if (nk_tree_push(ctx, NK_TREE_TAB, "Forbidden Regions", NK_MINIMIZED)) {
        dg_nuklear_draw_forbidden_regions(ctx, app);
        nk_tree_pop(ctx);
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
    app->routing_index = 0;
    app->width = 80;
    app->height = 40;
    app->forbidden_region_count = 0;
    app->selected_forbidden_region = -1;

    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ROOMS_AND_CORRIDORS);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ORGANIC_CAVE);
    dg_nuklear_reset_constraint_defaults(app);

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
