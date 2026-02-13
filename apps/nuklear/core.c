#include "core.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static uint64_t dg_nuklear_hash_bytes(uint64_t hash, const void *data, size_t byte_count)
{
    const unsigned char *bytes;
    size_t i;

    if (data == NULL || byte_count == 0u) {
        return hash;
    }

    bytes = (const unsigned char *)data;
    for (i = 0; i < byte_count; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= UINT64_C(1099511628211);
    }

    return hash;
}

static uint64_t dg_nuklear_hash_i32(uint64_t hash, int value)
{
    return dg_nuklear_hash_bytes(hash, &value, sizeof(value));
}

static uint64_t dg_nuklear_hash_u64(uint64_t hash, uint64_t value)
{
    return dg_nuklear_hash_bytes(hash, &value, sizeof(value));
}

static uint64_t dg_nuklear_hash_cstr(uint64_t hash, const char *value)
{
    if (value == NULL) {
        return dg_nuklear_hash_i32(hash, 0);
    }

    return dg_nuklear_hash_bytes(hash, value, strlen(value));
}

static uint64_t dg_nuklear_mix_u64(uint64_t value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    if (value == 0u) {
        value = 1u;
    }
    return value;
}

static uint64_t dg_nuklear_make_random_seed(const dg_nuklear_app_t *app)
{
    static uint64_t nonce = UINT64_C(0x1234a5b6c7d8e9f0);
    uint64_t prior_seed;
    uint64_t entropy;

    prior_seed = 0u;
    if (app != NULL) {
        (void)dg_nuklear_parse_u64(app->seed_text, &prior_seed);
    }

    entropy = (uint64_t)(unsigned long long)time(NULL);
    entropy ^= ((uint64_t)(unsigned long long)clock()) << 32;
    entropy ^= prior_seed;
    entropy ^= nonce;
    if (app != NULL) {
        entropy ^= (uint64_t)(uintptr_t)app;
    }
    nonce += UINT64_C(0x9e3779b97f4a7c15);

    return dg_nuklear_mix_u64(entropy);
}

static const dg_algorithm_t DG_NUKLEAR_ALGORITHMS[] = {
    DG_ALGORITHM_BSP_TREE,
    DG_ALGORITHM_ROOM_GRAPH,
    DG_ALGORITHM_ROOMS_AND_MAZES,
    DG_ALGORITHM_DRUNKARDS_WALK,
    DG_ALGORITHM_WORM_CAVES,
    DG_ALGORITHM_CELLULAR_AUTOMATA,
    DG_ALGORITHM_VALUE_NOISE,
    DG_ALGORITHM_SIMPLEX_NOISE
};

#define DG_NUKLEAR_ALGORITHM_COUNT \
    ((int)(sizeof(DG_NUKLEAR_ALGORITHMS) / sizeof(DG_NUKLEAR_ALGORITHMS[0])))

static dg_map_generation_class_t dg_nuklear_generation_class_from_index(int class_index)
{
    if (class_index == 1) {
        return DG_MAP_GENERATION_CLASS_CAVE_LIKE;
    }
    return DG_MAP_GENERATION_CLASS_ROOM_LIKE;
}

static int dg_nuklear_generation_class_index_from_class(dg_map_generation_class_t generation_class)
{
    if (generation_class == DG_MAP_GENERATION_CLASS_CAVE_LIKE) {
        return 1;
    }
    return 0;
}

static dg_algorithm_t dg_nuklear_algorithm_from_index(int algorithm_index)
{
    if (algorithm_index < 0 || algorithm_index >= DG_NUKLEAR_ALGORITHM_COUNT) {
        return DG_NUKLEAR_ALGORITHMS[0];
    }
    return DG_NUKLEAR_ALGORITHMS[algorithm_index];
}

static int dg_nuklear_algorithm_index_from_id(int algorithm_id)
{
    int i;

    for (i = 0; i < DG_NUKLEAR_ALGORITHM_COUNT; ++i) {
        if ((int)DG_NUKLEAR_ALGORITHMS[i] == algorithm_id) {
            return i;
        }
    }

    return 0;
}

static const char *dg_nuklear_algorithm_name(dg_algorithm_t algorithm)
{
    switch (algorithm) {
    case DG_ALGORITHM_SIMPLEX_NOISE:
        return "simplex_noise";
    case DG_ALGORITHM_WORM_CAVES:
        return "worm_caves";
    case DG_ALGORITHM_ROOM_GRAPH:
        return "room_graph";
    case DG_ALGORITHM_VALUE_NOISE:
        return "value_noise";
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        return "cellular_automata";
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        return "rooms_and_mazes";
    case DG_ALGORITHM_DRUNKARDS_WALK:
        return "drunkards_walk";
    case DG_ALGORITHM_BSP_TREE:
    default:
        return "bsp_tree";
    }
}

static const char *dg_nuklear_algorithm_display_name(dg_algorithm_t algorithm)
{
    switch (algorithm) {
    case DG_ALGORITHM_ROOM_GRAPH:
        return "Room Graph (MST)";
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        return "Rooms + Mazes";
    case DG_ALGORITHM_WORM_CAVES:
        return "Worm Caves";
    case DG_ALGORITHM_DRUNKARDS_WALK:
        return "Drunkard's Walk";
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        return "Cellular Automata";
    case DG_ALGORITHM_VALUE_NOISE:
        return "Value Noise";
    case DG_ALGORITHM_SIMPLEX_NOISE:
        return "Simplex Noise";
    case DG_ALGORITHM_BSP_TREE:
    default:
        return "BSP Tree";
    }
}

static int dg_nuklear_first_algorithm_index_for_class(dg_map_generation_class_t generation_class)
{
    int i;

    for (i = 0; i < DG_NUKLEAR_ALGORITHM_COUNT; ++i) {
        if (dg_algorithm_generation_class(DG_NUKLEAR_ALGORITHMS[i]) == generation_class) {
            return i;
        }
    }

    return 0;
}

