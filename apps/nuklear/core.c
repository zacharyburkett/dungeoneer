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

static const dg_algorithm_t DG_NUKLEAR_ALGORITHMS[] = {
    DG_ALGORITHM_BSP_TREE,
    DG_ALGORITHM_ROOMS_AND_MAZES,
    DG_ALGORITHM_DRUNKARDS_WALK,
    DG_ALGORITHM_CELLULAR_AUTOMATA,
    DG_ALGORITHM_VALUE_NOISE
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
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        return "Rooms + Mazes";
    case DG_ALGORITHM_DRUNKARDS_WALK:
        return "Drunkard's Walk";
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        return "Cellular Automata";
    case DG_ALGORITHM_VALUE_NOISE:
        return "Value Noise";
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

static const char *dg_nuklear_process_method_label(dg_process_method_type_t type)
{
    switch (type) {
    case DG_PROCESS_METHOD_SCALE:
        return "Scale";
    case DG_PROCESS_METHOD_ROOM_SHAPE:
        return "Room Shape";
    case DG_PROCESS_METHOD_PATH_SMOOTH:
        return "Path Smoothing";
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
    case 0:
    default:
        return DG_PROCESS_METHOD_SCALE;
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
            method->params.room_shape.mode != DG_ROOM_SHAPE_ORGANIC) {
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
        dg_nuklear_clamp_int(app->process_add_method_type_index, 0, 2);

    for (i = 0; i < app->process_method_count; ++i) {
        dg_nuklear_sanitize_process_method(&app->process_methods[i]);
    }
}

static void dg_nuklear_reset_process_defaults(dg_nuklear_app_t *app)
{
    if (app == NULL) {
        return;
    }

    app->process_method_count = 0;
    app->process_add_method_type_index = 0;
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
    return true;
}

static bool dg_nuklear_algorithm_supports_room_types(dg_algorithm_t algorithm)
{
    return algorithm == DG_ALGORITHM_BSP_TREE || algorithm == DG_ALGORITHM_ROOMS_AND_MAZES;
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
    } else if (algorithm == DG_ALGORITHM_CELLULAR_AUTOMATA) {
        app->cellular_automata_config = defaults.params.cellular_automata;
    } else if (algorithm == DG_ALGORITHM_VALUE_NOISE) {
        app->value_noise_config = defaults.params.value_noise;
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
    } else if (algorithm == DG_ALGORITHM_CELLULAR_AUTOMATA) {
        request.params.cellular_automata = app->cellular_automata_config;
    } else if (algorithm == DG_ALGORITHM_VALUE_NOISE) {
        request.params.value_noise = app->value_noise_config;
    } else if (algorithm == DG_ALGORITHM_ROOMS_AND_MAZES) {
        request.params.rooms_and_mazes = app->rooms_and_mazes_config;
    } else {
        request.params.bsp = app->bsp_config;
    }
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
    } else {
        dg_nuklear_reset_algorithm_defaults(
            app,
            dg_nuklear_algorithm_from_index(app->algorithm_index)
        );
        dg_nuklear_sync_generation_class_with_algorithm(app);
        dg_nuklear_reset_process_defaults(app);
        dg_nuklear_reset_room_type_defaults(app);
        dg_nuklear_set_status(app, "Loaded map from %s", app->file_path);
    }
}

