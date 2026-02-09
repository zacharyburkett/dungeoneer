#include "core.h"

#include <errno.h>
#include <limits.h>
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

static bool dg_nuklear_parse_int(const char *text, int *out_value)
{
    char *end;
    long parsed;

    if (text == NULL || out_value == NULL) {
        return false;
    }

    errno = 0;
    end = NULL;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }

    *out_value = (int)parsed;
    return true;
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

static void dg_nuklear_generate_map(dg_nuklear_app_t *app)
{
    dg_generate_request_t request;
    dg_map_t generated;
    dg_algorithm_t algorithm;
    int width;
    int height;
    int min_rooms;
    int max_rooms;
    int corridor_width;
    int walk_steps;
    int brush_radius;
    int smoothing_passes;
    uint64_t seed;
    dg_status_t status;

    if (app == NULL) {
        return;
    }

    if (!dg_nuklear_parse_int(app->width_text, &width) ||
        !dg_nuklear_parse_int(app->height_text, &height) ||
        !dg_nuklear_parse_u64(app->seed_text, &seed)) {
        dg_nuklear_set_status(app, "Invalid width/height/seed.");
        return;
    }

    algorithm = dg_nuklear_algorithm_from_index(app->algorithm_index);
    dg_default_generate_request(&request, algorithm, width, height, seed);
    request.constraints.max_generation_attempts = 8;

    if (algorithm == DG_ALGORITHM_ROOMS_AND_CORRIDORS) {
        if (!dg_nuklear_parse_int(app->min_rooms_text, &min_rooms) ||
            !dg_nuklear_parse_int(app->max_rooms_text, &max_rooms) ||
            !dg_nuklear_parse_int(app->corridor_width_text, &corridor_width)) {
            dg_nuklear_set_status(app, "Invalid rooms settings.");
            return;
        }

        request.params.rooms.min_rooms = min_rooms;
        request.params.rooms.max_rooms = max_rooms;
        request.params.rooms.corridor_width = corridor_width;
        request.params.rooms.corridor_routing = dg_nuklear_routing_from_index(app->routing_index);
    } else {
        if (!dg_nuklear_parse_int(app->walk_steps_text, &walk_steps) ||
            !dg_nuklear_parse_int(app->brush_radius_text, &brush_radius) ||
            !dg_nuklear_parse_int(app->smoothing_passes_text, &smoothing_passes)) {
            dg_nuklear_set_status(app, "Invalid organic settings.");
            return;
        }

        request.params.organic.walk_steps = walk_steps;
        request.params.organic.brush_radius = brush_radius;
        request.params.organic.smoothing_passes = smoothing_passes;
        request.params.organic.target_floor_coverage = app->target_floor_coverage;
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
    dg_nuklear_set_status(app, "Generated %dx%d map.", app->map.width, app->map.height);
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

    (void)snprintf(line, sizeof(line), "rooms: %llu", (unsigned long long)app->map.metadata.room_count);
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(
        line,
        sizeof(line),
        "corridors: %llu",
        (unsigned long long)app->map.metadata.corridor_count
    );
    nk_label(ctx, line, NK_TEXT_LEFT);

    (void)snprintf(
        line,
        sizeof(line),
        "walkable tiles: %llu",
        (unsigned long long)app->map.metadata.walkable_tile_count
    );
    nk_label(ctx, line, NK_TEXT_LEFT);
}

static void dg_nuklear_draw_controls(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    static const char *algorithms[] = {"Rooms + Corridors", "Organic Cave"};
    static const char *routing_modes[] = {"Random", "Horizontal First", "Vertical First"};

    if (ctx == NULL || app == NULL) {
        return;
    }

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Algorithm", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    app->algorithm_index = nk_combo(
        ctx,
        algorithms,
        (int)(sizeof(algorithms) / sizeof(algorithms[0])),
        app->algorithm_index,
        25,
        nk_vec2(300, 180)
    );

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Width", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    (void)nk_edit_string_zero_terminated(
        ctx,
        NK_EDIT_FIELD,
        app->width_text,
        (int)sizeof(app->width_text),
        nk_filter_decimal
    );

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Height", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    (void)nk_edit_string_zero_terminated(
        ctx,
        NK_EDIT_FIELD,
        app->height_text,
        (int)sizeof(app->height_text),
        nk_filter_decimal
    );

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

    if (app->algorithm_index == 0) {
        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Rooms Settings", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Min Rooms", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        (void)nk_edit_string_zero_terminated(
            ctx,
            NK_EDIT_FIELD,
            app->min_rooms_text,
            (int)sizeof(app->min_rooms_text),
            nk_filter_decimal
        );

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Max Rooms", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        (void)nk_edit_string_zero_terminated(
            ctx,
            NK_EDIT_FIELD,
            app->max_rooms_text,
            (int)sizeof(app->max_rooms_text),
            nk_filter_decimal
        );

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Corridor Width", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        (void)nk_edit_string_zero_terminated(
            ctx,
            NK_EDIT_FIELD,
            app->corridor_width_text,
            (int)sizeof(app->corridor_width_text),
            nk_filter_decimal
        );

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Routing", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        app->routing_index = nk_combo(
            ctx,
            routing_modes,
            (int)(sizeof(routing_modes) / sizeof(routing_modes[0])),
            app->routing_index,
            25,
            nk_vec2(300, 140)
        );
    } else {
        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Organic Settings", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Walk Steps", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        (void)nk_edit_string_zero_terminated(
            ctx,
            NK_EDIT_FIELD,
            app->walk_steps_text,
            (int)sizeof(app->walk_steps_text),
            nk_filter_decimal
        );

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Brush Radius", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        (void)nk_edit_string_zero_terminated(
            ctx,
            NK_EDIT_FIELD,
            app->brush_radius_text,
            (int)sizeof(app->brush_radius_text),
            nk_filter_decimal
        );

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Smoothing Passes", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        (void)nk_edit_string_zero_terminated(
            ctx,
            NK_EDIT_FIELD,
            app->smoothing_passes_text,
            (int)sizeof(app->smoothing_passes_text),
            nk_filter_decimal
        );

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Target Floor Coverage", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        (void)nk_slider_float(ctx, 0.0f, &app->target_floor_coverage, 0.9f, 0.01f);
    }

    nk_layout_row_dynamic(ctx, 32.0f, 2);
    if (nk_button_label(ctx, "Generate")) {
        dg_nuklear_generate_map(app);
    }
    if (nk_button_label(ctx, "Clear")) {
        dg_nuklear_destroy_map(app);
        dg_nuklear_set_status(app, "Cleared map.");
    }

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

    nk_layout_row_dynamic(ctx, 56.0f, 1);
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
    app->target_floor_coverage = 0.30f;
    (void)snprintf(app->width_text, sizeof(app->width_text), "80");
    (void)snprintf(app->height_text, sizeof(app->height_text), "40");
    (void)snprintf(app->seed_text, sizeof(app->seed_text), "1337");
    (void)snprintf(app->min_rooms_text, sizeof(app->min_rooms_text), "8");
    (void)snprintf(app->max_rooms_text, sizeof(app->max_rooms_text), "12");
    (void)snprintf(app->corridor_width_text, sizeof(app->corridor_width_text), "1");
    (void)snprintf(app->walk_steps_text, sizeof(app->walk_steps_text), "2000");
    (void)snprintf(app->brush_radius_text, sizeof(app->brush_radius_text), "1");
    (void)snprintf(app->smoothing_passes_text, sizeof(app->smoothing_passes_text), "2");
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
    const float left_width = 360.0f;
    const float metadata_height = 180.0f;
    float right_x;
    float right_width;
    float map_height;
    struct nk_rect controls_rect;
    struct nk_rect map_rect;
    struct nk_rect metadata_rect;

    if (ctx == NULL || app == NULL || screen_width <= 0 || screen_height <= 0) {
        return;
    }

    right_x = margin + left_width + margin;
    right_width = (float)screen_width - right_x - margin;
    map_height = (float)screen_height - (margin * 3.0f) - metadata_height;
    if (right_width < 260.0f) {
        right_width = 260.0f;
    }
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
    metadata_rect = nk_rect(right_x, margin * 2.0f + map_height, right_width, metadata_height);

    if (nk_begin(
            ctx,
            "Dungeoneer Controls",
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