static int dg_nuklear_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void dg_nuklear_apply_theme(struct nk_context *ctx)
{
    struct nk_color table[NK_COLOR_COUNT];

    if (ctx == NULL) {
        return;
    }

    table[NK_COLOR_TEXT] = nk_rgb(223, 228, 236);
    table[NK_COLOR_WINDOW] = nk_rgb(18, 23, 30);
    table[NK_COLOR_HEADER] = nk_rgb(33, 41, 52);
    table[NK_COLOR_BORDER] = nk_rgb(67, 81, 100);
    table[NK_COLOR_BUTTON] = nk_rgb(48, 59, 73);
    table[NK_COLOR_BUTTON_HOVER] = nk_rgb(62, 77, 95);
    table[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(77, 94, 116);
    table[NK_COLOR_TOGGLE] = nk_rgb(51, 63, 79);
    table[NK_COLOR_TOGGLE_HOVER] = nk_rgb(63, 78, 96);
    table[NK_COLOR_TOGGLE_CURSOR] = nk_rgb(162, 208, 189);
    table[NK_COLOR_SELECT] = nk_rgb(67, 90, 116);
    table[NK_COLOR_SELECT_ACTIVE] = nk_rgb(87, 120, 152);
    table[NK_COLOR_SLIDER] = nk_rgb(47, 58, 72);
    table[NK_COLOR_SLIDER_CURSOR] = nk_rgb(159, 205, 187);
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgb(184, 224, 209);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgb(204, 236, 224);
    table[NK_COLOR_PROPERTY] = nk_rgb(40, 50, 64);
    table[NK_COLOR_EDIT] = nk_rgb(32, 40, 51);
    table[NK_COLOR_EDIT_CURSOR] = nk_rgb(229, 236, 245);
    table[NK_COLOR_COMBO] = nk_rgb(33, 41, 53);
    table[NK_COLOR_CHART] = nk_rgb(40, 50, 64);
    table[NK_COLOR_CHART_COLOR] = nk_rgb(159, 205, 187);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgb(222, 170, 97);
    table[NK_COLOR_SCROLLBAR] = nk_rgb(30, 37, 47);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgb(71, 88, 108);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgb(86, 106, 129);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgb(104, 127, 155);
    table[NK_COLOR_TAB_HEADER] = nk_rgb(30, 37, 47);

    nk_style_from_table(ctx, table);

    ctx->style.window.border = 1.0f;
    ctx->style.window.rounding = 6.0f;
    ctx->style.window.spacing = nk_vec2(8.0f, 7.0f);
    ctx->style.window.padding = nk_vec2(10.0f, 10.0f);
    ctx->style.window.group_padding = nk_vec2(8.0f, 8.0f);
    ctx->style.window.min_row_height_padding = 2.0f;
    ctx->style.window.scrollbar_size = nk_vec2(13.0f, 13.0f);

    ctx->style.button.border = 1.0f;
    ctx->style.button.rounding = 5.0f;
    ctx->style.button.padding = nk_vec2(8.0f, 6.0f);
    ctx->style.button.text_alignment = NK_TEXT_CENTERED;

    ctx->style.combo.border = 1.0f;
    ctx->style.combo.rounding = 5.0f;
    ctx->style.combo.content_padding = nk_vec2(8.0f, 5.0f);
    ctx->style.combo.button_padding = nk_vec2(6.0f, 4.0f);
    ctx->style.combo.spacing = nk_vec2(5.0f, 5.0f);

    ctx->style.property.border = 1.0f;
    ctx->style.property.rounding = 5.0f;
    ctx->style.property.padding = nk_vec2(6.0f, 5.0f);

    ctx->style.edit.border = 1.0f;
    ctx->style.edit.rounding = 4.0f;
    ctx->style.edit.padding = nk_vec2(8.0f, 6.0f);
    ctx->style.edit.row_padding = 4.0f;
    ctx->style.edit.scrollbar_size = nk_vec2(10.0f, 10.0f);

    ctx->style.selectable.rounding = 4.0f;
    ctx->style.selectable.padding = nk_vec2(8.0f, 5.0f);
    ctx->style.selectable.text_alignment = NK_TEXT_LEFT;

    ctx->style.checkbox.spacing = 6.0f;
    ctx->style.option.spacing = 6.0f;

    ctx->style.scrollv.rounding = 6.0f;
    ctx->style.scrollv.rounding_cursor = 6.0f;
}

static void dg_nuklear_draw_subsection_heading(
    struct nk_context *ctx,
    const char *title,
    const char *hint
)
{
    if (ctx == NULL || title == NULL) {
        return;
    }

    nk_layout_row_dynamic(ctx, 19.0f, 1);
    nk_label(ctx, title, NK_TEXT_LEFT);

    if (hint != NULL && hint[0] != '\0') {
        nk_layout_row_dynamic(ctx, 32.0f, 1);
        nk_label_wrap(ctx, hint);
    }
}

static const char *dg_nuklear_process_method_label(dg_process_method_type_t type)
{
    switch (type) {
    case DG_PROCESS_METHOD_SCALE:
        return "General: Scale";
    case DG_PROCESS_METHOD_ROOM_SHAPE:
        return "Room: Shape";
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        return "Corridor: Path Smoothing";
    case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
        return "Corridor: Roughen";
    default:
        return "Unknown";
    }
}

static int dg_nuklear_process_type_to_ui_index(dg_process_method_type_t type)
{
    switch (type) {
    case DG_PROCESS_METHOD_ROOM_SHAPE:
        return 1;
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        return 2;
    case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
        return 3;
    case DG_PROCESS_METHOD_SCALE:
    default:
        return 0;
    }
}

static dg_process_method_type_t dg_nuklear_process_ui_index_to_type(int index)
{
    switch (index) {
    case 1:
        return DG_PROCESS_METHOD_ROOM_SHAPE;
    case 2:
        return DG_PROCESS_METHOD_PATH_SMOOTH;
    case 3:
        return DG_PROCESS_METHOD_CORRIDOR_ROUGHEN;
    case 0:
    default:
        return DG_PROCESS_METHOD_SCALE;
    }
}

static const char *DG_NUKLEAR_PROCESS_METHOD_TYPES[] = {
    "General: Scale",
    "Room: Shape",
    "Corridor: Path Smoothing",
    "Corridor: Roughen"
};

static const char *DG_NUKLEAR_ROOM_SHAPE_MODES[] = {
    "Rectangular",
    "Organic (Noise Blob)",
    "Cellular",
    "Chamfered"
};

static const char *DG_NUKLEAR_CORRIDOR_ROUGHEN_MODES[] = {
    "Uniform",
    "Organic"
};

static int dg_nuklear_room_shape_mode_to_ui_index(dg_room_shape_mode_t mode)
{
    switch (mode) {
    case DG_ROOM_SHAPE_RECTANGULAR:
        return 0;
    case DG_ROOM_SHAPE_ORGANIC:
        return 1;
    case DG_ROOM_SHAPE_CELLULAR:
        return 2;
    case DG_ROOM_SHAPE_CHAMFERED:
        return 3;
    default:
        return 1;
    }
}

static dg_room_shape_mode_t dg_nuklear_room_shape_ui_index_to_mode(int index)
{
    switch (index) {
    case 0:
        return DG_ROOM_SHAPE_RECTANGULAR;
    case 1:
        return DG_ROOM_SHAPE_ORGANIC;
    case 2:
        return DG_ROOM_SHAPE_CELLULAR;
    case 3:
        return DG_ROOM_SHAPE_CHAMFERED;
    default:
        return DG_ROOM_SHAPE_ORGANIC;
    }
}

static void dg_nuklear_sanitize_process_method(dg_process_method_t *method)
{
    if (method == NULL) {
        return;
    }

    switch (method->type) {
    case DG_PROCESS_METHOD_SCALE:
        method->params.scale.factor = dg_nuklear_clamp_int(method->params.scale.factor, 1, 8);
        break;
    case DG_PROCESS_METHOD_ROOM_SHAPE:
        if (method->params.room_shape.mode != DG_ROOM_SHAPE_RECTANGULAR &&
            method->params.room_shape.mode != DG_ROOM_SHAPE_ORGANIC &&
            method->params.room_shape.mode != DG_ROOM_SHAPE_CELLULAR &&
            method->params.room_shape.mode != DG_ROOM_SHAPE_CHAMFERED) {
            method->params.room_shape.mode = DG_ROOM_SHAPE_ORGANIC;
        }
        method->params.room_shape.organicity =
            dg_nuklear_clamp_int(method->params.room_shape.organicity, 0, 100);
        break;
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        method->params.path_smooth.strength =
            dg_nuklear_clamp_int(method->params.path_smooth.strength, 0, 12);
        method->params.path_smooth.inner_enabled =
            method->params.path_smooth.inner_enabled ? 1 : 0;
        method->params.path_smooth.outer_enabled =
            method->params.path_smooth.outer_enabled ? 1 : 0;
        break;
    case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
        method->params.corridor_roughen.strength =
            dg_nuklear_clamp_int(method->params.corridor_roughen.strength, 0, 100);
        method->params.corridor_roughen.max_depth =
            dg_nuklear_clamp_int(method->params.corridor_roughen.max_depth, 1, 32);
        if (method->params.corridor_roughen.mode != DG_CORRIDOR_ROUGHEN_UNIFORM &&
            method->params.corridor_roughen.mode != DG_CORRIDOR_ROUGHEN_ORGANIC) {
            method->params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_ORGANIC;
        }
        break;
    default:
        dg_default_process_method(method, DG_PROCESS_METHOD_SCALE);
        break;
    }
}

static void dg_nuklear_sanitize_process_settings(dg_nuklear_app_t *app)
{
    int i;

    if (app == NULL) {
        return;
    }

    app->process_method_count = dg_nuklear_clamp_int(
        app->process_method_count,
        0,
        DG_NUKLEAR_MAX_PROCESS_METHODS
    );
    app->process_add_method_type_index =
        dg_nuklear_clamp_int(app->process_add_method_type_index, 0, 3);
    app->process_enabled = app->process_enabled ? 1 : 0;

    for (i = 0; i < app->process_method_count; ++i) {
        dg_nuklear_sanitize_process_method(&app->process_methods[i]);
    }

    if (app->process_method_count <= 0) {
        app->process_selected_index = -1;
    } else {
        app->process_selected_index = dg_nuklear_clamp_int(
            app->process_selected_index,
            0,
            app->process_method_count - 1
        );
    }
}

static void dg_nuklear_reset_process_defaults(dg_nuklear_app_t *app)
{
    if (app == NULL) {
        return;
    }

    app->process_method_count = 0;
    app->process_add_method_type_index = 0;
    app->process_selected_index = -1;
    app->process_enabled = 1;
}

static bool dg_nuklear_append_process_method(
    dg_nuklear_app_t *app,
    dg_process_method_type_t type
)
{
    if (app == NULL || app->process_method_count >= DG_NUKLEAR_MAX_PROCESS_METHODS) {
        return false;
    }

    dg_default_process_method(&app->process_methods[app->process_method_count], type);
    app->process_method_count += 1;
    app->process_selected_index = app->process_method_count - 1;
    dg_nuklear_sanitize_process_settings(app);
    return true;
}

static bool dg_nuklear_remove_process_method(dg_nuklear_app_t *app, int index)
{
    int i;

    if (app == NULL || index < 0 || index >= app->process_method_count) {
        return false;
    }

    for (i = index; i < app->process_method_count - 1; ++i) {
        app->process_methods[i] = app->process_methods[i + 1];
    }
    app->process_method_count -= 1;
    if (app->process_method_count <= 0) {
        app->process_selected_index = -1;
    } else if (app->process_selected_index > index) {
        app->process_selected_index -= 1;
    } else if (app->process_selected_index >= app->process_method_count) {
        app->process_selected_index = app->process_method_count - 1;
    }
    dg_nuklear_sanitize_process_settings(app);
    return true;
}

static bool dg_nuklear_move_process_method(dg_nuklear_app_t *app, int index, int direction)
{
    int target;
    dg_process_method_t temp;

    if (app == NULL || index < 0 || index >= app->process_method_count) {
        return false;
    }

    target = index + direction;
    if (target < 0 || target >= app->process_method_count) {
        return false;
    }

    temp = app->process_methods[index];
    app->process_methods[index] = app->process_methods[target];
    app->process_methods[target] = temp;
    if (app->process_selected_index == index) {
        app->process_selected_index = target;
    } else if (app->process_selected_index == target) {
        app->process_selected_index = index;
    }
    dg_nuklear_sanitize_process_settings(app);
    return true;
}

static bool dg_nuklear_duplicate_process_method(dg_nuklear_app_t *app, int index)
{
    int i;

    if (app == NULL || index < 0 || index >= app->process_method_count) {
        return false;
    }
    if (app->process_method_count >= DG_NUKLEAR_MAX_PROCESS_METHODS) {
        return false;
    }

    for (i = app->process_method_count; i > index + 1; --i) {
        app->process_methods[i] = app->process_methods[i - 1];
    }
    app->process_methods[index + 1] = app->process_methods[index];
    app->process_method_count += 1;
    app->process_selected_index = index + 1;
    dg_nuklear_sanitize_process_settings(app);
    return true;
}

static bool dg_nuklear_algorithm_supports_room_types(dg_algorithm_t algorithm)
{
    return algorithm == DG_ALGORITHM_BSP_TREE ||
           algorithm == DG_ALGORITHM_ROOMS_AND_MAZES ||
           algorithm == DG_ALGORITHM_ROOM_GRAPH;
}

static void dg_nuklear_sync_generation_class_with_algorithm(dg_nuklear_app_t *app)
{
    dg_algorithm_t algorithm;
    dg_map_generation_class_t generation_class;

    if (app == NULL) {
        return;
    }

    algorithm = dg_nuklear_algorithm_from_index(app->algorithm_index);
    generation_class = dg_algorithm_generation_class(algorithm);
    app->generation_class_index =
        dg_nuklear_generation_class_index_from_class(generation_class);
}

static void dg_nuklear_ensure_algorithm_matches_class(dg_nuklear_app_t *app)
{
    dg_algorithm_t algorithm;
    dg_map_generation_class_t selected_class;

    if (app == NULL) {
        return;
    }

    algorithm = dg_nuklear_algorithm_from_index(app->algorithm_index);
    selected_class = dg_nuklear_generation_class_from_index(app->generation_class_index);
    if (dg_algorithm_generation_class(algorithm) != selected_class) {
        app->algorithm_index = dg_nuklear_first_algorithm_index_for_class(selected_class);
    }
}

static float dg_nuklear_min_float(float a, float b)
{
    return (a < b) ? a : b;
}

static float dg_nuklear_max_float(float a, float b)
{
    return (a > b) ? a : b;
}

static float dg_nuklear_clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int dg_nuklear_floor_to_int(float value)
{
    int truncated = (int)value;
    if ((float)truncated > value) {
        truncated -= 1;
    }
    return truncated;
}

static int dg_nuklear_ceil_to_int(float value)
{
    int truncated = (int)value;
    if ((float)truncated < value) {
        truncated += 1;
    }
    return truncated;
}

static struct nk_rect dg_nuklear_rect_intersection(struct nk_rect a, struct nk_rect b)
{
    float left = dg_nuklear_max_float(a.x, b.x);
    float top = dg_nuklear_max_float(a.y, b.y);
    float right = dg_nuklear_min_float(a.x + a.w, b.x + b.w);
    float bottom = dg_nuklear_min_float(a.y + a.h, b.y + b.h);

    if (right <= left || bottom <= top) {
        return nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    }

    return nk_rect(left, top, right - left, bottom - top);
}

static void dg_nuklear_draw_splitter_overlay(
    struct nk_context *ctx,
    const char *name,
    struct nk_rect rect,
    int vertical,
    int active,
    int hovered
)
{
    struct nk_command_buffer *canvas;
    struct nk_rect bounds;
    struct nk_rect track_rect;
    struct nk_color track_color;
    struct nk_color grip_color;
    float grip_half_span;
    float track_thickness;
    float center_x;
    float center_y;
    float t0;
    float t1;
    nk_flags flags;

    if (ctx == NULL || name == NULL || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }

    flags = NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND;
    if (!nk_begin(ctx, name, rect, flags)) {
        nk_end(ctx);
        return;
    }

    canvas = nk_window_get_canvas(ctx);
    bounds = nk_window_get_bounds(ctx);
    if (canvas == NULL || bounds.w <= 0.0f || bounds.h <= 0.0f) {
        nk_end(ctx);
        return;
    }

    if (active) {
        track_color = nk_rgba(92, 108, 138, 220);
        grip_color = nk_rgba(220, 230, 255, 240);
    } else if (hovered) {
        track_color = nk_rgba(80, 92, 116, 190);
        grip_color = nk_rgba(210, 218, 240, 220);
    } else {
        track_color = nk_rgba(65, 74, 92, 148);
        grip_color = nk_rgba(190, 200, 220, 165);
    }

    if (vertical) {
        track_rect = bounds;
    } else {
        track_thickness = dg_nuklear_clamp_float(bounds.h * 0.16f, 1.0f, 2.0f);
        track_rect = nk_rect(
            bounds.x,
            bounds.y + (bounds.h - track_thickness) * 0.5f,
            bounds.w,
            track_thickness
        );
    }
    nk_fill_rect(canvas, track_rect, 0.0f, track_color);

    center_x = bounds.x + bounds.w * 0.5f;
    center_y = bounds.y + bounds.h * 0.5f;
    grip_half_span = vertical ? bounds.h * 0.16f : bounds.w * 0.16f;
    grip_half_span = dg_nuklear_clamp_float(grip_half_span, 10.0f, 28.0f);

    if (vertical) {
        t0 = center_y - grip_half_span;
        t1 = center_y + grip_half_span;
        nk_stroke_line(canvas, center_x - 2.0f, t0, center_x - 2.0f, t1, 1.0f, grip_color);
        nk_stroke_line(canvas, center_x, t0, center_x, t1, 1.0f, grip_color);
        nk_stroke_line(canvas, center_x + 2.0f, t0, center_x + 2.0f, t1, 1.0f, grip_color);
    } else {
        t0 = center_x - grip_half_span;
        t1 = center_x + grip_half_span;
        nk_stroke_line(canvas, t0, center_y - 0.5f, t1, center_y - 0.5f, 1.0f, grip_color);
        nk_stroke_line(canvas, t0, center_y, t1, center_y, 1.0f, grip_color);
        nk_stroke_line(canvas, t0, center_y + 0.5f, t1, center_y + 0.5f, 1.0f, grip_color);
    }

    nk_end(ctx);
}

static void dg_nuklear_default_room_type_slot(dg_nuklear_room_type_ui_t *slot, int index)
{
    if (slot == NULL) {
        return;
    }

    memset(slot, 0, sizeof(*slot));
    slot->enabled = 1;
    slot->type_id = (index + 1) * 100;
    slot->min_count = 0;
    slot->max_count = -1;
    slot->target_count = -1;
    slot->area_min = 0;
    slot->area_max = -1;
    slot->degree_min = 0;
    slot->degree_max = -1;
    slot->border_distance_min = 0;
    slot->border_distance_max = -1;
    slot->graph_depth_min = 0;
    slot->graph_depth_max = -1;
    slot->weight = 1;
    slot->larger_room_bias = 0;
    slot->higher_degree_bias = 0;
    slot->border_distance_bias = 0;

    if (index == 0) {
        (void)snprintf(slot->label, sizeof(slot->label), "Large Halls");
        slot->min_count = 1;
        slot->weight = 2;
        slot->larger_room_bias = 35;
    } else if (index == 1) {
        (void)snprintf(slot->label, sizeof(slot->label), "Junctions");
        slot->min_count = 1;
        slot->weight = 2;
        slot->higher_degree_bias = 50;
    } else if (index == 2) {
        (void)snprintf(slot->label, sizeof(slot->label), "Inner Rooms");
        slot->min_count = 1;
        slot->weight = 2;
        slot->border_distance_bias = 45;
    } else {
        (void)snprintf(slot->label, sizeof(slot->label), "Type %d", index + 1);
    }
}

static void dg_nuklear_clamp_unbounded_range(int *min_value, int *max_value)
{
    if (min_value == NULL || max_value == NULL) {
        return;
    }

    if (*min_value < 0) {
        *min_value = 0;
    }

    if (*max_value != -1 && *max_value < *min_value) {
        *max_value = *min_value;
    }
}

static void dg_nuklear_sanitize_room_type_slot(dg_nuklear_room_type_ui_t *slot)
{
    if (slot == NULL) {
        return;
    }

    slot->enabled = slot->enabled ? 1 : 0;
    slot->type_id = dg_nuklear_clamp_int(slot->type_id, 0, INT_MAX);

    slot->min_count = dg_nuklear_clamp_int(slot->min_count, 0, INT_MAX);
    if (slot->max_count < -1) {
        slot->max_count = -1;
    }
    if (slot->max_count != -1 && slot->max_count < slot->min_count) {
        slot->max_count = slot->min_count;
    }

    if (slot->target_count < -1) {
        slot->target_count = -1;
    }
    if (slot->target_count != -1) {
        if (slot->target_count < slot->min_count) {
            slot->target_count = slot->min_count;
        }
        if (slot->max_count != -1 && slot->target_count > slot->max_count) {
            slot->target_count = slot->max_count;
        }
    }

    dg_nuklear_clamp_unbounded_range(&slot->area_min, &slot->area_max);
    dg_nuklear_clamp_unbounded_range(&slot->degree_min, &slot->degree_max);
    dg_nuklear_clamp_unbounded_range(&slot->border_distance_min, &slot->border_distance_max);
    dg_nuklear_clamp_unbounded_range(&slot->graph_depth_min, &slot->graph_depth_max);

    slot->weight = dg_nuklear_clamp_int(slot->weight, 0, INT_MAX);
    slot->larger_room_bias = dg_nuklear_clamp_int(slot->larger_room_bias, -100, 100);
    slot->higher_degree_bias = dg_nuklear_clamp_int(slot->higher_degree_bias, -100, 100);
    slot->border_distance_bias = dg_nuklear_clamp_int(slot->border_distance_bias, -100, 100);
}

static void dg_nuklear_reset_room_type_defaults(dg_nuklear_app_t *app)
{
    int i;

    if (app == NULL) {
        return;
    }

    app->room_types_enabled = 0;
    app->room_type_count = 3;
    app->room_type_strict_mode = 0;
    app->room_type_allow_untyped = 1;
    app->room_type_default_type_id = 100;

    for (i = 0; i < DG_NUKLEAR_MAX_ROOM_TYPES; ++i) {
        dg_nuklear_default_room_type_slot(&app->room_type_slots[i], i);
    }
}

static void dg_nuklear_sanitize_room_type_settings(dg_nuklear_app_t *app)
{
    int i;
    int first_enabled_type_id;
    bool has_default_enabled;

    if (app == NULL) {
        return;
    }

    app->room_types_enabled = app->room_types_enabled ? 1 : 0;
    app->room_type_count = dg_nuklear_clamp_int(app->room_type_count, 1, DG_NUKLEAR_MAX_ROOM_TYPES);
    app->room_type_strict_mode = app->room_type_strict_mode ? 1 : 0;
    app->room_type_allow_untyped = app->room_type_allow_untyped ? 1 : 0;
    app->room_type_default_type_id = dg_nuklear_clamp_int(app->room_type_default_type_id, 0, INT_MAX);

    first_enabled_type_id = -1;
    has_default_enabled = false;
    for (i = 0; i < app->room_type_count; ++i) {
        dg_nuklear_sanitize_room_type_slot(&app->room_type_slots[i]);
        if (app->room_type_slots[i].enabled) {
            if (first_enabled_type_id < 0) {
                first_enabled_type_id = app->room_type_slots[i].type_id;
            }
            if (app->room_type_slots[i].type_id == app->room_type_default_type_id) {
                has_default_enabled = true;
            }
        }
    }

    if (!app->room_type_allow_untyped && app->room_types_enabled) {
        if (first_enabled_type_id < 0) {
            app->room_type_slots[0].enabled = 1;
            first_enabled_type_id = app->room_type_slots[0].type_id;
            has_default_enabled = (app->room_type_default_type_id == first_enabled_type_id);
        }
        if (!has_default_enabled) {
            app->room_type_default_type_id = first_enabled_type_id;
        }
    }
}

static bool dg_nuklear_room_type_id_exists(const dg_nuklear_app_t *app, int type_id, int ignore_index)
{
    int i;

    if (app == NULL) {
        return false;
    }

    for (i = 0; i < app->room_type_count; ++i) {
        if (i == ignore_index) {
            continue;
        }
        if (app->room_type_slots[i].type_id == type_id) {
            return true;
        }
    }

    return false;
}

static int dg_nuklear_next_room_type_id(const dg_nuklear_app_t *app)
{
    int candidate;
    int max_id;
    int i;

    if (app == NULL) {
        return 1;
    }

    max_id = 0;
    for (i = 0; i < app->room_type_count; ++i) {
        if (app->room_type_slots[i].type_id > max_id) {
            max_id = app->room_type_slots[i].type_id;
        }
    }

    if (max_id >= INT_MAX - 1) {
        return INT_MAX;
    }
    candidate = max_id + 1;

    while (candidate < INT_MAX && dg_nuklear_room_type_id_exists(app, candidate, -1)) {
        candidate += 1;
    }

    return candidate;
}

static bool dg_nuklear_append_room_type_slot(dg_nuklear_app_t *app)
{
    int new_index;

    if (app == NULL || app->room_type_count >= DG_NUKLEAR_MAX_ROOM_TYPES) {
        return false;
    }

    new_index = app->room_type_count;
    dg_nuklear_default_room_type_slot(&app->room_type_slots[new_index], new_index);
    app->room_type_slots[new_index].type_id = dg_nuklear_next_room_type_id(app);
    app->room_type_count += 1;
    dg_nuklear_sanitize_room_type_settings(app);
    return true;
}

static bool dg_nuklear_remove_room_type_slot(dg_nuklear_app_t *app, int remove_index)
{
    int i;

    if (app == NULL || app->room_type_count <= 1) {
        return false;
    }

    if (remove_index < 0 || remove_index >= app->room_type_count) {
        return false;
    }

    for (i = remove_index; i < app->room_type_count - 1; ++i) {
        app->room_type_slots[i] = app->room_type_slots[i + 1];
    }
    app->room_type_count -= 1;
    dg_nuklear_default_room_type_slot(&app->room_type_slots[app->room_type_count], app->room_type_count);
    dg_nuklear_sanitize_room_type_settings(app);
    return true;
}

static bool dg_nuklear_point_room_type(const dg_map_t *map, int x, int y, uint32_t *out_type_id)
{
    size_t i;

    if (map == NULL || map->metadata.rooms == NULL) {
        return false;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_rect_t *room = &map->metadata.rooms[i].bounds;
        if (x >= room->x && y >= room->y && x < room->x + room->width && y < room->y + room->height) {
            if (out_type_id != NULL) {
                *out_type_id = map->metadata.rooms[i].type_id;
            }
            return true;
        }
    }

    return false;
}

static struct nk_color dg_nuklear_color_for_room_type(uint32_t type_id)
{
    uint32_t hash;
    unsigned char r;
    unsigned char g;
    unsigned char b;

    hash = type_id * 2654435761u;
    r = (unsigned char)(80u + ((hash >> 0) & 0x5Fu));
    g = (unsigned char)(95u + ((hash >> 8) & 0x5Fu));
    b = (unsigned char)(105u + ((hash >> 16) & 0x5Fu));
    return nk_rgb(r, g, b);
}

static struct nk_color dg_nuklear_tile_color(const dg_map_t *map, int x, int y, dg_tile_t tile)
{
    bool room_floor;
    uint32_t room_type_id;

    room_floor = false;
    room_type_id = DG_ROOM_TYPE_UNASSIGNED;
    if (
        tile == DG_TILE_FLOOR &&
        map != NULL &&
        map->metadata.generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE &&
        map->metadata.room_count > 0
    ) {
        room_floor = dg_nuklear_point_room_type(map, x, y, &room_type_id);
    }

    switch (tile) {
    case DG_TILE_WALL:
        return nk_rgb(48, 54, 66);
    case DG_TILE_FLOOR:
        if (room_floor) {
            if (room_type_id != DG_ROOM_TYPE_UNASSIGNED) {
                return dg_nuklear_color_for_room_type(room_type_id);
            }
            return nk_rgb(112, 176, 221);
        }
        return nk_rgb(188, 196, 173);
    case DG_TILE_DOOR:
        return nk_rgb(224, 176, 85);
    case DG_TILE_VOID:
    default:
        return nk_rgb(18, 22, 28);
    }
}

static void dg_nuklear_free_preview_image_buffer(dg_nuklear_app_t *app)
{
    if (app == NULL) {
        return;
    }

    free(app->preview_image_pixels);
    app->preview_image_pixels = NULL;
    app->preview_image_width = 0;
    app->preview_image_height = 0;
}

static void dg_nuklear_free_preview_tile_colors(dg_nuklear_app_t *app)
{
    if (app == NULL) {
        return;
    }

    free(app->preview_tile_colors);
    app->preview_tile_colors = NULL;
    app->preview_tile_color_width = 0;
    app->preview_tile_color_height = 0;
    app->preview_tile_colors_valid = 0;
}

static bool dg_nuklear_ensure_preview_image_buffer(
    dg_nuklear_app_t *app,
    int image_width,
    int image_height
)
{
    size_t pixel_count;
    size_t byte_count;
    unsigned char *buffer;

    if (app == NULL || image_width <= 0 || image_height <= 0) {
        return false;
    }

    if (app->preview_image_pixels != NULL && app->preview_image_width == image_width &&
        app->preview_image_height == image_height) {
        return true;
    }

    if ((size_t)image_width > (SIZE_MAX / (size_t)image_height)) {
        return false;
    }
    pixel_count = (size_t)image_width * (size_t)image_height;
    if (pixel_count > (SIZE_MAX / 4u)) {
        return false;
    }
    byte_count = pixel_count * 4u;

    buffer = (unsigned char *)malloc(byte_count);
    if (buffer == NULL) {
        return false;
    }

    dg_nuklear_free_preview_image_buffer(app);
    app->preview_image_pixels = buffer;
    app->preview_image_width = image_width;
    app->preview_image_height = image_height;
    return true;
}

static bool dg_nuklear_rebuild_preview_tile_colors(dg_nuklear_app_t *app)
{
    size_t cell_count;
    size_t i;
    unsigned char *tile_colors;
    unsigned char *room_mask;
    uint32_t *room_type_by_tile;
    size_t room_count;

    if (app == NULL || !app->has_map || app->map.tiles == NULL || app->map.width <= 0 || app->map.height <= 0) {
        return false;
    }

    if ((size_t)app->map.width > (SIZE_MAX / (size_t)app->map.height)) {
        return false;
    }
    cell_count = (size_t)app->map.width * (size_t)app->map.height;
    if (cell_count > (SIZE_MAX / 4u)) {
        return false;
    }

    tile_colors = (unsigned char *)malloc(cell_count * 4u);
    if (tile_colors == NULL) {
        return false;
    }

    room_mask = NULL;
    room_type_by_tile = NULL;
    room_count = app->map.metadata.room_count;
    if (
        app->map.metadata.generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE &&
        room_count > 0 &&
        app->map.metadata.rooms != NULL
    ) {
        room_mask = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
        room_type_by_tile = (uint32_t *)malloc(cell_count * sizeof(uint32_t));
        if (room_mask == NULL || room_type_by_tile == NULL) {
            free(tile_colors);
            free(room_mask);
            free(room_type_by_tile);
            return false;
        }
        for (i = 0; i < cell_count; ++i) {
            room_type_by_tile[i] = DG_ROOM_TYPE_UNASSIGNED;
        }

        for (i = 0; i < room_count; ++i) {
            const dg_room_metadata_t *room = &app->map.metadata.rooms[i];
            int x0;
            int y0;
            int x1;
            int y1;
            int x;
            int y;

            x0 = room->bounds.x;
            y0 = room->bounds.y;
            x1 = room->bounds.x + room->bounds.width;
            y1 = room->bounds.y + room->bounds.height;
            if (x0 < 0) {
                x0 = 0;
            }
            if (y0 < 0) {
                y0 = 0;
            }
            if (x1 > app->map.width) {
                x1 = app->map.width;
            }
            if (y1 > app->map.height) {
                y1 = app->map.height;
            }
            if (x0 >= x1 || y0 >= y1) {
                continue;
            }

            for (y = y0; y < y1; ++y) {
                for (x = x0; x < x1; ++x) {
                    size_t tile_index = (size_t)y * (size_t)app->map.width + (size_t)x;
                    if (app->map.tiles[tile_index] != DG_TILE_FLOOR) {
                        continue;
                    }
                    room_mask[tile_index] = 1u;
                    if (room->type_id != DG_ROOM_TYPE_UNASSIGNED &&
                        room_type_by_tile[tile_index] == DG_ROOM_TYPE_UNASSIGNED) {
                        room_type_by_tile[tile_index] = room->type_id;
                    }
                }
            }
        }
    }

    for (i = 0; i < cell_count; ++i) {
        dg_tile_t tile = app->map.tiles[i];
        struct nk_color color;
        unsigned char *dst = tile_colors + (i * 4u);

        if (tile == DG_TILE_FLOOR && room_mask != NULL && room_mask[i] != 0u) {
            if (room_type_by_tile[i] != DG_ROOM_TYPE_UNASSIGNED) {
                color = dg_nuklear_color_for_room_type(room_type_by_tile[i]);
            } else {
                color = nk_rgb(112, 176, 221);
            }
        } else {
            switch (tile) {
            case DG_TILE_WALL:
                color = nk_rgb(48, 54, 66);
                break;
            case DG_TILE_FLOOR:
                color = nk_rgb(188, 196, 173);
                break;
            case DG_TILE_DOOR:
                color = nk_rgb(224, 176, 85);
                break;
            case DG_TILE_VOID:
            default:
                color = nk_rgb(18, 22, 28);
                break;
            }
        }

        dst[0] = color.r;
        dst[1] = color.g;
        dst[2] = color.b;
        dst[3] = color.a;
    }

    free(room_mask);
    free(room_type_by_tile);

    dg_nuklear_free_preview_tile_colors(app);
    app->preview_tile_colors = tile_colors;
    app->preview_tile_color_width = app->map.width;
    app->preview_tile_color_height = app->map.height;
    app->preview_tile_colors_valid = 1;
    return true;
}

static bool dg_nuklear_ensure_preview_tile_colors(dg_nuklear_app_t *app)
{
    if (app == NULL || !app->has_map || app->map.tiles == NULL || app->map.width <= 0 || app->map.height <= 0) {
        return false;
    }

    if (app->preview_tile_colors_valid != 0 && app->preview_tile_colors != NULL &&
        app->preview_tile_color_width == app->map.width &&
        app->preview_tile_color_height == app->map.height) {
        return true;
    }

    return dg_nuklear_rebuild_preview_tile_colors(app);
}

static void dg_nuklear_rasterize_preview_image(
    dg_nuklear_app_t *app,
    float origin_x_tiles,
    float origin_y_tiles,
    float scale,
    int image_width,
    int image_height
)
{
    int px;
    int py;
    int tile_width;
    int tile_height;
    const unsigned char outside_rgba[4] = {18u, 22u, 28u, 255u};

    if (app == NULL || app->preview_image_pixels == NULL || app->preview_tile_colors == NULL || scale <= 0.0f) {
        return;
    }

    tile_width = app->preview_tile_color_width;
    tile_height = app->preview_tile_color_height;

    for (py = 0; py < image_height; ++py) {
        float map_yf = origin_y_tiles + (((float)py + 0.5f) / scale);
        int map_y = dg_nuklear_floor_to_int(map_yf);

        for (px = 0; px < image_width; ++px) {
            float map_xf = origin_x_tiles + (((float)px + 0.5f) / scale);
            int map_x = dg_nuklear_floor_to_int(map_xf);
            size_t dst_index = ((size_t)py * (size_t)image_width + (size_t)px) * 4u;

            if (map_x >= 0 && map_y >= 0 && map_x < tile_width && map_y < tile_height) {
                size_t src_index =
                    ((size_t)map_y * (size_t)tile_width + (size_t)map_x) * 4u;
                app->preview_image_pixels[dst_index + 0u] = app->preview_tile_colors[src_index + 0u];
                app->preview_image_pixels[dst_index + 1u] = app->preview_tile_colors[src_index + 1u];
                app->preview_image_pixels[dst_index + 2u] = app->preview_tile_colors[src_index + 2u];
                app->preview_image_pixels[dst_index + 3u] = app->preview_tile_colors[src_index + 3u];
            } else {
                app->preview_image_pixels[dst_index + 0u] = outside_rgba[0];
                app->preview_image_pixels[dst_index + 1u] = outside_rgba[1];
                app->preview_image_pixels[dst_index + 2u] = outside_rgba[2];
                app->preview_image_pixels[dst_index + 3u] = outside_rgba[3];
            }
        }
    }
}

static void dg_nuklear_draw_preview_grid_overlay(
    struct nk_command_buffer *canvas,
    struct nk_rect preview_content_bounds,
    float origin_x_tiles,
    float origin_y_tiles,
    float scale,
    int map_width,
    int map_height
)
{
    int x_first;
    int x_last;
    int y_first;
    int y_last;
    int x;
    int y;
    float map_left;
    float map_top;
    float map_right;
    float map_bottom;
    float draw_left;
    float draw_top;
    float draw_right;
    float draw_bottom;
    struct nk_color grid_color;

    if (canvas == NULL || scale <= 0.0f || map_width <= 0 || map_height <= 0) {
        return;
    }

    map_left = preview_content_bounds.x + (0.0f - origin_x_tiles) * scale;
    map_top = preview_content_bounds.y + (0.0f - origin_y_tiles) * scale;
    map_right = preview_content_bounds.x + ((float)map_width - origin_x_tiles) * scale;
    map_bottom = preview_content_bounds.y + ((float)map_height - origin_y_tiles) * scale;

    draw_left = dg_nuklear_max_float(preview_content_bounds.x, map_left);
    draw_top = dg_nuklear_max_float(preview_content_bounds.y, map_top);
    draw_right = dg_nuklear_min_float(preview_content_bounds.x + preview_content_bounds.w, map_right);
    draw_bottom = dg_nuklear_min_float(preview_content_bounds.y + preview_content_bounds.h, map_bottom);

    if (draw_right <= draw_left || draw_bottom <= draw_top) {
        return;
    }

    x_first = dg_nuklear_floor_to_int(origin_x_tiles);
    x_last = dg_nuklear_ceil_to_int(origin_x_tiles + (preview_content_bounds.w / scale));
    y_first = dg_nuklear_floor_to_int(origin_y_tiles);
    y_last = dg_nuklear_ceil_to_int(origin_y_tiles + (preview_content_bounds.h / scale));

    if (x_first < 0) {
        x_first = 0;
    }
    if (y_first < 0) {
        y_first = 0;
    }
    if (x_last > map_width) {
        x_last = map_width;
    }
    if (y_last > map_height) {
        y_last = map_height;
    }

    grid_color = nk_rgba(255, 255, 255, 52);
    for (x = x_first; x <= x_last; ++x) {
        float screen_x = preview_content_bounds.x + ((float)x - origin_x_tiles) * scale;
        nk_stroke_line(
            canvas,
            screen_x,
            draw_top,
            screen_x,
            draw_bottom,
            1.0f,
            grid_color
        );
    }
    for (y = y_first; y <= y_last; ++y) {
        float screen_y = preview_content_bounds.y + ((float)y - origin_y_tiles) * scale;
        nk_stroke_line(
            canvas,
            draw_left,
            screen_y,
            draw_right,
            screen_y,
            1.0f,
            grid_color
        );
    }
}

static void dg_nuklear_reset_preview_camera(dg_nuklear_app_t *app)
{
    if (app == NULL) {
        return;
    }

    app->preview_zoom = 1.0f;
    if (app->has_map && app->map.width > 0 && app->map.height > 0) {
        app->preview_center_x = (float)app->map.width * 0.5f;
        app->preview_center_y = (float)app->map.height * 0.5f;
    } else {
        app->preview_center_x = 0.0f;
        app->preview_center_y = 0.0f;
    }
}

static void dg_nuklear_destroy_map(dg_nuklear_app_t *app)
{
    if (app == NULL) {
        return;
    }

    dg_nuklear_free_preview_tile_colors(app);

    if (app->has_map) {
        dg_map_destroy(&app->map);
        app->map = (dg_map_t){0};
        app->has_map = false;
    }

    dg_nuklear_reset_preview_camera(app);
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
    } else if (algorithm == DG_ALGORITHM_WORM_CAVES) {
        app->worm_caves_config = defaults.params.worm_caves;
    } else if (algorithm == DG_ALGORITHM_CELLULAR_AUTOMATA) {
        app->cellular_automata_config = defaults.params.cellular_automata;
    } else if (algorithm == DG_ALGORITHM_SIMPLEX_NOISE) {
        app->simplex_noise_config = defaults.params.simplex_noise;
    } else if (algorithm == DG_ALGORITHM_VALUE_NOISE) {
        app->value_noise_config = defaults.params.value_noise;
    } else if (algorithm == DG_ALGORITHM_ROOM_GRAPH) {
        app->room_graph_config = defaults.params.room_graph;
    } else if (algorithm == DG_ALGORITHM_ROOMS_AND_MAZES) {
        app->rooms_and_mazes_config = defaults.params.rooms_and_mazes;
    } else {
        app->bsp_config = defaults.params.bsp;
    }
}