static void dg_nuklear_draw_map(
    struct nk_context *ctx,
    dg_nuklear_app_t *app,
    float suggested_height
)
{
    struct nk_rect preview_bounds;
    struct nk_rect preview_content_bounds;
    enum nk_widget_layout_states widget_state;
    struct nk_command_buffer *canvas;
    struct nk_rect old_clip;
    struct nk_rect draw_clip;

    if (ctx == NULL || app == NULL) {
        return;
    }

    if (suggested_height < 120.0f) {
        suggested_height = 120.0f;
    }

    if (app->has_map) {
        nk_layout_row_dynamic(ctx, 28.0f, 3);
        nk_label(ctx, "Zoom", NK_TEXT_LEFT);
        nk_property_float(ctx, "x", 0.10f, &app->preview_zoom, 24.0f, 0.10f, 0.01f);
        if (nk_button_label(ctx, "Fit")) {
            dg_nuklear_reset_preview_camera(app);
        }
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

    if (app->has_map && app->map.tiles != NULL && app->map.width > 0 && app->map.height > 0) {
        int x_start;
        int x_end;
        int y_start;
        int y_end;
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
            if (hovered && scroll_delta != 0.0f) {
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

            if (hovered && nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
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

            if (x_end > x_start && y_end > y_start) {
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

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Metadata", NK_TEXT_LEFT);

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

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Generation Type", NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    app->generation_class_index = nk_combo(
        ctx,
        generation_classes,
        (int)(sizeof(generation_classes) / sizeof(generation_classes[0])),
        app->generation_class_index,
        8,
        nk_vec2(220.0f, 80.0f)
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

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Algorithm", NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 28.0f, 1);
    selected_filtered_index = nk_combo(
        ctx,
        algorithm_labels,
        filtered_count,
        selected_filtered_index,
        10,
        nk_vec2(280, 120)
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

static void dg_nuklear_draw_process_settings(
    struct nk_context *ctx,
    dg_nuklear_app_t *app,
    dg_algorithm_t algorithm
)
{
    static const char *process_method_types[] = {"Scale", "Room Shape", "Path Smoothing"};
    static const char *room_shape_modes[] = {"Rectangular", "Organic"};
    int i;
    int pending_remove_index;

    if (ctx == NULL || app == NULL) {
        return;
    }

    dg_nuklear_sanitize_process_settings(app);
    pending_remove_index = -1;

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    {
        char line[96];
        (void)snprintf(
            line,
            sizeof(line),
            "Pipeline Steps: %d / %d",
            app->process_method_count,
            DG_NUKLEAR_MAX_PROCESS_METHODS
        );
        nk_label(ctx, line, NK_TEXT_LEFT);
    }

    nk_layout_row_dynamic(ctx, 28.0f, 2);
    app->process_add_method_type_index = nk_combo(
        ctx,
        process_method_types,
        (int)(sizeof(process_method_types) / sizeof(process_method_types[0])),
        app->process_add_method_type_index,
        22,
        nk_vec2(220.0f, 80.0f)
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

    nk_layout_row_dynamic(ctx, 30.0f, 2);
    if (nk_button_label(ctx, "Remove Last")) {
        if (dg_nuklear_remove_process_method(app, app->process_method_count - 1)) {
            dg_nuklear_set_status(app, "Removed last process step.");
        } else {
            dg_nuklear_set_status(app, "No process steps to remove.");
        }
    }
    if (nk_button_label(ctx, "Reset Process Defaults")) {
        dg_nuklear_reset_process_defaults(app);
        dg_nuklear_set_status(app, "Process defaults restored.");
    }

    if (app->process_method_count == 0) {
        nk_layout_row_dynamic(ctx, 36.0f, 1);
        nk_label_wrap(
            ctx,
            "No post-process steps configured. Add steps above to define a custom process pipeline."
        );
        return;
    }

    for (i = 0; i < app->process_method_count; ++i) {
        dg_process_method_t *method = &app->process_methods[i];
        char step_title[96];
        const char *method_name;
        int type_index;

        method_name = dg_nuklear_process_method_label(method->type);
        (void)snprintf(step_title, sizeof(step_title), "%d. %s", i + 1, method_name);

        if (nk_tree_push_id(ctx, NK_TREE_TAB, step_title, NK_MINIMIZED, 500 + i)) {
            nk_layout_row_dynamic(ctx, 28.0f, 4);
            if (nk_button_label(ctx, "Up")) {
                if (dg_nuklear_move_process_method(app, i, -1)) {
                    dg_nuklear_set_status(app, "Moved step %d up.", i + 1);
                }
            }
            if (nk_button_label(ctx, "Down")) {
                if (dg_nuklear_move_process_method(app, i, 1)) {
                    dg_nuklear_set_status(app, "Moved step %d down.", i + 1);
                }
            }
            if (nk_button_label(ctx, "Remove")) {
                pending_remove_index = i;
            }
            nk_label(ctx, " ", NK_TEXT_LEFT);

            type_index = dg_nuklear_process_type_to_ui_index(method->type);
            nk_layout_row_dynamic(ctx, 24.0f, 1);
            nk_label(ctx, "Type", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 28.0f, 1);
            type_index = nk_combo(
                ctx,
                process_method_types,
                (int)(sizeof(process_method_types) / sizeof(process_method_types[0])),
                type_index,
                22,
                nk_vec2(220.0f, 80.0f)
            );
            if (type_index < 0 || type_index >= (int)(sizeof(process_method_types) / sizeof(process_method_types[0]))) {
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
                    (method->params.room_shape.mode == DG_ROOM_SHAPE_ORGANIC) ? 1 : 0;
                nk_layout_row_dynamic(ctx, 24.0f, 1);
                nk_label(ctx, "Room Shape Mode", NK_TEXT_LEFT);
                nk_layout_row_dynamic(ctx, 28.0f, 1);
                room_shape_index = nk_combo(
                    ctx,
                    room_shape_modes,
                    (int)(sizeof(room_shape_modes) / sizeof(room_shape_modes[0])),
                    room_shape_index,
                    22,
                    nk_vec2(220.0f, 80.0f)
                );
                method->params.room_shape.mode =
                    (room_shape_index == 1) ? DG_ROOM_SHAPE_ORGANIC : DG_ROOM_SHAPE_RECTANGULAR;

                if (method->params.room_shape.mode == DG_ROOM_SHAPE_ORGANIC) {
                    nk_layout_row_dynamic(ctx, 28.0f, 1);
                    nk_property_int(
                        ctx,
                        "Organicity (%)",
                        0,
                        &method->params.room_shape.organicity,
                        100,
                        1,
                        0.25f
                    );
                }
                if (method->params.room_shape.mode == DG_ROOM_SHAPE_ORGANIC &&
                    dg_algorithm_generation_class(algorithm) != DG_MAP_GENERATION_CLASS_ROOM_LIKE) {
                    nk_layout_row_dynamic(ctx, 36.0f, 1);
                    nk_label_wrap(
                        ctx,
                        "Organic room shape has no effect for cave-like layouts."
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
                    "Inner fills bend corners; outer trims matching outer corners while preserving connectivity. In multi-step pipelines, run Inner before Outer."
                );
            }

            dg_nuklear_sanitize_process_method(method);
            nk_tree_pop(ctx);
        }
    }

    if (pending_remove_index >= 0) {
        if (dg_nuklear_remove_process_method(app, pending_remove_index)) {
            dg_nuklear_set_status(app, "Removed step %d.", pending_remove_index + 1);
        }
    }
}

static void dg_nuklear_draw_room_type_slot(struct nk_context *ctx, dg_nuklear_room_type_ui_t *slot)
{
    struct nk_color preview_color;

    if (ctx == NULL || slot == NULL) {
        return;
    }

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

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Quotas", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_property_int(ctx, "Min Count", 0, &slot->min_count, INT_MAX, 1, 0.25f);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_property_int(ctx, "Max Count (-1=Any)", -1, &slot->max_count, INT_MAX, 1, 0.25f);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_property_int(ctx, "Target Count (-1=None)", -1, &slot->target_count, INT_MAX, 1, 0.25f);

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Constraints", NK_TEXT_LEFT);
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

    nk_layout_row_dynamic(ctx, 20.0f, 1);
    nk_label(ctx, "Preferences", NK_TEXT_LEFT);
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

            dg_nuklear_draw_room_type_slot(ctx, &app->room_type_slots[i]);
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

    if (nk_tree_push(ctx, NK_TREE_TAB, "Layout", NK_MAXIMIZED)) {
        dg_nuklear_draw_generation_settings(ctx, app);
        algorithm = dg_nuklear_algorithm_from_index(app->algorithm_index);
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "Layout Parameters", NK_TEXT_LEFT);

        if (algorithm == DG_ALGORITHM_DRUNKARDS_WALK) {
            dg_nuklear_draw_drunkards_settings(ctx, app);
        } else if (algorithm == DG_ALGORITHM_CELLULAR_AUTOMATA) {
            dg_nuklear_draw_cellular_automata_settings(ctx, app);
        } else if (algorithm == DG_ALGORITHM_VALUE_NOISE) {
            dg_nuklear_draw_value_noise_settings(ctx, app);
        } else if (algorithm == DG_ALGORITHM_ROOMS_AND_MAZES) {
            dg_nuklear_draw_rooms_and_mazes_settings(ctx, app);
        } else {
            dg_nuklear_draw_bsp_settings(ctx, app);
        }
        nk_tree_pop(ctx);
    }

    if (nk_tree_push(ctx, NK_TREE_TAB, "Process", NK_MINIMIZED)) {
        dg_nuklear_draw_process_settings(ctx, app, algorithm);
        nk_tree_pop(ctx);
    }

    if (nk_tree_push(ctx, NK_TREE_TAB, "Room Type Assignment", NK_MINIMIZED)) {
        dg_nuklear_draw_room_type_settings(ctx, app, algorithm);
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
    app->generation_class_index = 0;
    app->algorithm_index = 0;
    app->width = 80;
    app->height = 40;

    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_BSP_TREE);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_DRUNKARDS_WALK);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_CELLULAR_AUTOMATA);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_VALUE_NOISE);
    dg_nuklear_reset_algorithm_defaults(app, DG_ALGORITHM_ROOMS_AND_MAZES);
    dg_nuklear_reset_process_defaults(app);
    dg_nuklear_reset_room_type_defaults(app);
    dg_nuklear_reset_preview_camera(app);
    dg_nuklear_sync_generation_class_with_algorithm(app);

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
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR
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