static void dg_nuklear_apply_room_type_slot_snapshot(
    dg_nuklear_room_type_ui_t *slot,
    const dg_snapshot_room_type_definition_t *definition,
    int index
)
{
    if (slot == NULL || definition == NULL) {
        return;
    }

    dg_nuklear_default_room_type_slot(slot, index);
    slot->type_id = (int)definition->type_id;
    slot->enabled = definition->enabled ? 1 : 0;
    slot->min_count = definition->min_count;
    slot->max_count = definition->max_count;
    slot->target_count = definition->target_count;
    slot->area_min = definition->constraints.area_min;
    slot->area_max = definition->constraints.area_max;
    slot->degree_min = definition->constraints.degree_min;
    slot->degree_max = definition->constraints.degree_max;
    slot->border_distance_min = definition->constraints.border_distance_min;
    slot->border_distance_max = definition->constraints.border_distance_max;
    slot->graph_depth_min = definition->constraints.graph_depth_min;
    slot->graph_depth_max = definition->constraints.graph_depth_max;
    slot->weight = definition->preferences.weight;
    slot->larger_room_bias = definition->preferences.larger_room_bias;
    slot->higher_degree_bias = definition->preferences.higher_degree_bias;
    slot->border_distance_bias = definition->preferences.border_distance_bias;
    (void)snprintf(slot->label, sizeof(slot->label), "Type %u", (unsigned int)definition->type_id);
    dg_nuklear_sanitize_room_type_slot(slot);
}

static bool dg_nuklear_apply_generation_request_snapshot(
    dg_nuklear_app_t *app,
    const dg_generation_request_snapshot_t *snapshot
)
{
    size_t i;
    size_t copy_count;

    if (app == NULL || snapshot == NULL || snapshot->present == 0) {
        return false;
    }

    app->algorithm_index = dg_nuklear_algorithm_index_from_id(snapshot->algorithm_id);
    app->width = snapshot->width;
    app->height = snapshot->height;
    (void)snprintf(app->seed_text, sizeof(app->seed_text), "%llu", (unsigned long long)snapshot->seed);
    dg_nuklear_reset_process_defaults(app);
    app->process_enabled = snapshot->process.enabled ? 1 : 0;
    copy_count = snapshot->process.method_count;
    if (copy_count > (size_t)DG_NUKLEAR_MAX_PROCESS_METHODS) {
        copy_count = (size_t)DG_NUKLEAR_MAX_PROCESS_METHODS;
    }
    for (i = 0; i < copy_count; ++i) {
        app->process_methods[i].type = (dg_process_method_type_t)snapshot->process.methods[i].type;
        switch (app->process_methods[i].type) {
        case DG_PROCESS_METHOD_SCALE:
            app->process_methods[i].params.scale.factor =
                snapshot->process.methods[i].params.scale.factor;
            break;
        case DG_PROCESS_METHOD_ROOM_SHAPE:
            app->process_methods[i].params.room_shape.mode =
                (dg_room_shape_mode_t)snapshot->process.methods[i].params.room_shape.mode;
            app->process_methods[i].params.room_shape.organicity =
                snapshot->process.methods[i].params.room_shape.organicity;
            break;
        case DG_PROCESS_METHOD_PATH_SMOOTH:
            app->process_methods[i].params.path_smooth.strength =
                snapshot->process.methods[i].params.path_smooth.strength;
            app->process_methods[i].params.path_smooth.inner_enabled =
                snapshot->process.methods[i].params.path_smooth.inner_enabled;
            app->process_methods[i].params.path_smooth.outer_enabled =
                snapshot->process.methods[i].params.path_smooth.outer_enabled;
            break;
        case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
            app->process_methods[i].params.corridor_roughen.strength =
                snapshot->process.methods[i].params.corridor_roughen.strength;
            app->process_methods[i].params.corridor_roughen.max_depth =
                snapshot->process.methods[i].params.corridor_roughen.max_depth;
            app->process_methods[i].params.corridor_roughen.mode =
                (dg_corridor_roughen_mode_t)snapshot->process.methods[i].params.corridor_roughen.mode;
            break;
        default:
            dg_default_process_method(&app->process_methods[i], DG_PROCESS_METHOD_SCALE);
            break;
        }
    }
    app->process_method_count = (int)copy_count;
    dg_nuklear_sanitize_process_settings(app);

    switch ((dg_algorithm_t)snapshot->algorithm_id) {
    case DG_ALGORITHM_DRUNKARDS_WALK:
        app->drunkards_walk_config.wiggle_percent = snapshot->params.drunkards_walk.wiggle_percent;
        break;
    case DG_ALGORITHM_WORM_CAVES:
        app->worm_caves_config.worm_count = snapshot->params.worm_caves.worm_count;
        app->worm_caves_config.wiggle_percent = snapshot->params.worm_caves.wiggle_percent;
        app->worm_caves_config.branch_chance_percent =
            snapshot->params.worm_caves.branch_chance_percent;
        app->worm_caves_config.target_floor_percent =
            snapshot->params.worm_caves.target_floor_percent;
        app->worm_caves_config.brush_radius = snapshot->params.worm_caves.brush_radius;
        app->worm_caves_config.max_steps_per_worm =
            snapshot->params.worm_caves.max_steps_per_worm;
        app->worm_caves_config.ensure_connected = snapshot->params.worm_caves.ensure_connected;
        break;
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        app->cellular_automata_config.initial_wall_percent =
            snapshot->params.cellular_automata.initial_wall_percent;
        app->cellular_automata_config.simulation_steps =
            snapshot->params.cellular_automata.simulation_steps;
        app->cellular_automata_config.wall_threshold =
            snapshot->params.cellular_automata.wall_threshold;
        break;
    case DG_ALGORITHM_VALUE_NOISE:
        app->value_noise_config.feature_size = snapshot->params.value_noise.feature_size;
        app->value_noise_config.octaves = snapshot->params.value_noise.octaves;
        app->value_noise_config.persistence_percent =
            snapshot->params.value_noise.persistence_percent;
        app->value_noise_config.floor_threshold_percent =
            snapshot->params.value_noise.floor_threshold_percent;
        break;
    case DG_ALGORITHM_SIMPLEX_NOISE:
        app->simplex_noise_config.feature_size = snapshot->params.simplex_noise.feature_size;
        app->simplex_noise_config.octaves = snapshot->params.simplex_noise.octaves;
        app->simplex_noise_config.persistence_percent =
            snapshot->params.simplex_noise.persistence_percent;
        app->simplex_noise_config.floor_threshold_percent =
            snapshot->params.simplex_noise.floor_threshold_percent;
        app->simplex_noise_config.ensure_connected =
            snapshot->params.simplex_noise.ensure_connected;
        break;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        app->rooms_and_mazes_config.min_rooms = snapshot->params.rooms_and_mazes.min_rooms;
        app->rooms_and_mazes_config.max_rooms = snapshot->params.rooms_and_mazes.max_rooms;
        app->rooms_and_mazes_config.room_min_size = snapshot->params.rooms_and_mazes.room_min_size;
        app->rooms_and_mazes_config.room_max_size = snapshot->params.rooms_and_mazes.room_max_size;
        app->rooms_and_mazes_config.maze_wiggle_percent =
            snapshot->params.rooms_and_mazes.maze_wiggle_percent;
        app->rooms_and_mazes_config.min_room_connections =
            snapshot->params.rooms_and_mazes.min_room_connections;
        app->rooms_and_mazes_config.max_room_connections =
            snapshot->params.rooms_and_mazes.max_room_connections;
        app->rooms_and_mazes_config.ensure_full_connectivity =
            snapshot->params.rooms_and_mazes.ensure_full_connectivity;
        app->rooms_and_mazes_config.dead_end_prune_steps =
            snapshot->params.rooms_and_mazes.dead_end_prune_steps;
        break;
    case DG_ALGORITHM_ROOM_GRAPH:
        app->room_graph_config.min_rooms = snapshot->params.room_graph.min_rooms;
        app->room_graph_config.max_rooms = snapshot->params.room_graph.max_rooms;
        app->room_graph_config.room_min_size = snapshot->params.room_graph.room_min_size;
        app->room_graph_config.room_max_size = snapshot->params.room_graph.room_max_size;
        app->room_graph_config.neighbor_candidates = snapshot->params.room_graph.neighbor_candidates;
        app->room_graph_config.extra_connection_chance_percent =
            snapshot->params.room_graph.extra_connection_chance_percent;
        break;
    case DG_ALGORITHM_BSP_TREE:
    default:
        app->bsp_config.min_rooms = snapshot->params.bsp.min_rooms;
        app->bsp_config.max_rooms = snapshot->params.bsp.max_rooms;
        app->bsp_config.room_min_size = snapshot->params.bsp.room_min_size;
        app->bsp_config.room_max_size = snapshot->params.bsp.room_max_size;
        break;
    }

    dg_nuklear_sync_generation_class_with_algorithm(app);

    dg_nuklear_reset_room_type_defaults(app);
    app->room_types_enabled = snapshot->room_types.definition_count > 0 ? 1 : 0;
    app->room_type_strict_mode = snapshot->room_types.policy.strict_mode ? 1 : 0;
    app->room_type_allow_untyped = snapshot->room_types.policy.allow_untyped_rooms ? 1 : 0;
    app->room_type_default_type_id = (int)snapshot->room_types.policy.default_type_id;

    copy_count = snapshot->room_types.definition_count;
    if (copy_count > (size_t)DG_NUKLEAR_MAX_ROOM_TYPES) {
        copy_count = (size_t)DG_NUKLEAR_MAX_ROOM_TYPES;
    }

    if (copy_count > 0) {
        app->room_type_count = (int)copy_count;
        for (i = 0; i < copy_count; ++i) {
            dg_nuklear_apply_room_type_slot_snapshot(
                &app->room_type_slots[i],
                &snapshot->room_types.definitions[i],
                (int)i
            );
        }
        for (; i < (size_t)DG_NUKLEAR_MAX_ROOM_TYPES; ++i) {
            dg_nuklear_default_room_type_slot(&app->room_type_slots[i], (int)i);
        }
    }

    dg_nuklear_sanitize_room_type_settings(app);
    return true;
}

static uint64_t dg_nuklear_compute_live_config_hash(const dg_nuklear_app_t *app)
{
    uint64_t hash;
    uint64_t seed;
    int seed_valid;
    int room_types_active;
    int i;

    if (app == NULL) {
        return UINT64_C(1469598103934665603);
    }

    hash = UINT64_C(1469598103934665603);
    hash = dg_nuklear_hash_i32(hash, app->algorithm_index);
    hash = dg_nuklear_hash_i32(hash, app->generation_class_index);
    hash = dg_nuklear_hash_i32(hash, app->width);
    hash = dg_nuklear_hash_i32(hash, app->height);

    seed = 0u;
    seed_valid = dg_nuklear_parse_u64(app->seed_text, &seed) ? 1 : 0;
    hash = dg_nuklear_hash_i32(hash, seed_valid);
    if (seed_valid != 0) {
        hash = dg_nuklear_hash_u64(hash, seed);
    } else {
        hash = dg_nuklear_hash_cstr(hash, app->seed_text);
    }

    hash = dg_nuklear_hash_i32(hash, app->bsp_config.min_rooms);
    hash = dg_nuklear_hash_i32(hash, app->bsp_config.max_rooms);
    hash = dg_nuklear_hash_i32(hash, app->bsp_config.room_min_size);
    hash = dg_nuklear_hash_i32(hash, app->bsp_config.room_max_size);

    hash = dg_nuklear_hash_i32(hash, app->drunkards_walk_config.wiggle_percent);
    hash = dg_nuklear_hash_i32(hash, app->worm_caves_config.worm_count);
    hash = dg_nuklear_hash_i32(hash, app->worm_caves_config.wiggle_percent);
    hash = dg_nuklear_hash_i32(hash, app->worm_caves_config.branch_chance_percent);
    hash = dg_nuklear_hash_i32(hash, app->worm_caves_config.target_floor_percent);
    hash = dg_nuklear_hash_i32(hash, app->worm_caves_config.brush_radius);
    hash = dg_nuklear_hash_i32(hash, app->worm_caves_config.max_steps_per_worm);
    hash = dg_nuklear_hash_i32(hash, app->worm_caves_config.ensure_connected);

    hash = dg_nuklear_hash_i32(hash, app->cellular_automata_config.initial_wall_percent);
    hash = dg_nuklear_hash_i32(hash, app->cellular_automata_config.simulation_steps);
    hash = dg_nuklear_hash_i32(hash, app->cellular_automata_config.wall_threshold);

    hash = dg_nuklear_hash_i32(hash, app->value_noise_config.feature_size);
    hash = dg_nuklear_hash_i32(hash, app->value_noise_config.octaves);
    hash = dg_nuklear_hash_i32(hash, app->value_noise_config.persistence_percent);
    hash = dg_nuklear_hash_i32(hash, app->value_noise_config.floor_threshold_percent);
    hash = dg_nuklear_hash_i32(hash, app->simplex_noise_config.feature_size);
    hash = dg_nuklear_hash_i32(hash, app->simplex_noise_config.octaves);
    hash = dg_nuklear_hash_i32(hash, app->simplex_noise_config.persistence_percent);
    hash = dg_nuklear_hash_i32(hash, app->simplex_noise_config.floor_threshold_percent);
    hash = dg_nuklear_hash_i32(hash, app->simplex_noise_config.ensure_connected);

    hash = dg_nuklear_hash_i32(hash, app->rooms_and_mazes_config.min_rooms);
    hash = dg_nuklear_hash_i32(hash, app->rooms_and_mazes_config.max_rooms);
    hash = dg_nuklear_hash_i32(hash, app->rooms_and_mazes_config.room_min_size);
    hash = dg_nuklear_hash_i32(hash, app->rooms_and_mazes_config.room_max_size);
    hash = dg_nuklear_hash_i32(hash, app->rooms_and_mazes_config.maze_wiggle_percent);
    hash = dg_nuklear_hash_i32(hash, app->rooms_and_mazes_config.min_room_connections);
    hash = dg_nuklear_hash_i32(hash, app->rooms_and_mazes_config.max_room_connections);
    hash = dg_nuklear_hash_i32(hash, app->rooms_and_mazes_config.ensure_full_connectivity);
    hash = dg_nuklear_hash_i32(hash, app->rooms_and_mazes_config.dead_end_prune_steps);
    hash = dg_nuklear_hash_i32(hash, app->room_graph_config.min_rooms);
    hash = dg_nuklear_hash_i32(hash, app->room_graph_config.max_rooms);
    hash = dg_nuklear_hash_i32(hash, app->room_graph_config.room_min_size);
    hash = dg_nuklear_hash_i32(hash, app->room_graph_config.room_max_size);
    hash = dg_nuklear_hash_i32(hash, app->room_graph_config.neighbor_candidates);
    hash = dg_nuklear_hash_i32(
        hash,
        app->room_graph_config.extra_connection_chance_percent
    );

    hash = dg_nuklear_hash_i32(hash, app->process_enabled);
    if (app->process_enabled != 0) {
        hash = dg_nuklear_hash_i32(hash, app->process_method_count);
        for (i = 0; i < app->process_method_count; ++i) {
            const dg_process_method_t *method = &app->process_methods[i];

            hash = dg_nuklear_hash_i32(hash, (int)method->type);
            switch (method->type) {
            case DG_PROCESS_METHOD_SCALE:
                hash = dg_nuklear_hash_i32(hash, method->params.scale.factor);
                break;
            case DG_PROCESS_METHOD_ROOM_SHAPE:
                hash = dg_nuklear_hash_i32(hash, (int)method->params.room_shape.mode);
                hash = dg_nuklear_hash_i32(hash, method->params.room_shape.organicity);
                break;
            case DG_PROCESS_METHOD_PATH_SMOOTH:
                hash = dg_nuklear_hash_i32(hash, method->params.path_smooth.strength);
                hash = dg_nuklear_hash_i32(hash, method->params.path_smooth.inner_enabled);
                hash = dg_nuklear_hash_i32(hash, method->params.path_smooth.outer_enabled);
                break;
            case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
                hash = dg_nuklear_hash_i32(hash, method->params.corridor_roughen.strength);
                hash = dg_nuklear_hash_i32(hash, method->params.corridor_roughen.max_depth);
                hash = dg_nuklear_hash_i32(hash, (int)method->params.corridor_roughen.mode);
                break;
            default:
                break;
            }
        }
    }

    room_types_active = app->room_types_enabled &&
                        dg_nuklear_algorithm_supports_room_types(
                            dg_nuklear_algorithm_from_index(app->algorithm_index)
                        );
    hash = dg_nuklear_hash_i32(hash, room_types_active);
    if (room_types_active != 0) {
        hash = dg_nuklear_hash_i32(hash, app->room_type_count);
        hash = dg_nuklear_hash_i32(hash, app->room_type_strict_mode);
        hash = dg_nuklear_hash_i32(hash, app->room_type_allow_untyped);
        hash = dg_nuklear_hash_i32(hash, app->room_type_default_type_id);
        for (i = 0; i < app->room_type_count; ++i) {
            const dg_nuklear_room_type_ui_t *slot = &app->room_type_slots[i];

            hash = dg_nuklear_hash_i32(hash, slot->type_id);
            hash = dg_nuklear_hash_i32(hash, slot->enabled);
            hash = dg_nuklear_hash_i32(hash, slot->min_count);
            hash = dg_nuklear_hash_i32(hash, slot->max_count);
            hash = dg_nuklear_hash_i32(hash, slot->target_count);
            hash = dg_nuklear_hash_i32(hash, slot->area_min);
            hash = dg_nuklear_hash_i32(hash, slot->area_max);
            hash = dg_nuklear_hash_i32(hash, slot->degree_min);
            hash = dg_nuklear_hash_i32(hash, slot->degree_max);
            hash = dg_nuklear_hash_i32(hash, slot->border_distance_min);
            hash = dg_nuklear_hash_i32(hash, slot->border_distance_max);
            hash = dg_nuklear_hash_i32(hash, slot->graph_depth_min);
            hash = dg_nuklear_hash_i32(hash, slot->graph_depth_max);
            hash = dg_nuklear_hash_i32(hash, slot->weight);
            hash = dg_nuklear_hash_i32(hash, slot->larger_room_bias);
            hash = dg_nuklear_hash_i32(hash, slot->higher_degree_bias);
            hash = dg_nuklear_hash_i32(hash, slot->border_distance_bias);
        }
    }

    return hash;
}

static void dg_nuklear_mark_live_config_clean(dg_nuklear_app_t *app)
{
    uint64_t seed;

    if (app == NULL) {
        return;
    }

    if (app->width < 8 || app->height < 8 || !dg_nuklear_parse_u64(app->seed_text, &seed)) {
        app->last_live_config_hash_valid = 0;
        return;
    }

    app->last_live_config_hash = dg_nuklear_compute_live_config_hash(app);
    app->last_live_config_hash_valid = 1;
}

static void dg_nuklear_generate_map(dg_nuklear_app_t *app)
{
    dg_generate_request_t request;
    dg_room_type_definition_t room_type_definitions[DG_NUKLEAR_MAX_ROOM_TYPES];
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
    dg_nuklear_sanitize_room_type_settings(app);
    dg_nuklear_sanitize_process_settings(app);

    if (algorithm == DG_ALGORITHM_DRUNKARDS_WALK) {
        request.params.drunkards_walk = app->drunkards_walk_config;
    } else if (algorithm == DG_ALGORITHM_WORM_CAVES) {
        request.params.worm_caves = app->worm_caves_config;
    } else if (algorithm == DG_ALGORITHM_CELLULAR_AUTOMATA) {
        request.params.cellular_automata = app->cellular_automata_config;
    } else if (algorithm == DG_ALGORITHM_SIMPLEX_NOISE) {
        request.params.simplex_noise = app->simplex_noise_config;
    } else if (algorithm == DG_ALGORITHM_VALUE_NOISE) {
        request.params.value_noise = app->value_noise_config;
    } else if (algorithm == DG_ALGORITHM_ROOM_GRAPH) {
        request.params.room_graph = app->room_graph_config;
    } else if (algorithm == DG_ALGORITHM_ROOMS_AND_MAZES) {
        request.params.rooms_and_mazes = app->rooms_and_mazes_config;
    } else {
        request.params.bsp = app->bsp_config;
    }
    request.process.enabled = app->process_enabled ? 1 : 0;
    request.process.methods = app->process_methods;
    request.process.method_count = (size_t)app->process_method_count;

    if (app->room_types_enabled && dg_nuklear_algorithm_supports_room_types(algorithm)) {
        int i;

        for (i = 0; i < app->room_type_count; ++i) {
            const dg_nuklear_room_type_ui_t *slot = &app->room_type_slots[i];
            dg_room_type_definition_t *definition = &room_type_definitions[i];

            dg_default_room_type_definition(definition, (uint32_t)slot->type_id);
            definition->enabled = slot->enabled ? 1 : 0;
            definition->min_count = slot->min_count;
            definition->max_count = slot->max_count;
            definition->target_count = slot->target_count;
            definition->constraints.area_min = slot->area_min;
            definition->constraints.area_max = slot->area_max;
            definition->constraints.degree_min = slot->degree_min;
            definition->constraints.degree_max = slot->degree_max;
            definition->constraints.border_distance_min = slot->border_distance_min;
            definition->constraints.border_distance_max = slot->border_distance_max;
            definition->constraints.graph_depth_min = slot->graph_depth_min;
            definition->constraints.graph_depth_max = slot->graph_depth_max;
            definition->preferences.weight = slot->weight;
            definition->preferences.larger_room_bias = slot->larger_room_bias;
            definition->preferences.higher_degree_bias = slot->higher_degree_bias;
            definition->preferences.border_distance_bias = slot->border_distance_bias;
        }

        request.room_types.definitions = room_type_definitions;
        request.room_types.definition_count = (size_t)app->room_type_count;
        request.room_types.policy.strict_mode = app->room_type_strict_mode ? 1 : 0;
        request.room_types.policy.allow_untyped_rooms = app->room_type_allow_untyped ? 1 : 0;
        request.room_types.policy.default_type_id = (uint32_t)app->room_type_default_type_id;
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
    dg_nuklear_reset_preview_camera(app);

    if (dg_nuklear_algorithm_supports_room_types(algorithm)) {
        size_t assigned_count;
        size_t i;

        assigned_count = 0;
        for (i = 0; i < app->map.metadata.room_count; ++i) {
            if (app->map.metadata.rooms[i].type_id != DG_ROOM_TYPE_UNASSIGNED) {
                assigned_count += 1;
            }
        }

        dg_nuklear_set_status(
            app,
            "Generated %dx%d %s map (typed rooms: %llu/%llu).",
            app->map.width,
            app->map.height,
            dg_nuklear_algorithm_name(algorithm),
            (unsigned long long)assigned_count,
            (unsigned long long)app->map.metadata.room_count
        );
    } else {
        dg_nuklear_set_status(
            app,
            "Generated %dx%d %s map.",
            app->map.width,
            app->map.height,
            dg_nuklear_algorithm_name(algorithm)
        );
    }

    dg_nuklear_mark_live_config_clean(app);
}

static void dg_nuklear_randomize_seed(dg_nuklear_app_t *app)
{
    uint64_t seed;

    if (app == NULL) {
        return;
    }

    seed = dg_nuklear_make_random_seed(app);
    (void)snprintf(app->seed_text, sizeof(app->seed_text), "%llu", (unsigned long long)seed);
    dg_nuklear_set_status(app, "Randomized seed: %llu", (unsigned long long)seed);
}

static void dg_nuklear_maybe_auto_generate(dg_nuklear_app_t *app)
{
    uint64_t seed;
    uint64_t hash;

    if (app == NULL) {
        return;
    }

    dg_nuklear_sanitize_process_settings(app);
    dg_nuklear_sanitize_room_type_settings(app);

    if (app->width < 8 || app->height < 8 || !dg_nuklear_parse_u64(app->seed_text, &seed)) {
        app->last_live_config_hash_valid = 0;
        return;
    }

    hash = dg_nuklear_compute_live_config_hash(app);
    if (app->last_live_config_hash_valid != 0 && hash == app->last_live_config_hash) {
        return;
    }

    /*
     * Mark this config as attempted before generation so invalid configs
     * don't spam generate failures every frame.
     */
    app->last_live_config_hash = hash;
    app->last_live_config_hash_valid = 1;
    dg_nuklear_generate_map(app);
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

static void dg_nuklear_build_export_paths(
    const char *source_path,
    char *out_png_path,
    size_t png_path_capacity,
    char *out_json_path,
    size_t json_path_capacity
)
{
    const char *fallback = "dungeon";
    const char *source;
    const char *slash_forward;
    const char *slash_back;
    const char *separator;
    const char *dot;
    size_t stem_length;
    int stem_precision;

    if (out_png_path == NULL || png_path_capacity == 0u ||
        out_json_path == NULL || json_path_capacity == 0u) {
        return;
    }

    source = (source_path != NULL && source_path[0] != '\0') ? source_path : fallback;
    slash_forward = strrchr(source, '/');
    slash_back = strrchr(source, '\\');
    separator = slash_forward;
    if (slash_back != NULL && (separator == NULL || slash_back > separator)) {
        separator = slash_back;
    }

    dot = strrchr(source, '.');
    stem_length = strlen(source);
    if (dot != NULL && (separator == NULL || dot > separator) && dot != source) {
        stem_length = (size_t)(dot - source);
    }

    if (stem_length > (size_t)INT_MAX) {
        stem_precision = INT_MAX;
    } else {
        stem_precision = (int)stem_length;
    }

    (void)snprintf(out_png_path, png_path_capacity, "%.*s.png", stem_precision, source);
    (void)snprintf(out_json_path, json_path_capacity, "%.*s.json", stem_precision, source);
}

static void dg_nuklear_export_map_png_json(dg_nuklear_app_t *app)
{
    dg_status_t status;
    char png_path[512];
    char json_path[512];
    uint64_t desired_hash;

    if (app == NULL) {
        return;
    }

    desired_hash = dg_nuklear_compute_live_config_hash(app);
    if (!app->last_live_config_hash_valid || app->last_live_config_hash != desired_hash) {
        dg_nuklear_generate_map(app);
        if (!app->last_live_config_hash_valid || app->last_live_config_hash != desired_hash) {
            return;
        }
    }

    if (!app->has_map) {
        dg_nuklear_set_status(app, "No map to export.");
        return;
    }

    dg_nuklear_build_export_paths(
        app->file_path,
        png_path,
        sizeof(png_path),
        json_path,
        sizeof(json_path)
    );
    status = dg_map_export_png_json(&app->map, png_path, json_path);
    if (status != DG_STATUS_OK) {
        dg_nuklear_set_status(app, "Export failed: %s", dg_status_string(status));
        return;
    }

    dg_nuklear_set_status(app, "Exported %s and %s", png_path, json_path);
}

static void dg_nuklear_load_map(dg_nuklear_app_t *app)
{
    dg_map_t loaded;
    dg_status_t status;
    bool restored_from_snapshot;

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
    dg_nuklear_reset_preview_camera(app);

    app->algorithm_index = dg_nuklear_algorithm_index_from_id(app->map.metadata.algorithm_id);
    dg_nuklear_sync_generation_class_with_algorithm(app);
    app->width = app->map.width;
    app->height = app->map.height;
    (void)snprintf(app->seed_text, sizeof(app->seed_text), "%llu", (unsigned long long)app->map.metadata.seed);

    restored_from_snapshot = dg_nuklear_apply_generation_request_snapshot(
        app,
        &app->map.metadata.generation_request
    );

    if (restored_from_snapshot) {
        if (app->map.metadata.generation_request.room_types.definition_count >
            (size_t)DG_NUKLEAR_MAX_ROOM_TYPES) {
            dg_nuklear_set_status(
                app,
                "Loaded map from %s (restored settings; room types truncated to %d).",
                app->file_path,
                DG_NUKLEAR_MAX_ROOM_TYPES
            );
        } else {
            dg_nuklear_set_status(app, "Loaded map from %s (restored generation settings).", app->file_path);
        }
        dg_nuklear_mark_live_config_clean(app);
    } else {
        dg_nuklear_reset_algorithm_defaults(
            app,
            dg_nuklear_algorithm_from_index(app->algorithm_index)
        );
        dg_nuklear_sync_generation_class_with_algorithm(app);
        dg_nuklear_reset_process_defaults(app);
        dg_nuklear_reset_room_type_defaults(app);
        dg_nuklear_set_status(app, "Loaded map from %s", app->file_path);
        dg_nuklear_mark_live_config_clean(app);
    }
}

static bool dg_nuklear_draw_preview_overlay_button(
    struct nk_context *ctx,
    struct nk_command_buffer *canvas,
    struct nk_rect rect,
    const char *label
)
{
    int hovered;
    int pressed;
    struct nk_color bg;
    struct nk_color border;
    struct nk_color fg;
    int label_len;

    if (ctx == NULL || canvas == NULL || label == NULL) {
        return false;
    }

    hovered = nk_input_is_mouse_hovering_rect(&ctx->input, rect);
    pressed = hovered && nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT);

    if (pressed) {
        bg = nk_rgba(79, 95, 121, 230);
    } else if (hovered) {
        bg = nk_rgba(66, 80, 103, 216);
    } else {
        bg = nk_rgba(47, 57, 73, 208);
    }
    border = nk_rgba(101, 118, 143, 235);
    fg = nk_rgba(240, 244, 250, 255);

    nk_fill_rect(canvas, rect, 4.0f, bg);
    nk_stroke_rect(canvas, rect, 4.0f, 1.0f, border);

    label_len = (int)strlen(label);
    if (label_len > 0 && ctx->style.font != NULL && ctx->style.font->width != NULL) {
        float text_width;
        float text_x;
        float text_y;
        struct nk_rect text_bounds;

        text_width = ctx->style.font->width(
            ctx->style.font->userdata,
            ctx->style.font->height,
            label,
            label_len
        );
        text_x = rect.x + (rect.w - text_width) * 0.5f;
        text_y = rect.y + (rect.h - ctx->style.font->height) * 0.5f;

        if (text_x < rect.x + 2.0f) {
            text_x = rect.x + 2.0f;
        }
        if (text_y < rect.y + 1.0f) {
            text_y = rect.y + 1.0f;
        }

        text_bounds = nk_rect(
            text_x,
            text_y,
            dg_nuklear_max_float(text_width + 2.0f, 1.0f),
            dg_nuklear_max_float(ctx->style.font->height + 2.0f, 1.0f)
        );
        nk_draw_text(canvas, text_bounds, label, label_len, ctx->style.font, bg, fg);
    }

    return hovered && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT);
}

static void dg_nuklear_draw_map(
    struct nk_context *ctx,
    dg_nuklear_app_t *app,
    float suggested_height,
    const dg_nuklear_preview_renderer_t *preview_renderer
)
{
    struct nk_rect content_region;
    struct nk_rect preview_bounds;
    struct nk_rect preview_content_bounds;
    struct nk_rect overlay_panel;
    struct nk_rect overlay_zoom_text;
    struct nk_rect overlay_zoom_in;
    struct nk_rect overlay_zoom_out;
    struct nk_rect overlay_fit;
    struct nk_rect overlay_grid;
    enum nk_widget_layout_states widget_state;
    struct nk_command_buffer *canvas;
    struct nk_rect old_clip;
    struct nk_rect draw_clip;
    bool overlay_visible;
    bool overlay_hovered;

    if (ctx == NULL || app == NULL) {
        return;
    }

    content_region = nk_window_get_content_region(ctx);
    if (suggested_height <= 0.0f) {
        suggested_height = content_region.h;
    }
    if (content_region.h > 0.0f) {
        float max_layout_h = dg_nuklear_max_float(content_region.h - 1.0f, 1.0f);
        suggested_height = dg_nuklear_clamp_float(suggested_height, 1.0f, max_layout_h);
    } else if (suggested_height < 1.0f) {
        suggested_height = 1.0f;
    }

    if (!app->has_map) {
        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "No map loaded. Adjust settings, or load a file.", NK_TEXT_LEFT);
    }

    nk_layout_row_dynamic(ctx, suggested_height, 1);
    widget_state = nk_widget(&preview_bounds, ctx);
    if (widget_state == NK_WIDGET_INVALID) {
        return;
    }

    canvas = nk_window_get_canvas(ctx);
    old_clip = canvas->clip;
    preview_content_bounds = preview_bounds;
    if (preview_content_bounds.w > 2.0f && preview_content_bounds.h > 2.0f) {
        preview_content_bounds.x += 1.0f;
        preview_content_bounds.y += 1.0f;
        preview_content_bounds.w -= 2.0f;
        preview_content_bounds.h -= 2.0f;
    }

    nk_fill_rect(canvas, preview_bounds, 0.0f, nk_rgb(20, 24, 31));
    draw_clip = dg_nuklear_rect_intersection(old_clip, preview_content_bounds);
    nk_push_scissor(canvas, draw_clip);

    overlay_panel = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    overlay_zoom_text = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    overlay_zoom_in = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    overlay_zoom_out = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    overlay_fit = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    overlay_grid = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    overlay_visible = false;
    overlay_hovered = false;

    if (app->has_map) {
        const float panel_pad = 6.0f;
        const float button_h = 26.0f;
        const float zoom_text_w = 56.0f;
        const float zoom_w = 28.0f;
        const float fit_w = 42.0f;
        const float grid_w = 92.0f;
        const float gap = 4.0f;
        const float edge_margin = 8.0f;
        float panel_w;
        float panel_h;
        float panel_x;
        float panel_y;
        float cursor_x;
        float cursor_y;

        panel_w =
            panel_pad * 2.0f + zoom_text_w + zoom_w * 2.0f + fit_w + grid_w + gap * 4.0f;
        panel_h = panel_pad * 2.0f + button_h;
        panel_x = preview_content_bounds.x + preview_content_bounds.w - panel_w - edge_margin;
        panel_y = preview_content_bounds.y + preview_content_bounds.h - panel_h - edge_margin;

        if (panel_x < preview_content_bounds.x + 2.0f) {
            panel_x = preview_content_bounds.x + 2.0f;
        }
        if (panel_y < preview_content_bounds.y + 2.0f) {
            panel_y = preview_content_bounds.y + 2.0f;
        }

        overlay_panel = nk_rect(panel_x, panel_y, panel_w, panel_h);
        overlay_zoom_text = nk_rect(panel_x + panel_pad, panel_y + panel_pad, zoom_text_w, button_h);
        overlay_zoom_in = nk_rect(
            overlay_zoom_text.x + zoom_text_w + gap,
            panel_y + panel_pad,
            zoom_w,
            button_h
        );
        overlay_zoom_out = nk_rect(
            overlay_zoom_in.x + zoom_w + gap,
            panel_y + panel_pad,
            zoom_w,
            button_h
        );
        overlay_fit = nk_rect(
            overlay_zoom_out.x + zoom_w + gap,
            panel_y + panel_pad,
            fit_w,
            button_h
        );
        overlay_grid = nk_rect(
            overlay_fit.x + fit_w + gap,
            panel_y + panel_pad,
            grid_w,
            button_h
        );

        cursor_x = ctx->input.mouse.pos.x;
        cursor_y = ctx->input.mouse.pos.y;
        overlay_visible =
            nk_input_is_mouse_hovering_rect(&ctx->input, preview_content_bounds) ||
            (cursor_x >= overlay_panel.x &&
             cursor_y >= overlay_panel.y &&
             cursor_x <= overlay_panel.x + overlay_panel.w &&
             cursor_y <= overlay_panel.y + overlay_panel.h);
        overlay_hovered = overlay_visible &&
            nk_input_is_mouse_hovering_rect(&ctx->input, overlay_panel);
    }

    if (app->has_map && app->map.tiles != NULL && app->map.width > 0 && app->map.height > 0) {
        int x_start;
        int x_end;
        int y_start;
        int y_end;
        int image_width;
        int image_height;
        int sample_step;
        int x;
        int y;
        size_t max_preview_quads;
        size_t sampled_quads;
        float base_scale;
        float scale;
        float view_w_tiles;
        float view_h_tiles;
        float center_x;
        float center_y;
        float origin_x_tiles;
        float origin_y_tiles;
        bool drew_image;
        bool hovered;
        float scroll_delta;

        base_scale = dg_nuklear_min_float(
            preview_content_bounds.w / (float)app->map.width,
            preview_content_bounds.h / (float)app->map.height
        );
        if (base_scale > 0.0f) {
            app->preview_zoom = dg_nuklear_clamp_float(app->preview_zoom, 0.10f, 24.0f);
            scale = base_scale * app->preview_zoom;
            scale = dg_nuklear_max_float(scale, 0.01f);
            view_w_tiles = preview_content_bounds.w / scale;
            view_h_tiles = preview_content_bounds.h / scale;

            center_x = app->preview_center_x;
            center_y = app->preview_center_y;
            if (center_x <= 0.0f || center_y <= 0.0f) {
                center_x = (float)app->map.width * 0.5f;
                center_y = (float)app->map.height * 0.5f;
            }

            hovered = nk_input_is_mouse_hovering_rect(&ctx->input, preview_content_bounds);
            scroll_delta = ctx->input.mouse.scroll_delta.y;
            if (hovered && !overlay_hovered && scroll_delta != 0.0f) {
                float mouse_x = ctx->input.mouse.pos.x;
                float mouse_y = ctx->input.mouse.pos.y;
                float old_scale = scale;
                float old_view_w = view_w_tiles;
                float old_view_h = view_h_tiles;
                float map_x_at_cursor;
                float map_y_at_cursor;
                float new_zoom;
                float new_scale;
                float new_view_w;
                float new_view_h;
                float origin_x_before = center_x - old_view_w * 0.5f;
                float origin_y_before = center_y - old_view_h * 0.5f;

                map_x_at_cursor = origin_x_before + (mouse_x - preview_content_bounds.x) / old_scale;
                map_y_at_cursor = origin_y_before + (mouse_y - preview_content_bounds.y) / old_scale;

                new_zoom = app->preview_zoom * (1.0f + scroll_delta * 0.12f);
                new_zoom = dg_nuklear_clamp_float(new_zoom, 0.10f, 24.0f);
                new_scale = dg_nuklear_max_float(base_scale * new_zoom, 0.01f);
                new_view_w = preview_content_bounds.w / new_scale;
                new_view_h = preview_content_bounds.h / new_scale;

                center_x = map_x_at_cursor -
                           (mouse_x - preview_content_bounds.x) / new_scale +
                           new_view_w * 0.5f;
                center_y = map_y_at_cursor -
                           (mouse_y - preview_content_bounds.y) / new_scale +
                           new_view_h * 0.5f;

                app->preview_zoom = new_zoom;
                scale = new_scale;
                view_w_tiles = new_view_w;
                view_h_tiles = new_view_h;
            }

            if (hovered && !overlay_hovered &&
                nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
                center_x -= ctx->input.mouse.delta.x / scale;
                center_y -= ctx->input.mouse.delta.y / scale;
            }

            if (view_w_tiles >= (float)app->map.width) {
                center_x = (float)app->map.width * 0.5f;
            } else {
                center_x = dg_nuklear_clamp_float(
                    center_x,
                    view_w_tiles * 0.5f,
                    (float)app->map.width - view_w_tiles * 0.5f
                );
            }

            if (view_h_tiles >= (float)app->map.height) {
                center_y = (float)app->map.height * 0.5f;
            } else {
                center_y = dg_nuklear_clamp_float(
                    center_y,
                    view_h_tiles * 0.5f,
                    (float)app->map.height - view_h_tiles * 0.5f
                );
            }

            app->preview_center_x = center_x;
            app->preview_center_y = center_y;

            origin_x_tiles = center_x - view_w_tiles * 0.5f;
            origin_y_tiles = center_y - view_h_tiles * 0.5f;

            x_start = dg_nuklear_floor_to_int(origin_x_tiles);
            y_start = dg_nuklear_floor_to_int(origin_y_tiles);
            x_end = dg_nuklear_ceil_to_int(origin_x_tiles + view_w_tiles);
            y_end = dg_nuklear_ceil_to_int(origin_y_tiles + view_h_tiles);

            x_start = dg_nuklear_clamp_int(x_start, 0, app->map.width);
            y_start = dg_nuklear_clamp_int(y_start, 0, app->map.height);
            x_end = dg_nuklear_clamp_int(x_end, 0, app->map.width);
            y_end = dg_nuklear_clamp_int(y_end, 0, app->map.height);

            drew_image = false;
            image_width = dg_nuklear_clamp_int((int)preview_content_bounds.w, 1, 4096);
            image_height = dg_nuklear_clamp_int((int)preview_content_bounds.h, 1, 4096);
            if (
                preview_renderer != NULL &&
                preview_renderer->upload_rgba8 != NULL &&
                dg_nuklear_ensure_preview_tile_colors(app) &&
                dg_nuklear_ensure_preview_image_buffer(app, image_width, image_height)
            ) {
                struct nk_image preview_image;
                dg_nuklear_rasterize_preview_image(
                    app,
                    origin_x_tiles,
                    origin_y_tiles,
                    scale,
                    image_width,
                    image_height
                );
                if (preview_renderer->upload_rgba8(
                        preview_renderer->user_data,
                        image_width,
                        image_height,
                        app->preview_image_pixels,
                        &preview_image
                    )) {
                    nk_draw_image(
                        canvas,
                        preview_content_bounds,
                        &preview_image,
                        nk_rgba(255, 255, 255, 255)
                    );
                    drew_image = true;
                }
            }

            if (!drew_image && x_end > x_start && y_end > y_start) {
                sample_step = 1;
                max_preview_quads = 4000u;
                sampled_quads = (size_t)(x_end - x_start) * (size_t)(y_end - y_start);
                while (sampled_quads > max_preview_quads) {
                    size_t sampled_w;
                    size_t sampled_h;

                    sample_step += 1;
                    sampled_w =
                        ((size_t)(x_end - x_start) + (size_t)sample_step - 1u) / (size_t)sample_step;
                    sampled_h =
                        ((size_t)(y_end - y_start) + (size_t)sample_step - 1u) / (size_t)sample_step;
                    sampled_quads = sampled_w * sampled_h;
                }

                for (y = y_start; y < y_end; y += sample_step) {
                    int block_h = sample_step;
                    if (y + block_h > y_end) {
                        block_h = y_end - y;
                    }

                    for (x = x_start; x < x_end; x += sample_step) {
                        int block_w = sample_step;
                        dg_tile_t tile;
                        struct nk_color color;
                        struct nk_rect r;

                        if (x + block_w > x_end) {
                            block_w = x_end - x;
                        }

                        tile = dg_map_get_tile(&app->map, x, y);
                        color = dg_nuklear_tile_color(&app->map, x, y, tile);
                        r = nk_rect(
                            preview_content_bounds.x + ((float)x - origin_x_tiles) * scale,
                            preview_content_bounds.y + ((float)y - origin_y_tiles) * scale,
                            scale * (float)block_w,
                            scale * (float)block_h
                        );
                        nk_fill_rect(canvas, r, 0.0f, color);
                    }
                }
            }

            if (app->preview_show_grid) {
                dg_nuklear_draw_preview_grid_overlay(
                    canvas,
                    preview_content_bounds,
                    origin_x_tiles,
                    origin_y_tiles,
                    scale,
                    app->map.width,
                    app->map.height
                );
            }

            if (overlay_visible) {
                const char *grid_label;
                struct nk_color panel_color;
                struct nk_color panel_border;
                char zoom_label[24];
                struct nk_color zoom_bg;
                struct nk_color zoom_border;
                struct nk_color zoom_fg;

                panel_color = nk_rgba(14, 18, 24, 208);
                panel_border = nk_rgba(90, 104, 125, 235);
                nk_fill_rect(canvas, overlay_panel, 6.0f, panel_color);
                nk_stroke_rect(canvas, overlay_panel, 6.0f, 1.0f, panel_border);

                zoom_bg = nk_rgba(30, 40, 53, 225);
                zoom_border = nk_rgba(97, 118, 142, 235);
                zoom_fg = nk_rgba(236, 242, 250, 255);
                nk_fill_rect(canvas, overlay_zoom_text, 4.0f, zoom_bg);
                nk_stroke_rect(canvas, overlay_zoom_text, 4.0f, 1.0f, zoom_border);
                (void)snprintf(
                    zoom_label,
                    sizeof(zoom_label),
                    "%.0f%%",
                    (double)(app->preview_zoom * 100.0f)
                );
                if (ctx->style.font != NULL && ctx->style.font->width != NULL) {
                    int zoom_len;
                    float text_w;
                    float text_x;
                    float text_y;
                    struct nk_rect text_bounds;

                    zoom_len = (int)strlen(zoom_label);
                    text_w = ctx->style.font->width(
                        ctx->style.font->userdata,
                        ctx->style.font->height,
                        zoom_label,
                        zoom_len
                    );
                    text_x = overlay_zoom_text.x + (overlay_zoom_text.w - text_w) * 0.5f;
                    text_y =
                        overlay_zoom_text.y +
                        (overlay_zoom_text.h - ctx->style.font->height) * 0.5f;
                    text_bounds = nk_rect(
                        text_x,
                        text_y,
                        dg_nuklear_max_float(text_w + 2.0f, 1.0f),
                        dg_nuklear_max_float(ctx->style.font->height + 2.0f, 1.0f)
                    );
                    nk_draw_text(
                        canvas,
                        text_bounds,
                        zoom_label,
                        zoom_len,
                        ctx->style.font,
                        zoom_bg,
                        zoom_fg
                    );
                }

                if (dg_nuklear_draw_preview_overlay_button(ctx, canvas, overlay_zoom_in, "+")) {
                    app->preview_zoom = dg_nuklear_clamp_float(app->preview_zoom * 1.15f, 0.10f, 24.0f);
                }
                if (dg_nuklear_draw_preview_overlay_button(ctx, canvas, overlay_zoom_out, "-")) {
                    app->preview_zoom = dg_nuklear_clamp_float(app->preview_zoom / 1.15f, 0.10f, 24.0f);
                }
                if (dg_nuklear_draw_preview_overlay_button(ctx, canvas, overlay_fit, "Fit")) {
                    dg_nuklear_reset_preview_camera(app);
                }

                grid_label = app->preview_show_grid ? "Hide Grid" : "Show Grid";
                if (dg_nuklear_draw_preview_overlay_button(ctx, canvas, overlay_grid, grid_label)) {
                    app->preview_show_grid = app->preview_show_grid ? 0 : 1;
                }
            }
        }
    }

    nk_push_scissor(canvas, old_clip);
    nk_stroke_rect(canvas, preview_bounds, 0.0f, 1.0f, nk_rgb(85, 96, 112));
}

static void dg_nuklear_draw_metadata(struct nk_context *ctx, const dg_nuklear_app_t *app)
{
    char line[128];
    double average_room_degree;
    size_t assigned_room_count;
    size_t unassigned_room_count;
    uint32_t shown_type_ids[6];
    size_t shown_type_counts[6];
    size_t shown_type_count;
    size_t i;

    if (ctx == NULL || app == NULL) {
        return;
    }

    dg_nuklear_draw_subsection_heading(
        ctx,
        "Overview",
        ""
    );

    if (!app->has_map) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "No map loaded.", NK_TEXT_LEFT);
        return;
    }

    nk_layout_row_dynamic(ctx, 18.0f, 1);

    average_room_degree = 0.0;
    assigned_room_count = 0;
    unassigned_room_count = 0;
    shown_type_count = 0;
    if (app->map.metadata.room_count > 0) {
        average_room_degree = (double)app->map.metadata.room_neighbor_count /
                              (double)app->map.metadata.room_count;
        for (i = 0; i < app->map.metadata.room_count; ++i) {
            uint32_t type_id = app->map.metadata.rooms[i].type_id;

            if (type_id == DG_ROOM_TYPE_UNASSIGNED) {
                unassigned_room_count += 1;
                continue;
            }

            assigned_room_count += 1;
            if (shown_type_count < (sizeof(shown_type_ids) / sizeof(shown_type_ids[0]))) {
                size_t j;
                bool found;

                found = false;
                for (j = 0; j < shown_type_count; ++j) {
                    if (shown_type_ids[j] == type_id) {
                        shown_type_counts[j] += 1;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    shown_type_ids[shown_type_count] = type_id;
                    shown_type_counts[shown_type_count] = 1;
                    shown_type_count += 1;
                }
            }
        }
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

    if (app->map.metadata.generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE) {
        (void)snprintf(
            line,
            sizeof(line),
            "room types: assigned=%llu unassigned=%llu",
            (unsigned long long)assigned_room_count,
            (unsigned long long)unassigned_room_count
        );
        nk_label(ctx, line, NK_TEXT_LEFT);

        for (i = 0; i < shown_type_count; ++i) {
            (void)snprintf(
                line,
                sizeof(line),
                "  type %u -> %llu rooms",
                (unsigned int)shown_type_ids[i],
                (unsigned long long)shown_type_counts[i]
            );
            nk_label(ctx, line, NK_TEXT_LEFT);
        }
    }

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

    if (app->map.metadata.diagnostics.process_step_count > 0) {
        (void)snprintf(
            line,
            sizeof(line),
            "process diagnostics: %llu step(s)",
            (unsigned long long)app->map.metadata.diagnostics.process_step_count
        );
        nk_label(ctx, line, NK_TEXT_LEFT);

        for (i = 0; i < app->map.metadata.diagnostics.process_step_count; ++i) {
            const dg_process_step_diagnostics_t *step = &app->map.metadata.diagnostics.process_steps[i];
            (void)snprintf(
                line,
                sizeof(line),
                "  %llu) %s walk %llu->%llu (%+lld) comp %llu->%llu",
                (unsigned long long)(i + 1),
                dg_nuklear_process_method_label((dg_process_method_type_t)step->method_type),
                (unsigned long long)step->walkable_before,
                (unsigned long long)step->walkable_after,
                (long long)step->walkable_delta,
                (unsigned long long)step->components_before,
                (unsigned long long)step->components_after
            );
            nk_label(ctx, line, NK_TEXT_LEFT);
        }
    }

    if (app->map.metadata.diagnostics.room_type_count > 0) {
        (void)snprintf(
            line,
            sizeof(line),
            "type quotas: min_miss=%llu max_excess=%llu target_miss=%llu",
            (unsigned long long)app->map.metadata.diagnostics.room_type_min_miss_count,
            (unsigned long long)app->map.metadata.diagnostics.room_type_max_excess_count,
            (unsigned long long)app->map.metadata.diagnostics.room_type_target_miss_count
        );
        nk_label(ctx, line, NK_TEXT_LEFT);

        for (i = 0; i < app->map.metadata.diagnostics.room_type_count; ++i) {
            const dg_room_type_quota_diagnostics_t *quota =
                &app->map.metadata.diagnostics.room_type_quotas[i];
            (void)snprintf(
                line,
                sizeof(line),
                "  type %u assigned=%llu min=%d max=%d target=%d",
                (unsigned int)quota->type_id,
                (unsigned long long)quota->assigned_count,
                quota->min_count,
                quota->max_count,
                quota->target_count
            );
            nk_label(ctx, line, NK_TEXT_LEFT);
        }
    }
}

static void dg_nuklear_draw_generation_settings(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    static const char *generation_classes[] = {"Room-like", "Cave-like"};
    const char *algorithm_labels[DG_NUKLEAR_ALGORITHM_COUNT];
    int algorithm_indices[DG_NUKLEAR_ALGORITHM_COUNT];
    dg_map_generation_class_t selected_class;
    int previous_algorithm_index;
    int previous_class_index;
    int selected_filtered_index;
    int filtered_count;
    int i;

    previous_algorithm_index = app->algorithm_index;
    previous_class_index = app->generation_class_index;

    app->generation_class_index = dg_nuklear_clamp_int(app->generation_class_index, 0, 1);
    app->algorithm_index = dg_nuklear_clamp_int(
        app->algorithm_index,
        0,
        DG_NUKLEAR_ALGORITHM_COUNT - 1
    );

    dg_nuklear_draw_subsection_heading(
        ctx,
        "Layout Setup",
        "Pick generation family, choose an algorithm, and iterate with size and seed."
    );

    nk_layout_row_dynamic(ctx, 19.0f, 1);
    nk_label(ctx, "Generation Type", NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 32.0f, 1);
    app->generation_class_index = nk_combo(
        ctx,
        generation_classes,
        (int)(sizeof(generation_classes) / sizeof(generation_classes[0])),
        app->generation_class_index,
        28,
        nk_vec2(240.0f, 116.0f)
    );
    app->generation_class_index = dg_nuklear_clamp_int(app->generation_class_index, 0, 1);
    dg_nuklear_ensure_algorithm_matches_class(app);

    selected_class = dg_nuklear_generation_class_from_index(app->generation_class_index);
    filtered_count = 0;
    selected_filtered_index = 0;
    for (i = 0; i < DG_NUKLEAR_ALGORITHM_COUNT; ++i) {
        dg_algorithm_t candidate = DG_NUKLEAR_ALGORITHMS[i];
        if (dg_algorithm_generation_class(candidate) != selected_class) {
            continue;
        }

        algorithm_labels[filtered_count] = dg_nuklear_algorithm_display_name(candidate);
        algorithm_indices[filtered_count] = i;
        if (i == app->algorithm_index) {
            selected_filtered_index = filtered_count;
        }
        filtered_count += 1;
    }

    if (filtered_count == 0) {
        app->algorithm_index = 0;
        algorithm_labels[0] =
            dg_nuklear_algorithm_display_name(dg_nuklear_algorithm_from_index(app->algorithm_index));
        algorithm_indices[0] = app->algorithm_index;
        filtered_count = 1;
        selected_filtered_index = 0;
    }

    nk_layout_row_dynamic(ctx, 19.0f, 1);
    nk_label(ctx, "Algorithm", NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 32.0f, 1);
    selected_filtered_index = nk_combo(
        ctx,
        algorithm_labels,
        filtered_count,
        selected_filtered_index,
        28,
        nk_vec2(300.0f, 200.0f)
    );
    if (selected_filtered_index < 0 || selected_filtered_index >= filtered_count) {
        selected_filtered_index = 0;
    }
    app->algorithm_index = algorithm_indices[selected_filtered_index];

    if (app->generation_class_index != previous_class_index) {
        dg_nuklear_set_status(
            app,
            "Selected generation type: %s",
            generation_classes[app->generation_class_index]
        );
    }

    if (app->algorithm_index != previous_algorithm_index) {
        dg_nuklear_set_status(
            app,
            "Selected algorithm: %s",
            dg_nuklear_algorithm_name(dg_nuklear_algorithm_from_index(app->algorithm_index))
        );
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_label(ctx, "", NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 30.0f, 2);
    nk_property_int(ctx, "Map Width", 8, &app->width, 512, 1, 0.25f);
    nk_property_int(ctx, "Map Height", 8, &app->height, 512, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 19.0f, 1);
    nk_label(ctx, "Seed", NK_TEXT_LEFT);

    {
        const float seed_cols[] = {0.70f, 0.30f};
        nk_layout_row(ctx, NK_DYNAMIC, 30.0f, 2, seed_cols);
    }
    (void)nk_edit_string_zero_terminated(
        ctx,
        NK_EDIT_FIELD,
        app->seed_text,
        (int)sizeof(app->seed_text),
        nk_filter_decimal
    );
    if (nk_button_label(ctx, "Randomize")) {
        dg_nuklear_randomize_seed(app);
    }
}

static void dg_nuklear_draw_bsp_settings(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Min Rooms", 1, &app->bsp_config.min_rooms, 256, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Max Rooms", 1, &app->bsp_config.max_rooms, 256, 1, 0.25f);

    if (nk_tree_push(ctx, NK_TREE_TAB, "Advanced", NK_MINIMIZED)) {
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Room Min Size", 3, &app->bsp_config.room_min_size, 64, 1, 0.25f);

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(ctx, "Room Max Size", 3, &app->bsp_config.room_max_size, 64, 1, 0.25f);
        nk_tree_pop(ctx);
    }

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

static void dg_nuklear_draw_room_graph_settings(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Min Rooms", 1, &app->room_graph_config.min_rooms, 256, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(ctx, "Max Rooms", 1, &app->room_graph_config.max_rooms, 256, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Extra Loops (%)",
        0,
        &app->room_graph_config.extra_connection_chance_percent,
        100,
        1,
        0.25f
    );

    if (nk_tree_push(ctx, NK_TREE_TAB, "Advanced", NK_MINIMIZED)) {
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Room Min Size",
            3,
            &app->room_graph_config.room_min_size,
            64,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Room Max Size",
            3,
            &app->room_graph_config.room_max_size,
            64,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Neighbor Candidates",
            1,
            &app->room_graph_config.neighbor_candidates,
            8,
            1,
            0.25f
        );
        nk_tree_pop(ctx);
    }

    if (app->room_graph_config.max_rooms < app->room_graph_config.min_rooms) {
        app->room_graph_config.max_rooms = app->room_graph_config.min_rooms;
    }
    if (app->room_graph_config.room_max_size < app->room_graph_config.room_min_size) {
        app->room_graph_config.room_max_size = app->room_graph_config.room_min_size;
    }
    app->room_graph_config.neighbor_candidates =
        dg_nuklear_clamp_int(app->room_graph_config.neighbor_candidates, 1, 8);
    app->room_graph_config.extra_connection_chance_percent =
        dg_nuklear_clamp_int(app->room_graph_config.extra_connection_chance_percent, 0, 100);

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Reset Room Graph Defaults")) {
        dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ROOM_GRAPH);
        dg_nuklear_set_status(app, "Room Graph defaults restored.");
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

static void dg_nuklear_draw_worm_caves_settings(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Worm Count",
        1,
        &app->worm_caves_config.worm_count,
        128,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Wiggle (%)",
        0,
        &app->worm_caves_config.wiggle_percent,
        100,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Target Floor (%)",
        5,
        &app->worm_caves_config.target_floor_percent,
        90,
        1,
        0.25f
    );

    if (nk_tree_push(ctx, NK_TREE_TAB, "Advanced", NK_MINIMIZED)) {
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Branch Chance (%)",
            0,
            &app->worm_caves_config.branch_chance_percent,
            100,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Brush Radius",
            0,
            &app->worm_caves_config.brush_radius,
            3,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Max Steps/Worm",
            8,
            &app->worm_caves_config.max_steps_per_worm,
            20000,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 24.0f, 1);
        app->worm_caves_config.ensure_connected = nk_check_label(
            ctx,
            "Ensure Connected Floor",
            app->worm_caves_config.ensure_connected
        );
        nk_tree_pop(ctx);
    }
    app->worm_caves_config.worm_count =
        dg_nuklear_clamp_int(app->worm_caves_config.worm_count, 1, 128);
    app->worm_caves_config.wiggle_percent =
        dg_nuklear_clamp_int(app->worm_caves_config.wiggle_percent, 0, 100);
    app->worm_caves_config.branch_chance_percent =
        dg_nuklear_clamp_int(app->worm_caves_config.branch_chance_percent, 0, 100);
    app->worm_caves_config.target_floor_percent =
        dg_nuklear_clamp_int(app->worm_caves_config.target_floor_percent, 5, 90);
    app->worm_caves_config.brush_radius =
        dg_nuklear_clamp_int(app->worm_caves_config.brush_radius, 0, 3);
    app->worm_caves_config.max_steps_per_worm =
        dg_nuklear_clamp_int(app->worm_caves_config.max_steps_per_worm, 8, 20000);
    app->worm_caves_config.ensure_connected = app->worm_caves_config.ensure_connected ? 1 : 0;

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Reset Worm Caves Defaults")) {
        dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_WORM_CAVES);
        dg_nuklear_set_status(app, "Worm Caves defaults restored.");
    }
}

static void dg_nuklear_draw_cellular_automata_settings(
    struct nk_context *ctx,
    dg_nuklear_app_t *app
)
{
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Initial Walls (%)",
        0,
        &app->cellular_automata_config.initial_wall_percent,
        100,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Simulation Steps",
        1,
        &app->cellular_automata_config.simulation_steps,
        12,
        1,
        0.25f
    );

    if (nk_tree_push(ctx, NK_TREE_TAB, "Advanced", NK_MINIMIZED)) {
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Wall Threshold",
            0,
            &app->cellular_automata_config.wall_threshold,
            8,
            1,
            0.25f
        );
        nk_tree_pop(ctx);
    }

    app->cellular_automata_config.initial_wall_percent =
        dg_nuklear_clamp_int(app->cellular_automata_config.initial_wall_percent, 0, 100);
    app->cellular_automata_config.simulation_steps =
        dg_nuklear_clamp_int(app->cellular_automata_config.simulation_steps, 1, 12);
    app->cellular_automata_config.wall_threshold =
        dg_nuklear_clamp_int(app->cellular_automata_config.wall_threshold, 0, 8);

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Reset Cellular Defaults")) {
        dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_CELLULAR_AUTOMATA);
        dg_nuklear_set_status(app, "Cellular Automata defaults restored.");
    }
}

static void dg_nuklear_draw_value_noise_settings(
    struct nk_context *ctx,
    dg_nuklear_app_t *app
)
{
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Feature Size",
        2,
        &app->value_noise_config.feature_size,
        64,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Floor Threshold (%)",
        0,
        &app->value_noise_config.floor_threshold_percent,
        100,
        1,
        0.25f
    );

    if (nk_tree_push(ctx, NK_TREE_TAB, "Advanced", NK_MINIMIZED)) {
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Octaves",
            1,
            &app->value_noise_config.octaves,
            6,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Persistence (%)",
            10,
            &app->value_noise_config.persistence_percent,
            90,
            1,
            0.25f
        );
        nk_tree_pop(ctx);
    }

    app->value_noise_config.feature_size =
        dg_nuklear_clamp_int(app->value_noise_config.feature_size, 2, 64);
    app->value_noise_config.octaves =
        dg_nuklear_clamp_int(app->value_noise_config.octaves, 1, 6);
    app->value_noise_config.persistence_percent =
        dg_nuklear_clamp_int(app->value_noise_config.persistence_percent, 10, 90);
    app->value_noise_config.floor_threshold_percent =
        dg_nuklear_clamp_int(app->value_noise_config.floor_threshold_percent, 0, 100);

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Reset Value Noise Defaults")) {
        dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_VALUE_NOISE);
        dg_nuklear_set_status(app, "Value Noise defaults restored.");
    }
}

static void dg_nuklear_draw_simplex_noise_settings(
    struct nk_context *ctx,
    dg_nuklear_app_t *app
)
{
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Feature Size",
        2,
        &app->simplex_noise_config.feature_size,
        128,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    nk_property_int(
        ctx,
        "Floor Threshold (%)",
        0,
        &app->simplex_noise_config.floor_threshold_percent,
        100,
        1,
        0.25f
    );

    if (nk_tree_push(ctx, NK_TREE_TAB, "Advanced", NK_MINIMIZED)) {
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Octaves",
            1,
            &app->simplex_noise_config.octaves,
            8,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Persistence (%)",
            10,
            &app->simplex_noise_config.persistence_percent,
            90,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 24.0f, 1);
        app->simplex_noise_config.ensure_connected = nk_check_label(
            ctx,
            "Ensure Connected Floor",
            app->simplex_noise_config.ensure_connected
        );
        nk_tree_pop(ctx);
    }
    app->simplex_noise_config.feature_size =
        dg_nuklear_clamp_int(app->simplex_noise_config.feature_size, 2, 128);
    app->simplex_noise_config.octaves =
        dg_nuklear_clamp_int(app->simplex_noise_config.octaves, 1, 8);
    app->simplex_noise_config.persistence_percent =
        dg_nuklear_clamp_int(app->simplex_noise_config.persistence_percent, 10, 90);
    app->simplex_noise_config.floor_threshold_percent =
        dg_nuklear_clamp_int(app->simplex_noise_config.floor_threshold_percent, 0, 100);
    app->simplex_noise_config.ensure_connected =
        app->simplex_noise_config.ensure_connected ? 1 : 0;

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Reset Simplex Defaults")) {
        dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_SIMPLEX_NOISE);
        dg_nuklear_set_status(app, "Simplex Noise defaults restored.");
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
        "Maze Wiggle (%)",
        0,
        &app->rooms_and_mazes_config.maze_wiggle_percent,
        100,
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

    if (nk_tree_push(ctx, NK_TREE_TAB, "Advanced", NK_MINIMIZED)) {
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
            "Min Connections",
            1,
            &app->rooms_and_mazes_config.min_room_connections,
            16,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Max Connections",
            1,
            &app->rooms_and_mazes_config.max_room_connections,
            16,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 24.0f, 1);
        app->rooms_and_mazes_config.ensure_full_connectivity = nk_check_label(
            ctx,
            "Ensure Full Connectivity",
            app->rooms_and_mazes_config.ensure_full_connectivity
        );
        nk_tree_pop(ctx);
    }

    if (app->rooms_and_mazes_config.max_rooms < app->rooms_and_mazes_config.min_rooms) {
        app->rooms_and_mazes_config.max_rooms = app->rooms_and_mazes_config.min_rooms;
    }

    if (app->rooms_and_mazes_config.room_max_size < app->rooms_and_mazes_config.room_min_size) {
        app->rooms_and_mazes_config.room_max_size = app->rooms_and_mazes_config.room_min_size;
    }

    if (app->rooms_and_mazes_config.maze_wiggle_percent < 0) {
        app->rooms_and_mazes_config.maze_wiggle_percent = 0;
    } else if (app->rooms_and_mazes_config.maze_wiggle_percent > 100) {
        app->rooms_and_mazes_config.maze_wiggle_percent = 100;
    }

    if (app->rooms_and_mazes_config.max_room_connections <
        app->rooms_and_mazes_config.min_room_connections) {
        app->rooms_and_mazes_config.max_room_connections =
            app->rooms_and_mazes_config.min_room_connections;
    }

    app->rooms_and_mazes_config.ensure_full_connectivity =
        app->rooms_and_mazes_config.ensure_full_connectivity ? 1 : 0;

    if (app->rooms_and_mazes_config.dead_end_prune_steps < -1) {
        app->rooms_and_mazes_config.dead_end_prune_steps = -1;
    }

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Reset Rooms+Mazes Defaults")) {
        dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ROOMS_AND_MAZES);
        dg_nuklear_set_status(app, "Rooms + Mazes defaults restored.");
    }
}

static const char *dg_nuklear_room_shape_mode_label(dg_room_shape_mode_t mode)
{
    switch (mode) {
    case DG_ROOM_SHAPE_RECTANGULAR:
        return "Rectangular";
    case DG_ROOM_SHAPE_CELLULAR:
        return "Cellular";
    case DG_ROOM_SHAPE_CHAMFERED:
        return "Chamfered";
    case DG_ROOM_SHAPE_ORGANIC:
    default:
        return "Organic";
    }
}

static const char *dg_nuklear_corridor_roughen_mode_label(dg_corridor_roughen_mode_t mode)
{
    return (mode == DG_CORRIDOR_ROUGHEN_UNIFORM) ? "Uniform" : "Organic";
}

static void dg_nuklear_process_method_summary(
    const dg_process_method_t *method,
    char *out_text,
    size_t out_text_capacity
)
{
    if (out_text == NULL || out_text_capacity == 0u) {
        return;
    }

    if (method == NULL) {
        (void)snprintf(out_text, out_text_capacity, "Unknown");
        return;
    }

    switch (method->type) {
    case DG_PROCESS_METHOD_SCALE:
        (void)snprintf(
            out_text,
            out_text_capacity,
            "Factor x%d",
            method->params.scale.factor
        );
        break;
    case DG_PROCESS_METHOD_ROOM_SHAPE:
        if (method->params.room_shape.mode == DG_ROOM_SHAPE_RECTANGULAR) {
            (void)snprintf(
                out_text,
                out_text_capacity,
                "Mode: %s",
                dg_nuklear_room_shape_mode_label(method->params.room_shape.mode)
            );
        } else {
            (void)snprintf(
                out_text,
                out_text_capacity,
                "Mode: %s, Strength %d%%",
                dg_nuklear_room_shape_mode_label(method->params.room_shape.mode),
                method->params.room_shape.organicity
            );
        }
        break;
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        (void)snprintf(
            out_text,
            out_text_capacity,
            "Strength %d, Inner %s, Outer %s",
            method->params.path_smooth.strength,
            method->params.path_smooth.inner_enabled ? "On" : "Off",
            method->params.path_smooth.outer_enabled ? "On" : "Off"
        );
        break;
    case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
        (void)snprintf(
            out_text,
            out_text_capacity,
            "%s, %d%%, Depth %d",
            dg_nuklear_corridor_roughen_mode_label(method->params.corridor_roughen.mode),
            method->params.corridor_roughen.strength,
            method->params.corridor_roughen.max_depth
        );
        break;
    default:
        (void)snprintf(out_text, out_text_capacity, "Unknown");
        break;
    }
}

static void dg_nuklear_draw_process_method_editor(
    struct nk_context *ctx,
    dg_nuklear_app_t *app,
    dg_algorithm_t algorithm,
    dg_process_method_t *method
)
{
    int type_index;

    if (ctx == NULL || app == NULL || method == NULL) {
        return;
    }

    type_index = dg_nuklear_process_type_to_ui_index(method->type);
    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Step Type", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    type_index = nk_combo(
        ctx,
        DG_NUKLEAR_PROCESS_METHOD_TYPES,
        (int)(sizeof(DG_NUKLEAR_PROCESS_METHOD_TYPES) / sizeof(DG_NUKLEAR_PROCESS_METHOD_TYPES[0])),
        type_index,
        22,
        nk_vec2(260.0f, 96.0f)
    );
    if (type_index < 0 ||
        type_index >= (int)(sizeof(DG_NUKLEAR_PROCESS_METHOD_TYPES) / sizeof(DG_NUKLEAR_PROCESS_METHOD_TYPES[0]))) {
        type_index = 0;
    }
    if (dg_nuklear_process_type_to_ui_index(method->type) != type_index) {
        dg_default_process_method(
            method,
            dg_nuklear_process_ui_index_to_type(type_index)
        );
    }

    if (method->type == DG_PROCESS_METHOD_SCALE) {
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Scale Factor",
            1,
            &method->params.scale.factor,
            8,
            1,
            0.25f
        );
    } else if (method->type == DG_PROCESS_METHOD_ROOM_SHAPE) {
        int room_shape_index =
            dg_nuklear_room_shape_mode_to_ui_index(method->params.room_shape.mode);

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Room Shape Mode", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        room_shape_index = nk_combo(
            ctx,
            DG_NUKLEAR_ROOM_SHAPE_MODES,
            (int)(sizeof(DG_NUKLEAR_ROOM_SHAPE_MODES) / sizeof(DG_NUKLEAR_ROOM_SHAPE_MODES[0])),
            room_shape_index,
            22,
            nk_vec2(280.0f, 112.0f)
        );
        method->params.room_shape.mode =
            dg_nuklear_room_shape_ui_index_to_mode(room_shape_index);

        if (method->params.room_shape.mode != DG_ROOM_SHAPE_RECTANGULAR) {
            nk_layout_row_dynamic(ctx, 28.0f, 1);
            nk_property_int(
                ctx,
                "Strength (%)",
                0,
                &method->params.room_shape.organicity,
                100,
                1,
                0.25f
            );
        }
        if (method->params.room_shape.mode != DG_ROOM_SHAPE_RECTANGULAR &&
            dg_algorithm_generation_class(algorithm) != DG_MAP_GENERATION_CLASS_ROOM_LIKE) {
            nk_layout_row_dynamic(ctx, 36.0f, 1);
            nk_label_wrap(
                ctx,
                "Room shape processing only affects room-like layouts."
            );
        }
    } else if (method->type == DG_PROCESS_METHOD_PATH_SMOOTH) {
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Strength",
            0,
            &method->params.path_smooth.strength,
            12,
            1,
            0.25f
        );
        nk_layout_row_dynamic(ctx, 24.0f, 2);
        method->params.path_smooth.inner_enabled = nk_check_label(
            ctx,
            "Inner",
            method->params.path_smooth.inner_enabled
        );
        method->params.path_smooth.outer_enabled = nk_check_label(
            ctx,
            "Outer",
            method->params.path_smooth.outer_enabled
        );
        nk_layout_row_dynamic(ctx, 36.0f, 1);
        nk_label_wrap(
            ctx,
            "Inner fills bend corners; outer trims matching outer corners while preserving connectivity."
        );
    } else if (method->type == DG_PROCESS_METHOD_CORRIDOR_ROUGHEN) {
        int mode_index =
            (method->params.corridor_roughen.mode == DG_CORRIDOR_ROUGHEN_UNIFORM) ? 0 : 1;

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Strength (%)",
            0,
            &method->params.corridor_roughen.strength,
            100,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        nk_property_int(
            ctx,
            "Max Depth",
            1,
            &method->params.corridor_roughen.max_depth,
            32,
            1,
            0.25f
        );

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Mode", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28.0f, 1);
        mode_index = nk_combo(
            ctx,
            DG_NUKLEAR_CORRIDOR_ROUGHEN_MODES,
            (int)(sizeof(DG_NUKLEAR_CORRIDOR_ROUGHEN_MODES) / sizeof(DG_NUKLEAR_CORRIDOR_ROUGHEN_MODES[0])),
            mode_index,
            22,
            nk_vec2(220.0f, 72.0f)
        );
        method->params.corridor_roughen.mode =
            (mode_index == 0) ? DG_CORRIDOR_ROUGHEN_UNIFORM : DG_CORRIDOR_ROUGHEN_ORGANIC;
    }

    dg_nuklear_sanitize_process_method(method);
}

static void dg_nuklear_draw_process_settings(
    struct nk_context *ctx,
    dg_nuklear_app_t *app,
    dg_algorithm_t algorithm
)
{
    int i;

    if (ctx == NULL || app == NULL) {
        return;
    }

    dg_nuklear_draw_subsection_heading(
        ctx,
        "Post-Process Pipeline",
        "Build your processing stack in order. Later steps operate on results of earlier ones."
    );

    dg_nuklear_sanitize_process_settings(app);

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    {
        char line[96];
        (void)snprintf(
            line,
            sizeof(line),
            "Pipeline Steps: %d / %d (%s)",
            app->process_method_count,
            DG_NUKLEAR_MAX_PROCESS_METHODS,
            app->process_enabled ? "Enabled" : "Disabled"
        );
        nk_label(ctx, line, NK_TEXT_LEFT);
    }

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    app->process_enabled = nk_check_label(
        ctx,
        "Enable Post-Processing",
        app->process_enabled
    );

    if (!app->process_enabled) {
        nk_layout_row_dynamic(ctx, 34.0f, 1);
        nk_label_wrap(
            ctx,
            "Post-processing is currently bypassed. Layout and room changes preview without process steps."
        );
    }

    nk_layout_row_dynamic(ctx, 28.0f, 3);
    app->process_add_method_type_index = nk_combo(
        ctx,
        DG_NUKLEAR_PROCESS_METHOD_TYPES,
        (int)(sizeof(DG_NUKLEAR_PROCESS_METHOD_TYPES) / sizeof(DG_NUKLEAR_PROCESS_METHOD_TYPES[0])),
        app->process_add_method_type_index,
        22,
        nk_vec2(240.0f, 96.0f)
    );
    if (nk_button_label(ctx, "Add Step")) {
        dg_process_method_type_t type =
            dg_nuklear_process_ui_index_to_type(app->process_add_method_type_index);
        if (dg_nuklear_append_process_method(app, type)) {
            dg_nuklear_set_status(
                app,
                "Added process step %d (%s).",
                app->process_method_count,
                dg_nuklear_process_method_label(type)
            );
        } else {
            dg_nuklear_set_status(app, "Maximum process step count reached.");
        }
    }
    if (nk_button_label(ctx, "Clear All")) {
        int keep_enabled = app->process_enabled ? 1 : 0;
        dg_nuklear_reset_process_defaults(app);
        app->process_enabled = keep_enabled;
        dg_nuklear_set_status(app, "Cleared post-process pipeline.");
    }

    if (app->process_method_count == 0) {
        nk_layout_row_dynamic(ctx, 36.0f, 1);
        nk_label_wrap(
            ctx,
            "No post-process steps configured. Add steps above to define a custom process pipeline."
        );
        return;
    }

    if (app->process_selected_index < 0 || app->process_selected_index >= app->process_method_count) {
        app->process_selected_index = 0;
    }

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Pipeline Steps", NK_TEXT_LEFT);

    for (i = 0; i < app->process_method_count; ++i) {
        dg_process_method_t *method = &app->process_methods[i];
        int is_selected = (i == app->process_selected_index);
        char row_label[220];
        char summary[128];

        dg_nuklear_process_method_summary(method, summary, sizeof(summary));
        (void)snprintf(
            row_label,
            sizeof(row_label),
            "%c %d. %s  |  %s",
            is_selected ? '>' : ' ',
            i + 1,
            dg_nuklear_process_method_label(method->type),
            summary
        );

        nk_layout_row_dynamic(ctx, 28.0f, 1);
        if (nk_button_label(ctx, row_label)) {
            app->process_selected_index = i;
        }
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_label(ctx, "", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Selected Step Actions", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 30.0f, 4);
    if (nk_button_label(ctx, "Move Up")) {
        if (dg_nuklear_move_process_method(app, app->process_selected_index, -1)) {
            dg_nuklear_set_status(
                app,
                "Moved step %d up.",
                app->process_selected_index + 2
            );
        }
        return;
    }
    if (nk_button_label(ctx, "Move Down")) {
        if (dg_nuklear_move_process_method(app, app->process_selected_index, 1)) {
            dg_nuklear_set_status(
                app,
                "Moved step %d down.",
                app->process_selected_index
            );
        }
        return;
    }
    if (nk_button_label(ctx, "Duplicate")) {
        if (dg_nuklear_duplicate_process_method(app, app->process_selected_index)) {
            dg_nuklear_set_status(
                app,
                "Duplicated step %d to step %d.",
                app->process_selected_index + 1,
                app->process_selected_index + 2
            );
        } else {
            dg_nuklear_set_status(app, "Cannot duplicate step. Maximum reached.");
        }
        return;
    }
    if (nk_button_label(ctx, "Remove")) {
        if (dg_nuklear_remove_process_method(app, app->process_selected_index)) {
            dg_nuklear_set_status(app, "Removed selected step.");
        }
        return;
    }

    if (nk_tree_push(ctx, NK_TREE_TAB, "Step Editor", NK_MAXIMIZED)) {
        dg_process_method_t *selected = &app->process_methods[app->process_selected_index];
        char step_title[120];

        (void)snprintf(
            step_title,
            sizeof(step_title),
            "%d. %s",
            app->process_selected_index + 1,
            dg_nuklear_process_method_label(selected->type)
        );

        nk_layout_row_dynamic(ctx, 22.0f, 1);
        nk_label(ctx, step_title, NK_TEXT_LEFT);
        dg_nuklear_draw_process_method_editor(ctx, app, algorithm, selected);
        nk_tree_pop(ctx);
    }
}

static void dg_nuklear_draw_room_type_slot(
    struct nk_context *ctx,
    dg_nuklear_room_type_ui_t *slot,
    int slot_index
)
{
    struct nk_color preview_color;

    if (ctx == NULL || slot == NULL || slot_index < 0) {
        return;
    }

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Basics", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    slot->enabled = nk_check_label(ctx, "Enabled", slot->enabled);

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Label", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 26.0f, 1);
    (void)nk_edit_string_zero_terminated(
        ctx,
        NK_EDIT_FIELD,
        slot->label,
        (int)sizeof(slot->label),
        nk_filter_default
    );

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_property_int(ctx, "Type ID", 0, &slot->type_id, INT_MAX, 1, 0.25f);

    preview_color = dg_nuklear_color_for_room_type((uint32_t)dg_nuklear_clamp_int(slot->type_id, 0, INT_MAX));
    nk_layout_row_dynamic(ctx, 24.0f, 2);
    nk_label(ctx, "Color", NK_TEXT_LEFT);
    (void)nk_button_color(ctx, preview_color);

    if (nk_tree_push_id(ctx, NK_TREE_TAB, "Quotas", NK_MINIMIZED, 1000 + slot_index * 10 + 1)) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Min Count", 0, &slot->min_count, INT_MAX, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Max Count (-1=Any)", -1, &slot->max_count, INT_MAX, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Target Count (-1=None)", -1, &slot->target_count, INT_MAX, 1, 0.25f);
        nk_tree_pop(ctx);
    }

    if (nk_tree_push_id(
            ctx,
            NK_TREE_TAB,
            "Constraints",
            NK_MINIMIZED,
            1000 + slot_index * 10 + 2
        )) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Area Min", 0, &slot->area_min, INT_MAX, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Area Max (-1=Any)", -1, &slot->area_max, INT_MAX, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Degree Min", 0, &slot->degree_min, INT_MAX, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Degree Max (-1=Any)", -1, &slot->degree_max, INT_MAX, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Border Dist Min", 0, &slot->border_distance_min, INT_MAX, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(
            ctx,
            "Border Dist Max (-1=Any)",
            -1,
            &slot->border_distance_max,
            INT_MAX,
            1,
            0.25f
        );
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Graph Depth Min", 0, &slot->graph_depth_min, INT_MAX, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(
            ctx,
            "Graph Depth Max (-1=Any)",
            -1,
            &slot->graph_depth_max,
            INT_MAX,
            1,
            0.25f
        );
        nk_tree_pop(ctx);
    }

    if (nk_tree_push_id(
            ctx,
            NK_TREE_TAB,
            "Preferences",
            NK_MINIMIZED,
            1000 + slot_index * 10 + 3
        )) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Weight", 0, &slot->weight, INT_MAX, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Larger Room Bias", -100, &slot->larger_room_bias, 100, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(ctx, "Higher Degree Bias", -100, &slot->higher_degree_bias, 100, 1, 0.25f);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_property_int(
            ctx,
            "Border Distance Bias",
            -100,
            &slot->border_distance_bias,
            100,
            1,
            0.25f
        );
        nk_tree_pop(ctx);
    }

    dg_nuklear_sanitize_room_type_slot(slot);
}

static void dg_nuklear_draw_room_type_settings(
    struct nk_context *ctx,
    dg_nuklear_app_t *app,
    dg_algorithm_t algorithm
)
{
    int i;
    int pending_remove_index;

    if (ctx == NULL || app == NULL) {
        return;
    }

    dg_nuklear_sanitize_room_type_settings(app);

    if (!dg_nuklear_algorithm_supports_room_types(algorithm)) {
        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Room types are only available for room-like algorithms.", NK_TEXT_LEFT);
        return;
    }

    dg_nuklear_draw_subsection_heading(
        ctx,
        "Room Type Assignment",
        "Define reusable room categories with counts, constraints, and weighted preferences."
    );

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    app->room_types_enabled = nk_check_label(
        ctx,
        "Enable Room Type Assignment",
        app->room_types_enabled
    );

    if (!app->room_types_enabled) {
        nk_layout_row_dynamic(ctx, 36.0f, 1);
        nk_label_wrap(
            ctx,
            "Disabled: rooms stay untyped. Enable this to assign configurable type IDs."
        );
        return;
    }

    pending_remove_index = -1;

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    {
        char line[96];
        (void)snprintf(
            line,
            sizeof(line),
            "Configured Types: %d / %d",
            app->room_type_count,
            DG_NUKLEAR_MAX_ROOM_TYPES
        );
        nk_label(ctx, line, NK_TEXT_LEFT);
    }

    nk_layout_row_dynamic(ctx, 30.0f, 3);
    if (nk_button_label(ctx, "Add Type")) {
        if (dg_nuklear_append_room_type_slot(app)) {
            app->room_types_enabled = 1;
            dg_nuklear_set_status(app, "Added room type %d.", app->room_type_count);
        } else {
            dg_nuklear_set_status(app, "Maximum room type count reached.");
        }
    }
    if (nk_button_label(ctx, "Remove Last")) {
        if (dg_nuklear_remove_room_type_slot(app, app->room_type_count - 1)) {
            dg_nuklear_set_status(app, "Removed last room type.");
        } else {
            dg_nuklear_set_status(app, "At least one room type slot must remain.");
        }
    }
    if (nk_button_label(ctx, "Reset Preset")) {
        dg_nuklear_reset_room_type_defaults(app);
        app->room_types_enabled = 1;
        dg_nuklear_set_status(app, "Room type preset restored.");
    }

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    app->room_type_strict_mode = nk_check_label(ctx, "Strict Mode (fail on infeasible)", app->room_type_strict_mode);

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    app->room_type_allow_untyped = nk_check_label(ctx, "Allow Untyped Rooms", app->room_type_allow_untyped);

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_property_int(
        ctx,
        "Default Type ID",
        0,
        &app->room_type_default_type_id,
        INT_MAX,
        1,
        0.25f
    );

    nk_layout_row_dynamic(ctx, 36.0f, 1);
    nk_label_wrap(
        ctx,
        "Tip: start with 2-3 broad types, then tighten constraints after checking the typed map preview."
    );

    for (i = 0; i < app->room_type_count; ++i) {
        char section_title[96];
        const char *label;

        label = app->room_type_slots[i].label[0] != '\0' ? app->room_type_slots[i].label : "Type";
        (void)snprintf(
            section_title,
            sizeof(section_title),
            "Type %d: %s (id %d)",
            i + 1,
            label,
            app->room_type_slots[i].type_id
        );

        if (nk_tree_push_id(ctx, NK_TREE_TAB, section_title, NK_MINIMIZED, i + 1)) {
            nk_layout_row_dynamic(ctx, 28.0f, 3);
            if (nk_button_label(ctx, "Duplicate")) {
                if (app->room_type_count >= DG_NUKLEAR_MAX_ROOM_TYPES) {
                    dg_nuklear_set_status(app, "Cannot duplicate: maximum room type count reached.");
                } else if (dg_nuklear_append_room_type_slot(app)) {
                    app->room_type_slots[app->room_type_count - 1] = app->room_type_slots[i];
                    app->room_type_slots[app->room_type_count - 1].type_id = dg_nuklear_next_room_type_id(app);
                    dg_nuklear_sanitize_room_type_settings(app);
                    dg_nuklear_set_status(app, "Duplicated type %d into slot %d.", i + 1, app->room_type_count);
                }
            }
            if (nk_button_label(ctx, "Remove")) {
                pending_remove_index = i;
            }
            nk_label(ctx, " ", NK_TEXT_LEFT);

            dg_nuklear_draw_room_type_slot(ctx, &app->room_type_slots[i], i);
            nk_tree_pop(ctx);
        }
    }

    if (pending_remove_index >= 0) {
        if (dg_nuklear_remove_room_type_slot(app, pending_remove_index)) {
            dg_nuklear_set_status(app, "Removed type %d.", pending_remove_index + 1);
        } else {
            dg_nuklear_set_status(app, "At least one room type slot must remain.");
        }
    }
}

static void dg_nuklear_draw_save_load(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    dg_nuklear_draw_subsection_heading(
        ctx,
        "File Path",
        "Save and load generation configs (.dgmap), or export a standalone PNG + JSON."
    );

    nk_layout_row_dynamic(ctx, 30.0f, 1);
    (void)nk_edit_string_zero_terminated(
        ctx,
        NK_EDIT_FIELD,
        app->file_path,
        (int)sizeof(app->file_path),
        nk_filter_default
    );

    nk_layout_row_dynamic(ctx, 32.0f, 2);
    if (nk_button_label(ctx, "Save Config")) {
        dg_nuklear_save_map(app);
    }
    if (nk_button_label(ctx, "Load Config")) {
        dg_nuklear_load_map(app);
    }

    nk_layout_row_dynamic(ctx, 34.0f, 1);
    if (nk_button_label(ctx, "Export PNG + JSON")) {
        dg_nuklear_export_map_png_json(app);
    }
}

static bool dg_nuklear_workflow_tab_button(
    struct nk_context *ctx,
    const char *label,
    int selected
)
{
    if (ctx == NULL || label == NULL) {
        return false;
    }

    if (selected) {
        (void)nk_style_push_style_item(
            ctx,
            &ctx->style.button.normal,
            nk_style_item_color(nk_rgb(72, 110, 146))
        );
        (void)nk_style_push_style_item(
            ctx,
            &ctx->style.button.hover,
            nk_style_item_color(nk_rgb(83, 124, 164))
        );
        (void)nk_style_push_style_item(
            ctx,
            &ctx->style.button.active,
            nk_style_item_color(nk_rgb(94, 138, 182))
        );
        (void)nk_style_push_color(ctx, &ctx->style.button.border_color, nk_rgb(125, 162, 196));
        (void)nk_style_push_color(ctx, &ctx->style.button.text_normal, nk_rgb(242, 247, 255));
        (void)nk_style_push_color(ctx, &ctx->style.button.text_hover, nk_rgb(250, 252, 255));
        (void)nk_style_push_color(ctx, &ctx->style.button.text_active, nk_rgb(255, 255, 255));
    } else {
        (void)nk_style_push_style_item(
            ctx,
            &ctx->style.button.normal,
            nk_style_item_color(nk_rgb(39, 49, 62))
        );
        (void)nk_style_push_style_item(
            ctx,
            &ctx->style.button.hover,
            nk_style_item_color(nk_rgb(52, 65, 81))
        );
        (void)nk_style_push_style_item(
            ctx,
            &ctx->style.button.active,
            nk_style_item_color(nk_rgb(63, 79, 98))
        );
        (void)nk_style_push_color(ctx, &ctx->style.button.border_color, nk_rgb(72, 89, 111));
        (void)nk_style_push_color(ctx, &ctx->style.button.text_normal, nk_rgb(203, 213, 227));
        (void)nk_style_push_color(ctx, &ctx->style.button.text_hover, nk_rgb(220, 229, 239));
        (void)nk_style_push_color(ctx, &ctx->style.button.text_active, nk_rgb(234, 241, 248));
    }
    (void)nk_style_push_float(ctx, &ctx->style.button.rounding, 8.0f);

    {
        int clicked = nk_button_label(ctx, label);
        (void)nk_style_pop_float(ctx);
        (void)nk_style_pop_color(ctx);
        (void)nk_style_pop_color(ctx);
        (void)nk_style_pop_color(ctx);
        (void)nk_style_pop_color(ctx);
        (void)nk_style_pop_style_item(ctx);
        (void)nk_style_pop_style_item(ctx);
        (void)nk_style_pop_style_item(ctx);
        return clicked != 0;
    }
}

static void dg_nuklear_draw_controls(struct nk_context *ctx, dg_nuklear_app_t *app)
{
    dg_algorithm_t algorithm;
    char context_line[192];

    if (ctx == NULL || app == NULL) {
        return;
    }

    app->controls_workflow_tab = dg_nuklear_clamp_int(
        app->controls_workflow_tab,
        DG_NUKLEAR_WORKFLOW_LAYOUT,
        DG_NUKLEAR_WORKFLOW_PROCESS
    );
    algorithm = dg_nuklear_algorithm_from_index(app->algorithm_index);

    (void)snprintf(
        context_line,
        sizeof(context_line),
        "Current: %s (%s)",
        dg_nuklear_algorithm_display_name(algorithm),
        app->generation_class_index == 0 ? "Room-like" : "Cave-like"
    );

    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, context_line, NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 34.0f, 3);
    if (dg_nuklear_workflow_tab_button(
            ctx,
            "Layout",
            app->controls_workflow_tab == DG_NUKLEAR_WORKFLOW_LAYOUT
        )) {
        app->controls_workflow_tab = DG_NUKLEAR_WORKFLOW_LAYOUT;
    }
    if (dg_nuklear_workflow_tab_button(
            ctx,
            "Rooms",
            app->controls_workflow_tab == DG_NUKLEAR_WORKFLOW_ROOMS
        )) {
        app->controls_workflow_tab = DG_NUKLEAR_WORKFLOW_ROOMS;
    }
    if (dg_nuklear_workflow_tab_button(
            ctx,
            "Post-Process",
            app->controls_workflow_tab == DG_NUKLEAR_WORKFLOW_PROCESS
        )) {
        app->controls_workflow_tab = DG_NUKLEAR_WORKFLOW_PROCESS;
    }

    nk_layout_row_dynamic(ctx, 6.0f, 1);
    nk_label(ctx, "", NK_TEXT_LEFT);

    if (app->controls_workflow_tab == DG_NUKLEAR_WORKFLOW_LAYOUT) {
        dg_nuklear_draw_generation_settings(ctx, app);
        algorithm = dg_nuklear_algorithm_from_index(app->algorithm_index);
        dg_nuklear_draw_subsection_heading(
            ctx,
            "Layout Parameters",
            "Tune algorithm-specific controls below."
        );

        if (algorithm == DG_ALGORITHM_DRUNKARDS_WALK) {
            dg_nuklear_draw_drunkards_settings(ctx, app);
        } else if (algorithm == DG_ALGORITHM_WORM_CAVES) {
            dg_nuklear_draw_worm_caves_settings(ctx, app);
        } else if (algorithm == DG_ALGORITHM_CELLULAR_AUTOMATA) {
            dg_nuklear_draw_cellular_automata_settings(ctx, app);
        } else if (algorithm == DG_ALGORITHM_SIMPLEX_NOISE) {
            dg_nuklear_draw_simplex_noise_settings(ctx, app);
        } else if (algorithm == DG_ALGORITHM_VALUE_NOISE) {
            dg_nuklear_draw_value_noise_settings(ctx, app);
        } else if (algorithm == DG_ALGORITHM_ROOM_GRAPH) {
            dg_nuklear_draw_room_graph_settings(ctx, app);
        } else if (algorithm == DG_ALGORITHM_ROOMS_AND_MAZES) {
            dg_nuklear_draw_rooms_and_mazes_settings(ctx, app);
        } else {
            dg_nuklear_draw_bsp_settings(ctx, app);
        }
    } else if (app->controls_workflow_tab == DG_NUKLEAR_WORKFLOW_ROOMS) {
        if (dg_nuklear_algorithm_supports_room_types(algorithm)) {
            dg_nuklear_draw_room_type_settings(ctx, app, algorithm);
        } else {
            nk_layout_row_dynamic(ctx, 48.0f, 1);
            nk_label_wrap(
                ctx,
                "Rooms workflow is unavailable for this layout algorithm. Switch to BSP Tree, Room Graph, or Rooms + Mazes."
            );
        }
    } else {
        dg_nuklear_draw_process_settings(ctx, app, algorithm);
    }

    dg_nuklear_maybe_auto_generate(app);

    if (nk_tree_push(ctx, NK_TREE_TAB, "Save / Load", NK_MINIMIZED)) {
        dg_nuklear_draw_save_load(ctx, app);
        nk_tree_pop(ctx);
    }

    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Status", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 56.0f, 1);
    nk_label_wrap(ctx, app->status_text);
}

void dg_nuklear_app_init(dg_nuklear_app_t *app)
{
    if (app == NULL) {
        return;
    }

    *app = (dg_nuklear_app_t){0};
    app->generation_class_index = 0;
    app->algorithm_index = 0;
    app->width = 40;
    app->height = 40;

    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_BSP_TREE);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ROOM_GRAPH);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_DRUNKARDS_WALK);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_WORM_CAVES);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_CELLULAR_AUTOMATA);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_VALUE_NOISE);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_SIMPLEX_NOISE);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ROOMS_AND_MAZES);
    dg_nuklear_reset_process_defaults(app);
    dg_nuklear_reset_room_type_defaults(app);
    app->controls_workflow_tab = DG_NUKLEAR_WORKFLOW_LAYOUT;
    dg_nuklear_reset_preview_camera(app);
    dg_nuklear_sync_generation_class_with_algorithm(app);
    app->layout_side_left_ratio = 0.30f;
    app->layout_side_map_ratio = 0.74f;
    app->layout_stacked_controls_ratio = 0.52f;
    app->layout_stacked_metadata_ratio = 0.21f;
    app->layout_active_splitter = DG_NUKLEAR_LAYOUT_SPLITTER_NONE;
    app->layout_hover_splitter = DG_NUKLEAR_LAYOUT_SPLITTER_NONE;

    (void)snprintf(app->seed_text, sizeof(app->seed_text), "1337");
    (void)snprintf(app->file_path, sizeof(app->file_path), "dungeon.dgmap");
    (void)snprintf(app->status_text, sizeof(app->status_text), "Ready.");
}

void dg_nuklear_app_shutdown(dg_nuklear_app_t *app)
{
    dg_nuklear_destroy_map(app);
    dg_nuklear_free_preview_image_buffer(app);
}

void dg_nuklear_app_draw(
    struct nk_context *ctx,
    dg_nuklear_app_t *app,
    int screen_width,
    int screen_height,
    const dg_nuklear_preview_renderer_t *preview_renderer
)
{
    const float margin = 10.0f;
    const float splitter_size = margin;
    int stacked_mode;
    int hovered_splitter;
    float left_width;
    float right_x;
    float right_width;
    float controls_height;
    float map_height;
    float metadata_height;
    float total_width;
    float total_height;
    float min_controls_width;
    float min_right_width;
    float min_map_height;
    float min_metadata_height;
    float min_controls_height;
    struct nk_rect side_vertical_splitter_rect;
    struct nk_rect side_horizontal_splitter_rect;
    struct nk_rect stacked_top_splitter_rect;
    struct nk_rect stacked_bottom_splitter_rect;
    struct nk_rect controls_rect;
    struct nk_rect map_rect;
    struct nk_rect metadata_rect;

    if (ctx == NULL || app == NULL || screen_width <= 0 || screen_height <= 0) {
        return;
    }

    dg_nuklear_apply_theme(ctx);

    total_width = (float)screen_width - (margin * 2.0f);
    total_height = (float)screen_height - (margin * 2.0f);
    if (total_width < 1.0f) {
        total_width = 1.0f;
    }
    if (total_height < 1.0f) {
        total_height = 1.0f;
    }

    stacked_mode = (screen_width < 980 || screen_height < 640) ? 1 : 0;
    if (stacked_mode) {
        if (app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_VERTICAL ||
            app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_HORIZONTAL) {
            app->layout_active_splitter = DG_NUKLEAR_LAYOUT_SPLITTER_NONE;
        }
    } else {
        if (app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_TOP ||
            app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_BOTTOM) {
            app->layout_active_splitter = DG_NUKLEAR_LAYOUT_SPLITTER_NONE;
        }
    }

    side_vertical_splitter_rect = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    side_horizontal_splitter_rect = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    stacked_top_splitter_rect = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
    stacked_bottom_splitter_rect = nk_rect(0.0f, 0.0f, 0.0f, 0.0f);

    min_controls_width = 280.0f;
    min_right_width = 240.0f;
    if (min_controls_width + min_right_width > total_width - splitter_size) {
        float width_scale;
        width_scale = (total_width - splitter_size) / (min_controls_width + min_right_width);
        if (width_scale < 0.15f) {
            width_scale = 0.15f;
        }
        min_controls_width *= width_scale;
        min_right_width *= width_scale;
    }

    min_map_height = 120.0f;
    min_metadata_height = 120.0f;
    min_controls_height = 140.0f;
    if (min_map_height + min_metadata_height > total_height - splitter_size) {
        float height_scale;
        height_scale = (total_height - splitter_size) / (min_map_height + min_metadata_height);
        if (height_scale < 0.20f) {
            height_scale = 0.20f;
        }
        min_map_height *= height_scale;
        min_metadata_height *= height_scale;
    }
    if (min_map_height + min_metadata_height + min_controls_height > total_height - splitter_size * 2.0f) {
        float stacked_scale;
        stacked_scale = (total_height - splitter_size * 2.0f) /
            (min_map_height + min_metadata_height + min_controls_height);
        if (stacked_scale < 0.20f) {
            stacked_scale = 0.20f;
        }
        min_map_height *= stacked_scale;
        min_metadata_height *= stacked_scale;
        min_controls_height *= stacked_scale;
    }

    if (stacked_mode) {
        float content_height;
        float controls_max;
        float metadata_max;

        content_height = (float)screen_height - margin * 4.0f;
        if (content_height < 1.0f) {
            content_height = 1.0f;
        }

        controls_height = app->layout_stacked_controls_ratio * content_height;
        metadata_height = app->layout_stacked_metadata_ratio * content_height;

        controls_max = content_height - min_map_height - min_metadata_height;
        if (controls_max < min_controls_height) {
            controls_max = min_controls_height;
        }
        controls_height = dg_nuklear_clamp_float(controls_height, min_controls_height, controls_max);

        metadata_max = content_height - controls_height - min_map_height;
        if (metadata_max < min_metadata_height) {
            metadata_max = min_metadata_height;
        }
        metadata_height = dg_nuklear_clamp_float(metadata_height, min_metadata_height, metadata_max);

        map_height = content_height - controls_height - metadata_height;
        if (map_height < min_map_height) {
            float deficit;
            float from_controls;
            float from_metadata;

            deficit = min_map_height - map_height;
            from_controls = dg_nuklear_clamp_float(controls_height - min_controls_height, 0.0f, deficit);
            controls_height -= from_controls;
            deficit -= from_controls;
            from_metadata = dg_nuklear_clamp_float(metadata_height - min_metadata_height, 0.0f, deficit);
            metadata_height -= from_metadata;
            map_height = content_height - controls_height - metadata_height;
        }

        if (app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_TOP &&
            nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
            controls_height += ctx->input.mouse.delta.y;
            controls_max = content_height - min_map_height - metadata_height;
            if (controls_max < min_controls_height) {
                controls_max = min_controls_height;
            }
            controls_height = dg_nuklear_clamp_float(controls_height, min_controls_height, controls_max);
            map_height = content_height - controls_height - metadata_height;
        } else if (app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_BOTTOM &&
            nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
            metadata_height -= ctx->input.mouse.delta.y;
            metadata_max = content_height - min_map_height - controls_height;
            if (metadata_max < min_metadata_height) {
                metadata_max = min_metadata_height;
            }
            metadata_height = dg_nuklear_clamp_float(metadata_height, min_metadata_height, metadata_max);
            map_height = content_height - controls_height - metadata_height;
        }

        if (content_height > 1.0f) {
            app->layout_stacked_controls_ratio = controls_height / content_height;
            app->layout_stacked_metadata_ratio = metadata_height / content_height;
        }

        controls_rect = nk_rect(
            margin,
            margin,
            total_width,
            controls_height
        );

        map_rect = nk_rect(
            margin,
            margin * 2.0f + controls_height,
            total_width,
            map_height
        );

        metadata_rect = nk_rect(
            margin,
            margin * 3.0f + controls_height + map_height,
            total_width,
            metadata_height
        );

        stacked_top_splitter_rect = nk_rect(
            margin,
            controls_rect.y + controls_rect.h,
            total_width,
            splitter_size
        );
        stacked_bottom_splitter_rect = nk_rect(
            margin,
            map_rect.y + map_rect.h,
            total_width,
            splitter_size
        );
    } else {
        float content_width;
        float content_height;
        float controls_max;
        float map_max;

        content_width = (float)screen_width - margin * 3.0f;
        content_height = (float)screen_height - margin * 3.0f;
        if (content_width < 1.0f) {
            content_width = 1.0f;
        }
        if (content_height < 1.0f) {
            content_height = 1.0f;
        }

        left_width = app->layout_side_left_ratio * content_width;
        controls_max = content_width - min_right_width;
        if (controls_max < min_controls_width) {
            controls_max = min_controls_width;
        }
        left_width = dg_nuklear_clamp_float(left_width, min_controls_width, controls_max);

        right_width = content_width - left_width;
        if (right_width < min_right_width) {
            right_width = min_right_width;
            left_width = content_width - right_width;
            if (left_width < min_controls_width) {
                left_width = min_controls_width;
                right_width = content_width - left_width;
            }
        }

        metadata_height = app->layout_side_map_ratio * content_height;
        map_max = content_height - min_metadata_height;
        if (map_max < min_map_height) {
            map_max = min_map_height;
        }
        map_height = dg_nuklear_clamp_float(metadata_height, min_map_height, map_max);
        metadata_height = content_height - map_height;

        if (app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_VERTICAL &&
            nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
            left_width += ctx->input.mouse.delta.x;
            controls_max = content_width - min_right_width;
            if (controls_max < min_controls_width) {
                controls_max = min_controls_width;
            }
            left_width = dg_nuklear_clamp_float(left_width, min_controls_width, controls_max);
            right_width = content_width - left_width;
        } else if (app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_HORIZONTAL &&
            nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
            map_height += ctx->input.mouse.delta.y;
            map_max = content_height - min_metadata_height;
            if (map_max < min_map_height) {
                map_max = min_map_height;
            }
            map_height = dg_nuklear_clamp_float(map_height, min_map_height, map_max);
            metadata_height = content_height - map_height;
        }

        right_x = margin + left_width + margin;

        app->layout_side_left_ratio = left_width / content_width;
        app->layout_side_map_ratio = map_height / content_height;

        controls_rect = nk_rect(
            margin,
            margin,
            left_width,
            total_height
        );
        map_rect = nk_rect(right_x, margin, right_width, map_height);
        metadata_rect = nk_rect(
            right_x,
            margin * 2.0f + map_height,
            right_width,
            metadata_height
        );

        side_vertical_splitter_rect = nk_rect(
            controls_rect.x + controls_rect.w,
            margin,
            splitter_size,
            (float)screen_height - margin * 2.0f
        );
        side_horizontal_splitter_rect = nk_rect(
            right_x,
            map_rect.y + map_rect.h,
            right_width,
            splitter_size
        );
    }

    hovered_splitter = DG_NUKLEAR_LAYOUT_SPLITTER_NONE;
    if (stacked_mode) {
        if (nk_input_is_mouse_hovering_rect(&ctx->input, stacked_top_splitter_rect)) {
            hovered_splitter = DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_TOP;
        } else if (nk_input_is_mouse_hovering_rect(&ctx->input, stacked_bottom_splitter_rect)) {
            hovered_splitter = DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_BOTTOM;
        }
    } else {
        if (nk_input_is_mouse_hovering_rect(&ctx->input, side_vertical_splitter_rect)) {
            hovered_splitter = DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_VERTICAL;
        } else if (nk_input_is_mouse_hovering_rect(&ctx->input, side_horizontal_splitter_rect)) {
            hovered_splitter = DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_HORIZONTAL;
        }
    }

    if (!nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
        app->layout_active_splitter = DG_NUKLEAR_LAYOUT_SPLITTER_NONE;
    } else if (app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_NONE &&
        nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
        app->layout_active_splitter = hovered_splitter;
    }

    app->layout_hover_splitter = hovered_splitter;
    if (app->layout_active_splitter != DG_NUKLEAR_LAYOUT_SPLITTER_NONE) {
        app->layout_hover_splitter = app->layout_active_splitter;
    }

    if (nk_begin(
            ctx,
            "Configuration",
            controls_rect,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE
        )) {
        dg_nuklear_draw_controls(ctx, app);
    }
    nk_end(ctx);

    if (nk_begin(
            ctx,
            "Preview",
            map_rect,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR
        )) {
        dg_nuklear_draw_map(ctx, app, map_rect.h - 20.0f, preview_renderer);
    }
    nk_end(ctx);

    if (nk_begin(
            ctx,
            "Metadata",
            metadata_rect,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE
        )) {
        dg_nuklear_draw_metadata(ctx, app);
    }
    nk_end(ctx);

    if (stacked_mode) {
        dg_nuklear_draw_splitter_overlay(
            ctx,
            "__dg_splitter_stacked_top",
            stacked_top_splitter_rect,
            0,
            app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_TOP,
            app->layout_hover_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_TOP
        );
        dg_nuklear_draw_splitter_overlay(
            ctx,
            "__dg_splitter_stacked_bottom",
            stacked_bottom_splitter_rect,
            0,
            app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_BOTTOM,
            app->layout_hover_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_STACKED_BOTTOM
        );
    } else {
        dg_nuklear_draw_splitter_overlay(
            ctx,
            "__dg_splitter_side_vertical",
            side_vertical_splitter_rect,
            1,
            app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_VERTICAL,
            app->layout_hover_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_VERTICAL
        );
        dg_nuklear_draw_splitter_overlay(
            ctx,
            "__dg_splitter_side_horizontal",
            side_horizontal_splitter_rect,
            0,
            app->layout_active_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_HORIZONTAL,
            app->layout_hover_splitter == DG_NUKLEAR_LAYOUT_SPLITTER_SIDE_HORIZONTAL
        );
    }
}
