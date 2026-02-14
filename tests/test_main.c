#include "dungeoneer/dungeoneer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(condition)                                                      \
    do {                                                                            \
        if (!(condition)) {                                                         \
            fprintf(stderr, "Assertion failed at %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                                    \
            return 1;                                                               \
        }                                                                           \
    } while (0)

#define ASSERT_STATUS(actual, expected)                                             \
    do {                                                                            \
        dg_status_t status_result = (actual);                                       \
        if (status_result != (expected)) {                                          \
            fprintf(stderr, "Unexpected status at %s:%d: got %s expected %s\n",    \
                    __FILE__, __LINE__, dg_status_string(status_result),            \
                    dg_status_string((expected)));                                  \
            return 1;                                                               \
        }                                                                           \
    } while (0)

static bool is_walkable(dg_tile_t tile)
{
    return tile == DG_TILE_FLOOR || tile == DG_TILE_DOOR;
}

static size_t count_walkable_tiles(const dg_map_t *map)
{
    size_t i;
    size_t count;
    size_t cell_count;

    count = 0;
    cell_count = (size_t)map->width * (size_t)map->height;
    for (i = 0; i < cell_count; ++i) {
        if (is_walkable(map->tiles[i])) {
            count += 1;
        }
    }

    return count;
}

static size_t count_door_tiles(const dg_map_t *map)
{
    size_t i;
    size_t count;
    size_t cell_count;

    count = 0;
    cell_count = (size_t)map->width * (size_t)map->height;
    for (i = 0; i < cell_count; ++i) {
        if (map->tiles[i] == DG_TILE_DOOR) {
            count += 1;
        }
    }

    return count;
}

static bool maps_have_same_tiles(const dg_map_t *a, const dg_map_t *b)
{
    size_t cell_count;

    if (a == NULL || b == NULL || a->tiles == NULL || b->tiles == NULL) {
        return false;
    }

    if (a->width != b->width || a->height != b->height) {
        return false;
    }

    cell_count = (size_t)a->width * (size_t)a->height;
    return memcmp(a->tiles, b->tiles, cell_count * sizeof(dg_tile_t)) == 0;
}

static bool point_in_any_room(const dg_map_t *map, int x, int y)
{
    size_t i;

    if (map == NULL || map->metadata.rooms == NULL || x < 0 || y < 0 ||
        x >= map->width || y >= map->height) {
        return false;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_rect_t *room = &map->metadata.rooms[i].bounds;
        if (x >= room->x && y >= room->y &&
            x < room->x + room->width && y < room->y + room->height) {
            return true;
        }
    }

    return false;
}

static bool corridor_floor(const dg_map_t *map, int x, int y)
{
    size_t index;

    if (map == NULL || map->tiles == NULL || x < 0 || y < 0 ||
        x >= map->width || y >= map->height) {
        return false;
    }

    index = (size_t)y * (size_t)map->width + (size_t)x;
    return is_walkable(map->tiles[index]) && !point_in_any_room(map, x, y);
}

static bool corridor_touches_room(const dg_map_t *map, int x, int y)
{
    static const int k_dirs[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    int d;

    if (!corridor_floor(map, x, y)) {
        return false;
    }

    for (d = 0; d < 4; ++d) {
        int nx = x + k_dirs[d][0];
        int ny = y + k_dirs[d][1];
        size_t nindex;

        if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) {
            continue;
        }

        nindex = (size_t)ny * (size_t)map->width + (size_t)nx;
        if (point_in_any_room(map, nx, ny) && is_walkable(map->tiles[nindex])) {
            return true;
        }
    }

    return false;
}

static bool maps_have_same_generation_request_snapshot(const dg_map_t *a, const dg_map_t *b)
{
    const dg_generation_request_snapshot_t *sa;
    const dg_generation_request_snapshot_t *sb;
    size_t i;

    if (a == NULL || b == NULL) {
        return false;
    }

    sa = &a->metadata.generation_request;
    sb = &b->metadata.generation_request;
    if (sa->present != sb->present) {
        return false;
    }
    if (sa->present == 0) {
        return true;
    }

    if (sa->width != sb->width ||
        sa->height != sb->height ||
        sa->seed != sb->seed ||
        sa->algorithm_id != sb->algorithm_id ||
        sa->edge_openings.opening_count != sb->edge_openings.opening_count ||
        sa->process.enabled != sb->process.enabled ||
        sa->process.method_count != sb->process.method_count ||
        sa->room_types.definition_count != sb->room_types.definition_count ||
        sa->room_types.policy.strict_mode != sb->room_types.policy.strict_mode ||
        sa->room_types.policy.allow_untyped_rooms != sb->room_types.policy.allow_untyped_rooms ||
        sa->room_types.policy.default_type_id != sb->room_types.policy.default_type_id) {
        return false;
    }

    switch ((dg_algorithm_t)sa->algorithm_id) {
    case DG_ALGORITHM_BSP_TREE:
        if (sa->params.bsp.min_rooms != sb->params.bsp.min_rooms ||
            sa->params.bsp.max_rooms != sb->params.bsp.max_rooms ||
            sa->params.bsp.room_min_size != sb->params.bsp.room_min_size ||
            sa->params.bsp.room_max_size != sb->params.bsp.room_max_size) {
            return false;
        }
        break;
    case DG_ALGORITHM_DRUNKARDS_WALK:
        if (sa->params.drunkards_walk.wiggle_percent !=
            sb->params.drunkards_walk.wiggle_percent) {
            return false;
        }
        break;
    case DG_ALGORITHM_CELLULAR_AUTOMATA:
        if (sa->params.cellular_automata.initial_wall_percent !=
                sb->params.cellular_automata.initial_wall_percent ||
            sa->params.cellular_automata.simulation_steps !=
                sb->params.cellular_automata.simulation_steps ||
            sa->params.cellular_automata.wall_threshold !=
                sb->params.cellular_automata.wall_threshold) {
            return false;
        }
        break;
    case DG_ALGORITHM_VALUE_NOISE:
        if (sa->params.value_noise.feature_size != sb->params.value_noise.feature_size ||
            sa->params.value_noise.octaves != sb->params.value_noise.octaves ||
            sa->params.value_noise.persistence_percent !=
                sb->params.value_noise.persistence_percent ||
            sa->params.value_noise.floor_threshold_percent !=
                sb->params.value_noise.floor_threshold_percent) {
            return false;
        }
        break;
    case DG_ALGORITHM_ROOMS_AND_MAZES:
        if (sa->params.rooms_and_mazes.min_rooms != sb->params.rooms_and_mazes.min_rooms ||
            sa->params.rooms_and_mazes.max_rooms != sb->params.rooms_and_mazes.max_rooms ||
            sa->params.rooms_and_mazes.room_min_size != sb->params.rooms_and_mazes.room_min_size ||
            sa->params.rooms_and_mazes.room_max_size != sb->params.rooms_and_mazes.room_max_size ||
            sa->params.rooms_and_mazes.maze_wiggle_percent !=
                sb->params.rooms_and_mazes.maze_wiggle_percent ||
            sa->params.rooms_and_mazes.min_room_connections !=
                sb->params.rooms_and_mazes.min_room_connections ||
            sa->params.rooms_and_mazes.max_room_connections !=
                sb->params.rooms_and_mazes.max_room_connections ||
            sa->params.rooms_and_mazes.ensure_full_connectivity !=
                sb->params.rooms_and_mazes.ensure_full_connectivity ||
            sa->params.rooms_and_mazes.dead_end_prune_steps !=
                sb->params.rooms_and_mazes.dead_end_prune_steps) {
            return false;
        }
        break;
    case DG_ALGORITHM_ROOM_GRAPH:
        if (sa->params.room_graph.min_rooms != sb->params.room_graph.min_rooms ||
            sa->params.room_graph.max_rooms != sb->params.room_graph.max_rooms ||
            sa->params.room_graph.room_min_size != sb->params.room_graph.room_min_size ||
            sa->params.room_graph.room_max_size != sb->params.room_graph.room_max_size ||
            sa->params.room_graph.neighbor_candidates !=
                sb->params.room_graph.neighbor_candidates ||
            sa->params.room_graph.extra_connection_chance_percent !=
                sb->params.room_graph.extra_connection_chance_percent) {
            return false;
        }
        break;
    case DG_ALGORITHM_WORM_CAVES:
        if (sa->params.worm_caves.worm_count != sb->params.worm_caves.worm_count ||
            sa->params.worm_caves.wiggle_percent != sb->params.worm_caves.wiggle_percent ||
            sa->params.worm_caves.branch_chance_percent !=
                sb->params.worm_caves.branch_chance_percent ||
            sa->params.worm_caves.target_floor_percent !=
                sb->params.worm_caves.target_floor_percent ||
            sa->params.worm_caves.brush_radius != sb->params.worm_caves.brush_radius ||
            sa->params.worm_caves.max_steps_per_worm !=
                sb->params.worm_caves.max_steps_per_worm ||
            sa->params.worm_caves.ensure_connected !=
                sb->params.worm_caves.ensure_connected) {
            return false;
        }
        break;
    case DG_ALGORITHM_SIMPLEX_NOISE:
        if (sa->params.simplex_noise.feature_size != sb->params.simplex_noise.feature_size ||
            sa->params.simplex_noise.octaves != sb->params.simplex_noise.octaves ||
            sa->params.simplex_noise.persistence_percent !=
                sb->params.simplex_noise.persistence_percent ||
            sa->params.simplex_noise.floor_threshold_percent !=
                sb->params.simplex_noise.floor_threshold_percent ||
            sa->params.simplex_noise.ensure_connected !=
                sb->params.simplex_noise.ensure_connected) {
            return false;
        }
        break;
    default:
        return false;
    }

    if ((sa->process.method_count > 0 &&
         (sa->process.methods == NULL || sb->process.methods == NULL))) {
        return false;
    }

    if ((sa->edge_openings.opening_count > 0u &&
         (sa->edge_openings.openings == NULL || sb->edge_openings.openings == NULL))) {
        return false;
    }

    for (i = 0; i < sa->edge_openings.opening_count; ++i) {
        const dg_snapshot_edge_opening_spec_t *oa = &sa->edge_openings.openings[i];
        const dg_snapshot_edge_opening_spec_t *ob = &sb->edge_openings.openings[i];
        if (oa->side != ob->side ||
            oa->start != ob->start ||
            oa->end != ob->end ||
            oa->role != ob->role) {
            return false;
        }
    }

    for (i = 0; i < sa->process.method_count; ++i) {
        const dg_snapshot_process_method_t *ma = &sa->process.methods[i];
        const dg_snapshot_process_method_t *mb = &sb->process.methods[i];

        if (ma->type != mb->type) {
            return false;
        }

        switch ((dg_process_method_type_t)ma->type) {
        case DG_PROCESS_METHOD_SCALE:
            if (ma->params.scale.factor != mb->params.scale.factor) {
                return false;
            }
            break;
        case DG_PROCESS_METHOD_PATH_SMOOTH:
            if (ma->params.path_smooth.strength != mb->params.path_smooth.strength ||
                ma->params.path_smooth.inner_enabled != mb->params.path_smooth.inner_enabled ||
                ma->params.path_smooth.outer_enabled != mb->params.path_smooth.outer_enabled) {
                return false;
            }
            break;
        case DG_PROCESS_METHOD_CORRIDOR_ROUGHEN:
            if (ma->params.corridor_roughen.strength != mb->params.corridor_roughen.strength ||
                ma->params.corridor_roughen.max_depth != mb->params.corridor_roughen.max_depth ||
                ma->params.corridor_roughen.mode != mb->params.corridor_roughen.mode) {
                return false;
            }
            break;
        default:
            return false;
        }
    }

    if ((sa->room_types.definition_count > 0 &&
         (sa->room_types.definitions == NULL || sb->room_types.definitions == NULL))) {
        return false;
    }

    for (i = 0; i < sa->room_types.definition_count; ++i) {
        const dg_snapshot_room_type_definition_t *da = &sa->room_types.definitions[i];
        const dg_snapshot_room_type_definition_t *db = &sb->room_types.definitions[i];

        if (da->type_id != db->type_id ||
            da->enabled != db->enabled ||
            da->min_count != db->min_count ||
            da->max_count != db->max_count ||
            da->target_count != db->target_count ||
            strcmp(da->template_map_path, db->template_map_path) != 0 ||
            da->template_opening_query.side_mask != db->template_opening_query.side_mask ||
            da->template_opening_query.role_mask != db->template_opening_query.role_mask ||
            da->template_opening_query.edge_coord_min !=
                db->template_opening_query.edge_coord_min ||
            da->template_opening_query.edge_coord_max !=
                db->template_opening_query.edge_coord_max ||
            da->template_opening_query.min_length != db->template_opening_query.min_length ||
            da->template_opening_query.max_length != db->template_opening_query.max_length ||
            da->template_opening_query.require_component !=
                db->template_opening_query.require_component ||
            da->template_required_opening_matches != db->template_required_opening_matches ||
            da->constraints.area_min != db->constraints.area_min ||
            da->constraints.area_max != db->constraints.area_max ||
            da->constraints.degree_min != db->constraints.degree_min ||
            da->constraints.degree_max != db->constraints.degree_max ||
            da->constraints.border_distance_min != db->constraints.border_distance_min ||
            da->constraints.border_distance_max != db->constraints.border_distance_max ||
            da->constraints.graph_depth_min != db->constraints.graph_depth_min ||
            da->constraints.graph_depth_max != db->constraints.graph_depth_max ||
            da->preferences.weight != db->preferences.weight ||
            da->preferences.larger_room_bias != db->preferences.larger_room_bias ||
            da->preferences.higher_degree_bias != db->preferences.higher_degree_bias ||
            da->preferences.border_distance_bias != db->preferences.border_distance_bias) {
            return false;
        }
    }

    return true;
}

static bool maps_have_same_metadata(const dg_map_t *a, const dg_map_t *b)
{
    size_t i;

    if (a == NULL || b == NULL) {
        return false;
    }

    if (a->metadata.seed != b->metadata.seed ||
        a->metadata.algorithm_id != b->metadata.algorithm_id ||
        a->metadata.generation_class != b->metadata.generation_class ||
        a->metadata.room_count != b->metadata.room_count ||
        a->metadata.corridor_count != b->metadata.corridor_count ||
        a->metadata.room_entrance_count != b->metadata.room_entrance_count ||
        a->metadata.edge_opening_count != b->metadata.edge_opening_count ||
        a->metadata.primary_entrance_opening_id != b->metadata.primary_entrance_opening_id ||
        a->metadata.primary_exit_opening_id != b->metadata.primary_exit_opening_id ||
        a->metadata.room_adjacency_count != b->metadata.room_adjacency_count ||
        a->metadata.room_neighbor_count != b->metadata.room_neighbor_count ||
        a->metadata.walkable_tile_count != b->metadata.walkable_tile_count ||
        a->metadata.wall_tile_count != b->metadata.wall_tile_count ||
        a->metadata.special_room_count != b->metadata.special_room_count ||
        a->metadata.entrance_room_count != b->metadata.entrance_room_count ||
        a->metadata.exit_room_count != b->metadata.exit_room_count ||
        a->metadata.boss_room_count != b->metadata.boss_room_count ||
        a->metadata.treasure_room_count != b->metadata.treasure_room_count ||
        a->metadata.shop_room_count != b->metadata.shop_room_count ||
        a->metadata.leaf_room_count != b->metadata.leaf_room_count ||
        a->metadata.corridor_total_length != b->metadata.corridor_total_length ||
        a->metadata.entrance_exit_distance != b->metadata.entrance_exit_distance ||
        a->metadata.connected_component_count != b->metadata.connected_component_count ||
        a->metadata.largest_component_size != b->metadata.largest_component_size ||
        a->metadata.connected_floor != b->metadata.connected_floor ||
        a->metadata.generation_attempts != b->metadata.generation_attempts ||
        a->metadata.diagnostics.process_step_count != b->metadata.diagnostics.process_step_count ||
        a->metadata.diagnostics.typed_room_count != b->metadata.diagnostics.typed_room_count ||
        a->metadata.diagnostics.untyped_room_count != b->metadata.diagnostics.untyped_room_count ||
        a->metadata.diagnostics.room_type_count != b->metadata.diagnostics.room_type_count ||
        a->metadata.diagnostics.room_type_min_miss_count !=
            b->metadata.diagnostics.room_type_min_miss_count ||
        a->metadata.diagnostics.room_type_max_excess_count !=
            b->metadata.diagnostics.room_type_max_excess_count ||
        a->metadata.diagnostics.room_type_target_miss_count !=
            b->metadata.diagnostics.room_type_target_miss_count) {
        return false;
    }

    if ((a->metadata.room_count > 0 && (a->metadata.rooms == NULL || b->metadata.rooms == NULL)) ||
        (a->metadata.corridor_count > 0 &&
         (a->metadata.corridors == NULL || b->metadata.corridors == NULL)) ||
        (a->metadata.room_entrance_count > 0 &&
         (a->metadata.room_entrances == NULL || b->metadata.room_entrances == NULL)) ||
        (a->metadata.edge_opening_count > 0 &&
         (a->metadata.edge_openings == NULL || b->metadata.edge_openings == NULL)) ||
        (a->metadata.room_adjacency_count > 0 &&
         (a->metadata.room_adjacency == NULL || b->metadata.room_adjacency == NULL)) ||
        (a->metadata.room_neighbor_count > 0 &&
         (a->metadata.room_neighbors == NULL || b->metadata.room_neighbors == NULL)) ||
        (a->metadata.diagnostics.process_step_count > 0 &&
         (a->metadata.diagnostics.process_steps == NULL ||
          b->metadata.diagnostics.process_steps == NULL)) ||
        (a->metadata.diagnostics.room_type_count > 0 &&
         (a->metadata.diagnostics.room_type_quotas == NULL ||
          b->metadata.diagnostics.room_type_quotas == NULL))) {
        return false;
    }

    for (i = 0; i < a->metadata.room_count; ++i) {
        const dg_room_metadata_t *ra = &a->metadata.rooms[i];
        const dg_room_metadata_t *rb = &b->metadata.rooms[i];
        if (ra->id != rb->id ||
            ra->bounds.x != rb->bounds.x ||
            ra->bounds.y != rb->bounds.y ||
            ra->bounds.width != rb->bounds.width ||
            ra->bounds.height != rb->bounds.height ||
            ra->flags != rb->flags ||
            ra->role != rb->role ||
            ra->type_id != rb->type_id) {
            return false;
        }
    }

    for (i = 0; i < a->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *ca = &a->metadata.corridors[i];
        const dg_corridor_metadata_t *cb = &b->metadata.corridors[i];
        if (ca->from_room_id != cb->from_room_id ||
            ca->to_room_id != cb->to_room_id ||
            ca->width != cb->width ||
            ca->length != cb->length) {
            return false;
        }
    }

    for (i = 0; i < a->metadata.room_entrance_count; ++i) {
        const dg_room_entrance_metadata_t *ea = &a->metadata.room_entrances[i];
        const dg_room_entrance_metadata_t *eb = &b->metadata.room_entrances[i];
        if (ea->room_id != eb->room_id ||
            ea->room_tile.x != eb->room_tile.x ||
            ea->room_tile.y != eb->room_tile.y ||
            ea->corridor_tile.x != eb->corridor_tile.x ||
            ea->corridor_tile.y != eb->corridor_tile.y ||
            ea->normal_x != eb->normal_x ||
            ea->normal_y != eb->normal_y) {
            return false;
        }
    }

    for (i = 0; i < a->metadata.edge_opening_count; ++i) {
        const dg_map_edge_opening_t *oa = &a->metadata.edge_openings[i];
        const dg_map_edge_opening_t *ob = &b->metadata.edge_openings[i];
        if (oa->id != ob->id ||
            oa->side != ob->side ||
            oa->start != ob->start ||
            oa->end != ob->end ||
            oa->length != ob->length ||
            oa->edge_tile.x != ob->edge_tile.x ||
            oa->edge_tile.y != ob->edge_tile.y ||
            oa->inward_tile.x != ob->inward_tile.x ||
            oa->inward_tile.y != ob->inward_tile.y ||
            oa->normal_x != ob->normal_x ||
            oa->normal_y != ob->normal_y ||
            oa->component_id != ob->component_id ||
            oa->role != ob->role) {
            return false;
        }
    }

    for (i = 0; i < a->metadata.room_adjacency_count; ++i) {
        const dg_room_adjacency_span_t *sa = &a->metadata.room_adjacency[i];
        const dg_room_adjacency_span_t *sb = &b->metadata.room_adjacency[i];
        if (sa->start_index != sb->start_index || sa->count != sb->count) {
            return false;
        }
    }

    for (i = 0; i < a->metadata.room_neighbor_count; ++i) {
        const dg_room_neighbor_t *na = &a->metadata.room_neighbors[i];
        const dg_room_neighbor_t *nb = &b->metadata.room_neighbors[i];
        if (na->room_id != nb->room_id || na->corridor_index != nb->corridor_index) {
            return false;
        }
    }

    for (i = 0; i < a->metadata.diagnostics.process_step_count; ++i) {
        const dg_process_step_diagnostics_t *sa = &a->metadata.diagnostics.process_steps[i];
        const dg_process_step_diagnostics_t *sb = &b->metadata.diagnostics.process_steps[i];
        if (sa->method_type != sb->method_type ||
            sa->walkable_before != sb->walkable_before ||
            sa->walkable_after != sb->walkable_after ||
            sa->walkable_delta != sb->walkable_delta ||
            sa->components_before != sb->components_before ||
            sa->components_after != sb->components_after ||
            sa->components_delta != sb->components_delta ||
            sa->connected_before != sb->connected_before ||
            sa->connected_after != sb->connected_after) {
            return false;
        }
    }

    for (i = 0; i < a->metadata.diagnostics.room_type_count; ++i) {
        const dg_room_type_quota_diagnostics_t *qa = &a->metadata.diagnostics.room_type_quotas[i];
        const dg_room_type_quota_diagnostics_t *qb = &b->metadata.diagnostics.room_type_quotas[i];
        if (qa->type_id != qb->type_id ||
            qa->enabled != qb->enabled ||
            qa->min_count != qb->min_count ||
            qa->max_count != qb->max_count ||
            qa->target_count != qb->target_count ||
            qa->assigned_count != qb->assigned_count ||
            qa->min_satisfied != qb->min_satisfied ||
            qa->max_satisfied != qb->max_satisfied ||
            qa->target_satisfied != qb->target_satisfied) {
            return false;
        }
    }

    return maps_have_same_generation_request_snapshot(a, b);
}

static size_t count_rooms_with_type_id(const dg_map_t *map, uint32_t type_id)
{
    size_t i;
    size_t count;

    if (map == NULL) {
        return 0;
    }

    count = 0;
    for (i = 0; i < map->metadata.room_count; ++i) {
        if (map->metadata.rooms[i].type_id == type_id) {
            count += 1;
        }
    }

    return count;
}

static size_t count_rooms_with_assigned_type(const dg_map_t *map)
{
    size_t i;
    size_t count;

    if (map == NULL) {
        return 0;
    }

    count = 0;
    for (i = 0; i < map->metadata.room_count; ++i) {
        if (map->metadata.rooms[i].type_id != DG_ROOM_TYPE_UNASSIGNED) {
            count += 1;
        }
    }

    return count;
}

static size_t count_wall_tiles_inside_rooms(const dg_map_t *map)
{
    size_t i;
    size_t count;
    size_t cell_count;

    if (map == NULL || map->tiles == NULL) {
        return 0;
    }

    count = 0;
    cell_count = (size_t)map->width * (size_t)map->height;
    for (i = 0; i < cell_count; ++i) {
        int x = (int)(i % (size_t)map->width);
        int y = (int)(i / (size_t)map->width);

        if (map->tiles[i] == DG_TILE_WALL && point_in_any_room(map, x, y)) {
            count += 1;
        }
    }

    return count;
}

static bool room_types_match_by_room_id(const dg_map_t *a, const dg_map_t *b)
{
    size_t i;

    if (a == NULL || b == NULL) {
        return false;
    }

    if (a->metadata.room_count != b->metadata.room_count) {
        return false;
    }

    if (a->metadata.room_count > 0 &&
        (a->metadata.rooms == NULL || b->metadata.rooms == NULL)) {
        return false;
    }

    for (i = 0; i < a->metadata.room_count; ++i) {
        int room_id = a->metadata.rooms[i].id;

        if (room_id < 0 || (size_t)room_id >= b->metadata.room_count) {
            return false;
        }

        if (b->metadata.rooms[room_id].id != room_id) {
            return false;
        }

        if (a->metadata.rooms[i].type_id != b->metadata.rooms[room_id].type_id) {
            return false;
        }
    }

    return true;
}

static bool has_outer_walls(const dg_map_t *map)
{
    int x;
    int y;

    for (x = 0; x < map->width; ++x) {
        if (dg_map_get_tile(map, x, 0) != DG_TILE_WALL) {
            return false;
        }
        if (dg_map_get_tile(map, x, map->height - 1) != DG_TILE_WALL) {
            return false;
        }
    }

    for (y = 0; y < map->height; ++y) {
        if (dg_map_get_tile(map, 0, y) != DG_TILE_WALL) {
            return false;
        }
        if (dg_map_get_tile(map, map->width - 1, y) != DG_TILE_WALL) {
            return false;
        }
    }

    return true;
}

static bool rects_overlap_with_padding(const dg_rect_t *a, const dg_rect_t *b, int padding)
{
    long long a_left;
    long long a_top;
    long long a_right;
    long long a_bottom;
    long long b_left;
    long long b_top;
    long long b_right;
    long long b_bottom;

    a_left = (long long)a->x - (long long)padding;
    a_top = (long long)a->y - (long long)padding;
    a_right = (long long)a->x + (long long)a->width + (long long)padding;
    a_bottom = (long long)a->y + (long long)a->height + (long long)padding;

    b_left = (long long)b->x;
    b_top = (long long)b->y;
    b_right = (long long)b->x + (long long)b->width;
    b_bottom = (long long)b->y + (long long)b->height;

    if (a_right <= b_left || b_right <= a_left) {
        return false;
    }
    if (a_bottom <= b_top || b_bottom <= a_top) {
        return false;
    }

    return true;
}

static bool rooms_have_min_wall_separation(const dg_map_t *map)
{
    size_t i;
    size_t j;

    for (i = 0; i < map->metadata.room_count; ++i) {
        for (j = i + 1; j < map->metadata.room_count; ++j) {
            if (rects_overlap_with_padding(&map->metadata.rooms[i].bounds, &map->metadata.rooms[j].bounds, 1)) {
                return false;
            }
        }
    }

    return true;
}

static bool corridors_have_unique_room_pairs(const dg_map_t *map)
{
    size_t i;
    size_t j;

    for (i = 0; i < map->metadata.corridor_count; ++i) {
        int ai = map->metadata.corridors[i].from_room_id;
        int bi = map->metadata.corridors[i].to_room_id;
        int min_i = (ai < bi) ? ai : bi;
        int max_i = (ai < bi) ? bi : ai;

        for (j = i + 1; j < map->metadata.corridor_count; ++j) {
            int aj = map->metadata.corridors[j].from_room_id;
            int bj = map->metadata.corridors[j].to_room_id;
            int min_j = (aj < bj) ? aj : bj;
            int max_j = (aj < bj) ? bj : aj;

            if (min_i == min_j && max_i == max_j) {
                return false;
            }
        }
    }

    return true;
}

static bool point_is_inside_any_room(const dg_map_t *map, int x, int y)
{
    size_t i;

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_rect_t *room = &map->metadata.rooms[i].bounds;
        if (
            x >= room->x &&
            y >= room->y &&
            x < room->x + room->width &&
            y < room->y + room->height
        ) {
            return true;
        }
    }

    return false;
}

static bool room_entrances_are_valid(const dg_map_t *map)
{
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    size_t i;

    if (map == NULL) {
        return false;
    }

    if (map->metadata.room_entrance_count == 0) {
        return map->metadata.generation_class != DG_MAP_GENERATION_CLASS_ROOM_LIKE ||
               map->metadata.room_count == 0;
    }
    if (map->metadata.room_entrances == NULL) {
        return false;
    }

    for (i = 0; i < map->metadata.room_entrance_count; ++i) {
        const dg_room_entrance_metadata_t *entrance = &map->metadata.room_entrances[i];
        const dg_rect_t *room;
        bool has_interior_neighbor;
        int d;

        if (entrance->room_id < 0 || (size_t)entrance->room_id >= map->metadata.room_count) {
            return false;
        }
        room = &map->metadata.rooms[entrance->room_id].bounds;

        if (!dg_map_in_bounds(map, entrance->room_tile.x, entrance->room_tile.y) ||
            !dg_map_in_bounds(map, entrance->corridor_tile.x, entrance->corridor_tile.y)) {
            return false;
        }
        if (!point_is_inside_any_room(map, entrance->room_tile.x, entrance->room_tile.y)) {
            return false;
        }
        if (!is_walkable(dg_map_get_tile(map, entrance->room_tile.x, entrance->room_tile.y))) {
            return false;
        }
        if (dg_map_get_tile(map, entrance->room_tile.x, entrance->room_tile.y) != DG_TILE_DOOR) {
            return false;
        }
        if (point_is_inside_any_room(map, entrance->corridor_tile.x, entrance->corridor_tile.y)) {
            return false;
        }
        if (!is_walkable(dg_map_get_tile(map, entrance->corridor_tile.x, entrance->corridor_tile.y))) {
            return false;
        }
        if (entrance->corridor_tile.x != entrance->room_tile.x + entrance->normal_x ||
            entrance->corridor_tile.y != entrance->room_tile.y + entrance->normal_y) {
            return false;
        }
        if (abs(entrance->normal_x) + abs(entrance->normal_y) != 1) {
            return false;
        }

        has_interior_neighbor = false;
        for (d = 0; d < 4; ++d) {
            int nx = entrance->room_tile.x + directions[d][0];
            int ny = entrance->room_tile.y + directions[d][1];

            if (!dg_map_in_bounds(map, nx, ny)) {
                continue;
            }
            if (!is_walkable(dg_map_get_tile(map, nx, ny))) {
                continue;
            }
            if (nx == entrance->corridor_tile.x && ny == entrance->corridor_tile.y) {
                continue;
            }
            if (nx >= room->x && ny >= room->y &&
                nx < room->x + room->width && ny < room->y + room->height) {
                has_interior_neighbor = true;
                break;
            }
        }

        if (!has_interior_neighbor) {
            return false;
        }
    }

    return true;
}

static size_t count_non_room_dead_ends(const dg_map_t *map)
{
    size_t count;
    int x;
    int y;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    count = 0;
    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            int neighbors;
            int d;
            dg_tile_t tile;

            if (point_is_inside_any_room(map, x, y)) {
                continue;
            }

            tile = dg_map_get_tile(map, x, y);
            if (!is_walkable(tile)) {
                continue;
            }

            neighbors = 0;
            for (d = 0; d < 4; ++d) {
                int nx = x + directions[d][0];
                int ny = y + directions[d][1];
                if (is_walkable(dg_map_get_tile(map, nx, ny))) {
                    neighbors += 1;
                }
            }

            if (neighbors <= 1) {
                count += 1;
            }
        }
    }

    return count;
}

static size_t count_room_connected_nub_dead_ends(const dg_map_t *map)
{
    size_t count;
    int x;
    int y;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    count = 0;
    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            int neighbors;
            int d;
            bool touches_room;

            if (point_is_inside_any_room(map, x, y)) {
                continue;
            }
            if (!is_walkable(dg_map_get_tile(map, x, y))) {
                continue;
            }

            neighbors = 0;
            touches_room = false;
            for (d = 0; d < 4; ++d) {
                int nx = x + directions[d][0];
                int ny = y + directions[d][1];
                dg_tile_t neighbor_tile = dg_map_get_tile(map, nx, ny);

                if (is_walkable(neighbor_tile)) {
                    neighbors += 1;
                    if (point_is_inside_any_room(map, nx, ny)) {
                        touches_room = true;
                    }
                }
            }

            if (touches_room && neighbors <= 1) {
                count += 1;
            }
        }
    }

    return count;
}

static size_t count_non_room_isolated_walkable_tiles(const dg_map_t *map)
{
    size_t count;
    int x;
    int y;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    count = 0;
    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            int neighbors;
            int d;
            dg_tile_t tile;

            if (point_is_inside_any_room(map, x, y)) {
                continue;
            }

            tile = dg_map_get_tile(map, x, y);
            if (!is_walkable(tile)) {
                continue;
            }

            neighbors = 0;
            for (d = 0; d < 4; ++d) {
                int nx = x + directions[d][0];
                int ny = y + directions[d][1];
                if (is_walkable(dg_map_get_tile(map, nx, ny))) {
                    neighbors += 1;
                }
            }

            if (neighbors == 0) {
                count += 1;
            }
        }
    }

    return count;
}

static size_t count_non_room_diagonal_touches_to_room_tiles(const dg_map_t *map)
{
    size_t count;
    int x;
    int y;
    static const int cardinals[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };
    static const int diagonals[4][2] = {
        {1, 1},
        {1, -1},
        {-1, 1},
        {-1, -1}
    };

    count = 0;
    for (y = 1; y < map->height - 1; ++y) {
        for (x = 1; x < map->width - 1; ++x) {
            int d;
            dg_tile_t tile;
            bool orthogonally_adjacent_to_room;

            if (point_is_inside_any_room(map, x, y)) {
                continue;
            }

            tile = dg_map_get_tile(map, x, y);
            if (!is_walkable(tile)) {
                continue;
            }

            orthogonally_adjacent_to_room = false;
            for (d = 0; d < 4; ++d) {
                int nx = x + cardinals[d][0];
                int ny = y + cardinals[d][1];
                if (point_is_inside_any_room(map, nx, ny) && is_walkable(dg_map_get_tile(map, nx, ny))) {
                    orthogonally_adjacent_to_room = true;
                    break;
                }
            }
            if (orthogonally_adjacent_to_room) {
                continue;
            }

            for (d = 0; d < 4; ++d) {
                int nx = x + diagonals[d][0];
                int ny = y + diagonals[d][1];
                if (!point_is_inside_any_room(map, nx, ny)) {
                    continue;
                }
                if (is_walkable(dg_map_get_tile(map, nx, ny))) {
                    count += 1;
                    break;
                }
            }
        }
    }

    return count;
}

static bool is_connected(const dg_map_t *map)
{
    size_t cell_count;
    unsigned char *visited;
    size_t *queue;
    size_t head;
    size_t tail;
    size_t i;
    size_t start;
    static const int directions[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    cell_count = (size_t)map->width * (size_t)map->height;
    visited = (unsigned char *)calloc(cell_count, sizeof(unsigned char));
    queue = (size_t *)malloc(cell_count * sizeof(size_t));
    if (visited == NULL || queue == NULL) {
        free(visited);
        free(queue);
        return false;
    }

    start = cell_count;
    for (i = 0; i < cell_count; ++i) {
        if (is_walkable(map->tiles[i])) {
            start = i;
            break;
        }
    }

    if (start == cell_count) {
        free(visited);
        free(queue);
        return false;
    }

    head = 0;
    tail = 0;
    queue[tail++] = start;
    visited[start] = 1;

    while (head < tail) {
        size_t current = queue[head++];
        int x = (int)(current % (size_t)map->width);
        int y = (int)(current / (size_t)map->width);
        int d;

        for (d = 0; d < 4; ++d) {
            int nx = x + directions[d][0];
            int ny = y + directions[d][1];
            size_t neighbor;

            if (!dg_map_in_bounds(map, nx, ny)) {
                continue;
            }

            neighbor = (size_t)ny * (size_t)map->width + (size_t)nx;
            if (visited[neighbor] != 0 || !is_walkable(map->tiles[neighbor])) {
                continue;
            }

            visited[neighbor] = 1;
            queue[tail++] = neighbor;
        }
    }

    for (i = 0; i < cell_count; ++i) {
        if (is_walkable(map->tiles[i]) && visited[i] == 0) {
            free(visited);
            free(queue);
            return false;
        }
    }

    free(visited);
    free(queue);
    return true;
}

static int test_map_basics(void)
{
    dg_map_t map = {0};
    dg_rect_t room = {2, 2, 4, 3};

    ASSERT_STATUS(dg_map_init(&map, 16, 8, DG_TILE_WALL), DG_STATUS_OK);
    ASSERT_TRUE(dg_map_in_bounds(&map, 0, 0));
    ASSERT_TRUE(!dg_map_in_bounds(&map, -1, 0));
    ASSERT_STATUS(dg_map_set_tile(&map, 3, 3, DG_TILE_FLOOR), DG_STATUS_OK);
    ASSERT_TRUE(dg_map_get_tile(&map, 3, 3) == DG_TILE_FLOOR);
    ASSERT_STATUS(dg_map_add_room(&map, &room, DG_ROOM_FLAG_NONE), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_add_corridor(&map, 0, 0, 1, 3), DG_STATUS_OK);
    ASSERT_TRUE(map.metadata.room_count == 1);
    ASSERT_TRUE(map.metadata.corridor_count == 1);

    dg_map_destroy(&map);
    return 0;
}

static int test_map_edge_opening_query_filters(void)
{
    dg_map_t map = {0};
    dg_map_edge_opening_query_t query;
    size_t matches[8];
    size_t count;
    const dg_map_edge_opening_t *found;

    ASSERT_STATUS(dg_map_init(&map, 8, 8, DG_TILE_WALL), DG_STATUS_OK);

    map.metadata.edge_openings =
        (dg_map_edge_opening_t *)calloc(3u, sizeof(dg_map_edge_opening_t));
    ASSERT_TRUE(map.metadata.edge_openings != NULL);
    map.metadata.edge_opening_capacity = 3u;
    map.metadata.edge_opening_count = 3u;
    map.metadata.primary_entrance_opening_id = 0;
    map.metadata.primary_exit_opening_id = 1;

    map.metadata.edge_openings[0].id = 0;
    map.metadata.edge_openings[0].side = DG_MAP_EDGE_TOP;
    map.metadata.edge_openings[0].start = 1;
    map.metadata.edge_openings[0].end = 2;
    map.metadata.edge_openings[0].length = 2;
    map.metadata.edge_openings[0].component_id = 0u;
    map.metadata.edge_openings[0].role = DG_MAP_EDGE_OPENING_ROLE_ENTRANCE;

    map.metadata.edge_openings[1].id = 1;
    map.metadata.edge_openings[1].side = DG_MAP_EDGE_RIGHT;
    map.metadata.edge_openings[1].start = 3;
    map.metadata.edge_openings[1].end = 5;
    map.metadata.edge_openings[1].length = 3;
    map.metadata.edge_openings[1].component_id = 0u;
    map.metadata.edge_openings[1].role = DG_MAP_EDGE_OPENING_ROLE_EXIT;

    map.metadata.edge_openings[2].id = 2;
    map.metadata.edge_openings[2].side = DG_MAP_EDGE_LEFT;
    map.metadata.edge_openings[2].start = 4;
    map.metadata.edge_openings[2].end = 4;
    map.metadata.edge_openings[2].length = 1;
    map.metadata.edge_openings[2].component_id = 1u;
    map.metadata.edge_openings[2].role = DG_MAP_EDGE_OPENING_ROLE_NONE;

    dg_default_map_edge_opening_query(&query);
    count = dg_map_query_edge_openings(&map, &query, matches, sizeof(matches) / sizeof(matches[0]));
    ASSERT_TRUE(count == 3u);
    ASSERT_TRUE(matches[0] == 0u);
    ASSERT_TRUE(matches[1] == 1u);
    ASSERT_TRUE(matches[2] == 2u);

    query.role_mask = DG_MAP_EDGE_OPENING_ROLE_MASK_ANY;
    query.side_mask = DG_MAP_EDGE_MASK_RIGHT;
    count = dg_map_query_edge_openings(&map, &query, matches, sizeof(matches) / sizeof(matches[0]));
    ASSERT_TRUE(count == 1u);
    ASSERT_TRUE(matches[0] == 1u);

    query.side_mask = DG_MAP_EDGE_MASK_ALL;
    query.require_component = 1;
    count = dg_map_query_edge_openings(&map, &query, matches, sizeof(matches) / sizeof(matches[0]));
    ASSERT_TRUE(count == 1u);
    ASSERT_TRUE(matches[0] == 2u);

    query.require_component = -1;
    query.edge_coord_min = 2;
    query.edge_coord_max = 4;
    query.min_length = 2;
    query.max_length = 2;
    count = dg_map_query_edge_openings(&map, &query, matches, sizeof(matches) / sizeof(matches[0]));
    ASSERT_TRUE(count == 1u);
    ASSERT_TRUE(matches[0] == 0u);

    found = dg_map_find_edge_opening_by_id(&map, 1);
    ASSERT_TRUE(found != NULL);
    ASSERT_TRUE(found->side == DG_MAP_EDGE_RIGHT);
    ASSERT_TRUE(found->role == DG_MAP_EDGE_OPENING_ROLE_EXIT);
    ASSERT_TRUE(dg_map_find_edge_opening_by_id(&map, 99) == NULL);

    dg_map_destroy(&map);
    return 0;
}

static int test_rng_reproducibility(void)
{
    int i;
    dg_rng_t a = {0};
    dg_rng_t b = {0};

    dg_rng_seed(&a, 123456u);
    dg_rng_seed(&b, 123456u);
    for (i = 0; i < 64; ++i) {
        ASSERT_TRUE(dg_rng_next_u32(&a) == dg_rng_next_u32(&b));
    }

    return 0;
}

static int test_bsp_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;
    size_t i;

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 96, 54, 42u);
    request.params.bsp.min_rooms = 10;
    request.params.bsp.max_rooms = 10;
    request.params.bsp.room_min_size = 4;
    request.params.bsp.room_max_size = 11;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_BSP_TREE);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE);
    ASSERT_TRUE(map.metadata.room_count == 10);
    ASSERT_TRUE(map.metadata.corridor_count == map.metadata.room_count - 1);
    ASSERT_TRUE(map.metadata.connected_floor);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.generation_attempts == 1);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(is_connected(&map));

    for (i = 0; i < map.metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map.metadata.rooms[i];
        ASSERT_TRUE(room->bounds.width >= request.params.bsp.room_min_size);
        ASSERT_TRUE(room->bounds.width <= request.params.bsp.room_max_size);
        ASSERT_TRUE(room->bounds.height >= request.params.bsp.room_min_size);
        ASSERT_TRUE(room->bounds.height <= request.params.bsp.room_max_size);
        ASSERT_TRUE(room->role == DG_ROOM_ROLE_NONE);
        ASSERT_TRUE(room->type_id == DG_ROOM_TYPE_UNASSIGNED);
        ASSERT_TRUE(room->flags == DG_ROOM_FLAG_NONE);
    }

    dg_map_destroy(&map);
    return 0;
}

static int test_explicit_edge_openings_are_applied(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_edge_opening_spec_t openings[2];
    const dg_map_edge_opening_t *entrance_opening;
    dg_tile_t edge_tile;
    dg_tile_t inward_tile;

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 42424u);
    request.params.bsp.min_rooms = 10;
    request.params.bsp.max_rooms = 12;

    openings[0].side = DG_MAP_EDGE_TOP;
    openings[0].start = 8;
    openings[0].end = 10;
    openings[0].role = DG_MAP_EDGE_OPENING_ROLE_ENTRANCE;
    openings[1].side = DG_MAP_EDGE_BOTTOM;
    openings[1].start = 28;
    openings[1].end = 30;
    openings[1].role = DG_MAP_EDGE_OPENING_ROLE_EXIT;
    request.edge_openings.openings = openings;
    request.edge_openings.opening_count = 2u;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    edge_tile = dg_map_get_tile(&map, 8, 0);
    inward_tile = dg_map_get_tile(&map, 8, 1);
    ASSERT_TRUE(is_walkable(edge_tile));
    ASSERT_TRUE(is_walkable(inward_tile));
    ASSERT_TRUE(map.metadata.edge_opening_count >= 2u);

    entrance_opening = dg_map_find_edge_opening_by_id(
        &map,
        map.metadata.primary_entrance_opening_id
    );
    ASSERT_TRUE(entrance_opening != NULL);
    ASSERT_TRUE(entrance_opening->role == DG_MAP_EDGE_OPENING_ROLE_ENTRANCE);

    dg_map_destroy(&map);
    return 0;
}

static int test_bsp_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 1337u);
    request.params.bsp.min_rooms = 9;
    request.params.bsp.max_rooms = 13;
    request.params.bsp.room_min_size = 4;
    request.params.bsp.room_max_size = 10;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_drunkards_walk_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;

    dg_default_generate_request(&request, DG_ALGORITHM_DRUNKARDS_WALK, 96, 54, 4242u);
    request.params.drunkards_walk.wiggle_percent = 70;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_DRUNKARDS_WALK);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_CAVE_LIKE);
    ASSERT_TRUE(map.metadata.room_count == 0);
    ASSERT_TRUE(map.metadata.corridor_count == 0);
    ASSERT_TRUE(map.metadata.connected_floor);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(is_connected(&map));

    dg_map_destroy(&map);
    return 0;
}

static int test_drunkards_walk_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_DRUNKARDS_WALK, 88, 48, 7070u);
    request.params.drunkards_walk.wiggle_percent = 45;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_drunkards_wiggle_affects_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 500u; seed < 560u; ++seed) {
        dg_generate_request_t request_low;
        dg_generate_request_t request_high;
        dg_map_t low = {0};
        dg_map_t high = {0};

        dg_default_generate_request(&request_low, DG_ALGORITHM_DRUNKARDS_WALK, 80, 44, seed);
        request_low.params.drunkards_walk.wiggle_percent = 5;

        dg_default_generate_request(&request_high, DG_ALGORITHM_DRUNKARDS_WALK, 80, 44, seed);
        request_high.params.drunkards_walk.wiggle_percent = 95;

        ASSERT_STATUS(dg_generate(&request_low, &low), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&request_high, &high), DG_STATUS_OK);

        if (!maps_have_same_tiles(&low, &high)) {
            found_difference = true;
        }

        dg_map_destroy(&low);
        dg_map_destroy(&high);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_cellular_automata_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;

    dg_default_generate_request(&request, DG_ALGORITHM_CELLULAR_AUTOMATA, 96, 54, 9876u);
    request.params.cellular_automata.initial_wall_percent = 45;
    request.params.cellular_automata.simulation_steps = 5;
    request.params.cellular_automata.wall_threshold = 5;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_CELLULAR_AUTOMATA);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_CAVE_LIKE);
    ASSERT_TRUE(map.metadata.room_count == 0);
    ASSERT_TRUE(map.metadata.corridor_count == 0);
    ASSERT_TRUE(map.metadata.connected_floor);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(is_connected(&map));

    dg_map_destroy(&map);
    return 0;
}

static int test_cellular_automata_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_CELLULAR_AUTOMATA, 88, 48, 3333u);
    request.params.cellular_automata.initial_wall_percent = 49;
    request.params.cellular_automata.simulation_steps = 4;
    request.params.cellular_automata.wall_threshold = 5;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_cellular_automata_threshold_affects_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 6100u; seed < 6180u; ++seed) {
        dg_generate_request_t low_threshold;
        dg_generate_request_t high_threshold;
        dg_map_t low = {0};
        dg_map_t high = {0};

        dg_default_generate_request(&low_threshold, DG_ALGORITHM_CELLULAR_AUTOMATA, 80, 44, seed);
        low_threshold.params.cellular_automata.initial_wall_percent = 45;
        low_threshold.params.cellular_automata.simulation_steps = 5;
        low_threshold.params.cellular_automata.wall_threshold = 4;

        high_threshold = low_threshold;
        high_threshold.params.cellular_automata.wall_threshold = 6;

        ASSERT_STATUS(dg_generate(&low_threshold, &low), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&high_threshold, &high), DG_STATUS_OK);

        if (!maps_have_same_tiles(&low, &high)) {
            found_difference = true;
        }

        dg_map_destroy(&low);
        dg_map_destroy(&high);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_value_noise_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 96, 54, 2468u);
    request.params.value_noise.feature_size = 11;
    request.params.value_noise.octaves = 3;
    request.params.value_noise.persistence_percent = 55;
    request.params.value_noise.floor_threshold_percent = 47;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_VALUE_NOISE);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_CAVE_LIKE);
    ASSERT_TRUE(map.metadata.room_count == 0);
    ASSERT_TRUE(map.metadata.corridor_count == 0);
    ASSERT_TRUE(map.metadata.connected_floor);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(is_connected(&map));

    dg_map_destroy(&map);
    return 0;
}

static int test_value_noise_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 88, 48, 13579u);
    request.params.value_noise.feature_size = 10;
    request.params.value_noise.octaves = 4;
    request.params.value_noise.persistence_percent = 60;
    request.params.value_noise.floor_threshold_percent = 50;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_value_noise_threshold_affects_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 7200u; seed < 7280u; ++seed) {
        dg_generate_request_t open_request;
        dg_generate_request_t tight_request;
        dg_map_t open_map = {0};
        dg_map_t tight_map = {0};

        dg_default_generate_request(&open_request, DG_ALGORITHM_VALUE_NOISE, 88, 48, seed);
        open_request.params.value_noise.feature_size = 12;
        open_request.params.value_noise.octaves = 3;
        open_request.params.value_noise.persistence_percent = 55;
        open_request.params.value_noise.floor_threshold_percent = 40;

        tight_request = open_request;
        tight_request.params.value_noise.floor_threshold_percent = 58;

        ASSERT_STATUS(dg_generate(&open_request, &open_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&tight_request, &tight_map), DG_STATUS_OK);

        if (!maps_have_same_tiles(&open_map, &tight_map)) {
            found_difference = true;
        }

        dg_map_destroy(&open_map);
        dg_map_destroy(&tight_map);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_room_graph_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;
    size_t i;

    dg_default_generate_request(&request, DG_ALGORITHM_ROOM_GRAPH, 96, 54, 314159u);
    request.params.room_graph.min_rooms = 8;
    request.params.room_graph.max_rooms = 12;
    request.params.room_graph.room_min_size = 4;
    request.params.room_graph.room_max_size = 10;
    request.params.room_graph.neighbor_candidates = 4;
    request.params.room_graph.extra_connection_chance_percent = 20;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_ROOM_GRAPH);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE);
    ASSERT_TRUE(map.metadata.room_count >= 2);
    ASSERT_TRUE(map.metadata.room_count <= (size_t)request.params.room_graph.max_rooms);
    ASSERT_TRUE(map.metadata.corridor_count >= map.metadata.room_count - 1);
    ASSERT_TRUE(map.metadata.connected_floor);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(rooms_have_min_wall_separation(&map));
    ASSERT_TRUE(corridors_have_unique_room_pairs(&map));
    ASSERT_TRUE(is_connected(&map));

    for (i = 0; i < map.metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map.metadata.rooms[i];
        ASSERT_TRUE(room->bounds.width >= request.params.room_graph.room_min_size);
        ASSERT_TRUE(room->bounds.width <= request.params.room_graph.room_max_size);
        ASSERT_TRUE(room->bounds.height >= request.params.room_graph.room_min_size);
        ASSERT_TRUE(room->bounds.height <= request.params.room_graph.room_max_size);
        ASSERT_TRUE(room->role == DG_ROOM_ROLE_NONE);
        ASSERT_TRUE(room->type_id == DG_ROOM_TYPE_UNASSIGNED);
        ASSERT_TRUE(room->flags == DG_ROOM_FLAG_NONE);
    }

    dg_map_destroy(&map);
    return 0;
}

static int test_room_graph_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_ROOM_GRAPH, 88, 48, 161803u);
    request.params.room_graph.min_rooms = 9;
    request.params.room_graph.max_rooms = 14;
    request.params.room_graph.room_min_size = 4;
    request.params.room_graph.room_max_size = 9;
    request.params.room_graph.neighbor_candidates = 5;
    request.params.room_graph.extra_connection_chance_percent = 35;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_room_graph_loop_chance_affects_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 8600u; seed < 8700u; ++seed) {
        dg_generate_request_t tree_request;
        dg_generate_request_t loopy_request;
        dg_map_t tree_map = {0};
        dg_map_t loopy_map = {0};

        dg_default_generate_request(&tree_request, DG_ALGORITHM_ROOM_GRAPH, 88, 48, seed);
        tree_request.params.room_graph.min_rooms = 9;
        tree_request.params.room_graph.max_rooms = 14;
        tree_request.params.room_graph.room_min_size = 4;
        tree_request.params.room_graph.room_max_size = 9;
        tree_request.params.room_graph.neighbor_candidates = 5;
        tree_request.params.room_graph.extra_connection_chance_percent = 0;

        loopy_request = tree_request;
        loopy_request.params.room_graph.extra_connection_chance_percent = 100;

        ASSERT_STATUS(dg_generate(&tree_request, &tree_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&loopy_request, &loopy_map), DG_STATUS_OK);

        if (!maps_have_same_tiles(&tree_map, &loopy_map) ||
            tree_map.metadata.corridor_count != loopy_map.metadata.corridor_count) {
            found_difference = true;
        }

        dg_map_destroy(&tree_map);
        dg_map_destroy(&loopy_map);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_worm_caves_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 96, 54, 271828u);
    request.params.worm_caves.worm_count = 8;
    request.params.worm_caves.wiggle_percent = 55;
    request.params.worm_caves.branch_chance_percent = 10;
    request.params.worm_caves.target_floor_percent = 36;
    request.params.worm_caves.brush_radius = 1;
    request.params.worm_caves.max_steps_per_worm = 1000;
    request.params.worm_caves.ensure_connected = 1;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_WORM_CAVES);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_CAVE_LIKE);
    ASSERT_TRUE(map.metadata.room_count == 0);
    ASSERT_TRUE(map.metadata.corridor_count == 0);
    ASSERT_TRUE(map.metadata.connected_floor);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(is_connected(&map));

    dg_map_destroy(&map);
    return 0;
}

static int test_worm_caves_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 88, 48, 31415u);
    request.params.worm_caves.worm_count = 6;
    request.params.worm_caves.wiggle_percent = 45;
    request.params.worm_caves.branch_chance_percent = 12;
    request.params.worm_caves.target_floor_percent = 30;
    request.params.worm_caves.brush_radius = 0;
    request.params.worm_caves.max_steps_per_worm = 1200;
    request.params.worm_caves.ensure_connected = 1;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_worm_caves_branch_affects_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 9100u; seed < 9200u; ++seed) {
        dg_generate_request_t low_branch_request;
        dg_generate_request_t high_branch_request;
        dg_map_t low_branch_map = {0};
        dg_map_t high_branch_map = {0};

        dg_default_generate_request(&low_branch_request, DG_ALGORITHM_WORM_CAVES, 88, 48, seed);
        low_branch_request.params.worm_caves.worm_count = 6;
        low_branch_request.params.worm_caves.wiggle_percent = 40;
        low_branch_request.params.worm_caves.branch_chance_percent = 0;
        low_branch_request.params.worm_caves.target_floor_percent = 32;
        low_branch_request.params.worm_caves.brush_radius = 0;
        low_branch_request.params.worm_caves.max_steps_per_worm = 900;
        low_branch_request.params.worm_caves.ensure_connected = 1;

        high_branch_request = low_branch_request;
        high_branch_request.params.worm_caves.branch_chance_percent = 45;

        ASSERT_STATUS(dg_generate(&low_branch_request, &low_branch_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&high_branch_request, &high_branch_map), DG_STATUS_OK);

        if (!maps_have_same_tiles(&low_branch_map, &high_branch_map)) {
            found_difference = true;
        }

        dg_map_destroy(&low_branch_map);
        dg_map_destroy(&high_branch_map);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_simplex_noise_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 96, 54, 141421u);
    request.params.simplex_noise.feature_size = 16;
    request.params.simplex_noise.octaves = 4;
    request.params.simplex_noise.persistence_percent = 55;
    request.params.simplex_noise.floor_threshold_percent = 50;
    request.params.simplex_noise.ensure_connected = 1;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_SIMPLEX_NOISE);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_CAVE_LIKE);
    ASSERT_TRUE(map.metadata.room_count == 0);
    ASSERT_TRUE(map.metadata.corridor_count == 0);
    ASSERT_TRUE(map.metadata.connected_floor);
    ASSERT_TRUE(map.metadata.connected_component_count == 1);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(is_connected(&map));

    dg_map_destroy(&map);
    return 0;
}

static int test_simplex_noise_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 88, 48, 173205u);
    request.params.simplex_noise.feature_size = 15;
    request.params.simplex_noise.octaves = 5;
    request.params.simplex_noise.persistence_percent = 60;
    request.params.simplex_noise.floor_threshold_percent = 52;
    request.params.simplex_noise.ensure_connected = 1;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_simplex_noise_threshold_affects_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 9700u; seed < 9780u; ++seed) {
        dg_generate_request_t open_request;
        dg_generate_request_t tight_request;
        dg_map_t open_map = {0};
        dg_map_t tight_map = {0};

        dg_default_generate_request(&open_request, DG_ALGORITHM_SIMPLEX_NOISE, 88, 48, seed);
        open_request.params.simplex_noise.feature_size = 14;
        open_request.params.simplex_noise.octaves = 4;
        open_request.params.simplex_noise.persistence_percent = 55;
        open_request.params.simplex_noise.floor_threshold_percent = 38;
        open_request.params.simplex_noise.ensure_connected = 1;

        tight_request = open_request;
        tight_request.params.simplex_noise.floor_threshold_percent = 62;

        ASSERT_STATUS(dg_generate(&open_request, &open_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&tight_request, &tight_map), DG_STATUS_OK);

        if (!maps_have_same_tiles(&open_map, &tight_map)) {
            found_difference = true;
        }

        dg_map_destroy(&open_map);
        dg_map_destroy(&tight_map);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_rooms_and_mazes_generation(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    size_t floors;
    size_t i;

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 96, 54, 2026u);
    request.params.rooms_and_mazes.min_rooms = 10;
    request.params.rooms_and_mazes.max_rooms = 14;
    request.params.rooms_and_mazes.room_min_size = 4;
    request.params.rooms_and_mazes.room_max_size = 10;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    floors = count_walkable_tiles(&map);
    ASSERT_TRUE(floors > 0);
    ASSERT_TRUE(map.metadata.algorithm_id == DG_ALGORITHM_ROOMS_AND_MAZES);
    ASSERT_TRUE(map.metadata.generation_class == DG_MAP_GENERATION_CLASS_ROOM_LIKE);
    ASSERT_TRUE(map.metadata.room_count >= (size_t)request.params.rooms_and_mazes.min_rooms);
    ASSERT_TRUE(map.metadata.room_count <= (size_t)request.params.rooms_and_mazes.max_rooms);
    ASSERT_TRUE(map.metadata.walkable_tile_count == floors);
    ASSERT_TRUE(has_outer_walls(&map));
    ASSERT_TRUE(rooms_have_min_wall_separation(&map));
    ASSERT_TRUE(corridors_have_unique_room_pairs(&map));
    ASSERT_TRUE(count_non_room_diagonal_touches_to_room_tiles(&map) == 0);
    ASSERT_TRUE(map.metadata.connected_floor);

    for (i = 0; i < map.metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map.metadata.rooms[i];
        ASSERT_TRUE(room->bounds.width >= request.params.rooms_and_mazes.room_min_size);
        ASSERT_TRUE(room->bounds.width <= request.params.rooms_and_mazes.room_max_size);
        ASSERT_TRUE(room->bounds.height >= request.params.rooms_and_mazes.room_min_size);
        ASSERT_TRUE(room->bounds.height <= request.params.rooms_and_mazes.room_max_size);
        ASSERT_TRUE(room->role == DG_ROOM_ROLE_NONE);
        ASSERT_TRUE(room->type_id == DG_ROOM_TYPE_UNASSIGNED);
        ASSERT_TRUE(room->flags == DG_ROOM_FLAG_NONE);
    }

    dg_map_destroy(&map);
    return 0;
}

static int test_rooms_and_mazes_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, 9151u);
    request.params.rooms_and_mazes.min_rooms = 8;
    request.params.rooms_and_mazes.max_rooms = 12;
    request.params.rooms_and_mazes.room_min_size = 4;
    request.params.rooms_and_mazes.room_max_size = 9;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_rooms_and_mazes_pruning_control(void)
{
    uint64_t seed;
    bool found_seed_with_pruning_effect;

    found_seed_with_pruning_effect = false;
    for (seed = 1000u; seed < 1200u; ++seed) {
        dg_generate_request_t request_off;
        dg_generate_request_t request_full;
        dg_map_t map_off = {0};
        dg_map_t map_full = {0};
        size_t dead_ends_off;
        size_t dead_ends_full;

        dg_default_generate_request(&request_off, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        request_off.params.rooms_and_mazes.min_rooms = 9;
        request_off.params.rooms_and_mazes.max_rooms = 14;
        request_off.params.rooms_and_mazes.room_min_size = 4;
        request_off.params.rooms_and_mazes.room_max_size = 10;
        request_off.params.rooms_and_mazes.dead_end_prune_steps = 0;

        request_full = request_off;
        request_full.params.rooms_and_mazes.dead_end_prune_steps = -1;

        ASSERT_STATUS(dg_generate(&request_off, &map_off), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&request_full, &map_full), DG_STATUS_OK);

        dead_ends_off = count_non_room_dead_ends(&map_off);
        dead_ends_full = count_non_room_dead_ends(&map_full);

        if (dead_ends_off > 0 && dead_ends_full == 0) {
            found_seed_with_pruning_effect = true;
            ASSERT_TRUE(!maps_have_same_tiles(&map_off, &map_full));
        }

        dg_map_destroy(&map_off);
        dg_map_destroy(&map_full);

        if (found_seed_with_pruning_effect) {
            break;
        }
    }

    ASSERT_TRUE(found_seed_with_pruning_effect);
    return 0;
}

static int test_rooms_and_mazes_wiggle_affects_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 3200u; seed < 3280u; ++seed) {
        dg_generate_request_t request_low;
        dg_generate_request_t request_high;
        dg_map_t low = {0};
        dg_map_t high = {0};

        dg_default_generate_request(&request_low, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        request_low.params.rooms_and_mazes.min_rooms = 9;
        request_low.params.rooms_and_mazes.max_rooms = 14;
        request_low.params.rooms_and_mazes.room_min_size = 4;
        request_low.params.rooms_and_mazes.room_max_size = 10;
        request_low.params.rooms_and_mazes.dead_end_prune_steps = 0;
        request_low.params.rooms_and_mazes.ensure_full_connectivity = 0;
        request_low.params.rooms_and_mazes.maze_wiggle_percent = 0;

        request_high = request_low;
        request_high.params.rooms_and_mazes.maze_wiggle_percent = 100;

        ASSERT_STATUS(dg_generate(&request_low, &low), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&request_high, &high), DG_STATUS_OK);

        if (!maps_have_same_tiles(&low, &high)) {
            found_difference = true;
        }

        dg_map_destroy(&low);
        dg_map_destroy(&high);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_rooms_and_mazes_unpruned_has_no_isolated_seed_tiles(void)
{
    uint64_t seed;

    for (seed = 2000u; seed < 2120u; ++seed) {
        dg_generate_request_t request;
        dg_map_t map = {0};
        size_t isolated_count;

        dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        request.params.rooms_and_mazes.min_rooms = 9;
        request.params.rooms_and_mazes.max_rooms = 14;
        request.params.rooms_and_mazes.room_min_size = 4;
        request.params.rooms_and_mazes.room_max_size = 10;
        request.params.rooms_and_mazes.dead_end_prune_steps = 0;

        ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
        isolated_count = count_non_room_isolated_walkable_tiles(&map);
        dg_map_destroy(&map);

        ASSERT_TRUE(isolated_count == 0);
    }

    return 0;
}

static int test_rooms_and_mazes_pruned_has_no_room_nub_dead_ends(void)
{
    uint64_t seed;

    for (seed = 4400u; seed < 4560u; ++seed) {
        dg_generate_request_t request;
        dg_map_t map = {0};
        size_t nub_count;

        dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        request.params.rooms_and_mazes.min_rooms = 9;
        request.params.rooms_and_mazes.max_rooms = 14;
        request.params.rooms_and_mazes.room_min_size = 4;
        request.params.rooms_and_mazes.room_max_size = 10;
        request.params.rooms_and_mazes.dead_end_prune_steps = -1;

        ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
        nub_count = count_room_connected_nub_dead_ends(&map);
        dg_map_destroy(&map);

        ASSERT_TRUE(nub_count == 0);
    }

    return 0;
}

static int test_post_process_scaling(void)
{
    dg_generate_request_t base_request;
    dg_generate_request_t scaled_request;
    dg_map_t base_map = {0};
    dg_map_t scaled_map = {0};
    dg_process_method_t scaled_methods[1];
    int factor;
    size_t base_doors;
    size_t scaled_doors;

    dg_default_generate_request(&base_request, DG_ALGORITHM_BSP_TREE, 72, 42, 123456u);
    base_request.params.bsp.min_rooms = 8;
    base_request.params.bsp.max_rooms = 10;
    base_request.params.bsp.room_min_size = 4;
    base_request.params.bsp.room_max_size = 9;

    scaled_request = base_request;
    factor = 3;
    dg_default_process_method(&scaled_methods[0], DG_PROCESS_METHOD_SCALE);
    scaled_methods[0].params.scale.factor = factor;
    scaled_request.process.methods = scaled_methods;
    scaled_request.process.method_count = 1;

    ASSERT_STATUS(dg_generate(&base_request, &base_map), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&scaled_request, &scaled_map), DG_STATUS_OK);

    ASSERT_TRUE(scaled_map.width == base_map.width * factor);
    ASSERT_TRUE(scaled_map.height == base_map.height * factor);
    ASSERT_TRUE(scaled_map.metadata.room_count == base_map.metadata.room_count);
    ASSERT_TRUE(scaled_map.metadata.corridor_count == base_map.metadata.corridor_count);
    ASSERT_TRUE(scaled_map.metadata.generation_request.process.method_count == 1);
    ASSERT_TRUE(
        scaled_map.metadata.generation_request.process.methods[0].type ==
        (int)DG_PROCESS_METHOD_SCALE
    );
    ASSERT_TRUE(
        scaled_map.metadata.generation_request.process.methods[0].params.scale.factor == factor
    );
    ASSERT_TRUE(scaled_map.metadata.rooms[0].bounds.width == base_map.metadata.rooms[0].bounds.width * factor);
    ASSERT_TRUE(scaled_map.metadata.rooms[0].bounds.height == base_map.metadata.rooms[0].bounds.height * factor);
    base_doors = count_door_tiles(&base_map);
    scaled_doors = count_door_tiles(&scaled_map);
    ASSERT_TRUE(base_doors > 0);
    ASSERT_TRUE(scaled_doors > base_doors);

    dg_map_destroy(&base_map);
    dg_map_destroy(&scaled_map);
    return 0;
}

static int test_post_process_disabled_bypasses_pipeline(void)
{
    dg_generate_request_t base_request;
    dg_generate_request_t disabled_request;
    dg_map_t base_map = {0};
    dg_map_t disabled_map = {0};
    dg_process_method_t scaled_methods[1];

    dg_default_generate_request(&base_request, DG_ALGORITHM_BSP_TREE, 72, 42, 123456u);
    base_request.params.bsp.min_rooms = 8;
    base_request.params.bsp.max_rooms = 10;
    base_request.params.bsp.room_min_size = 4;
    base_request.params.bsp.room_max_size = 9;

    disabled_request = base_request;
    dg_default_process_method(&scaled_methods[0], DG_PROCESS_METHOD_SCALE);
    scaled_methods[0].params.scale.factor = 3;
    disabled_request.process.enabled = 0;
    disabled_request.process.methods = scaled_methods;
    disabled_request.process.method_count = 1;

    ASSERT_STATUS(dg_generate(&base_request, &base_map), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&disabled_request, &disabled_map), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&base_map, &disabled_map));
    ASSERT_TRUE(disabled_map.metadata.diagnostics.process_step_count == 0);
    ASSERT_TRUE(disabled_map.metadata.generation_request.process.enabled == 0);
    ASSERT_TRUE(disabled_map.metadata.generation_request.process.method_count == 1);
    ASSERT_TRUE(
        disabled_map.metadata.generation_request.process.methods[0].type ==
        (int)DG_PROCESS_METHOD_SCALE
    );

    dg_map_destroy(&base_map);
    dg_map_destroy(&disabled_map);
    return 0;
}

static int test_post_process_path_smoothing_changes_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 4000u; seed < 4060u; ++seed) {
        dg_generate_request_t base_request;
        dg_generate_request_t smooth_request;
        dg_map_t base_map = {0};
        dg_map_t smooth_map = {0};
        dg_process_method_t smooth_methods[1];

        dg_default_generate_request(&base_request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        base_request.params.rooms_and_mazes.min_rooms = 10;
        base_request.params.rooms_and_mazes.max_rooms = 16;
        base_request.params.rooms_and_mazes.room_min_size = 4;
        base_request.params.rooms_and_mazes.room_max_size = 10;
        base_request.params.rooms_and_mazes.dead_end_prune_steps = 0;

        smooth_request = base_request;
        dg_default_process_method(&smooth_methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
        smooth_methods[0].params.path_smooth.strength = 2;
        smooth_methods[0].params.path_smooth.inner_enabled = 1;
        smooth_methods[0].params.path_smooth.outer_enabled = 1;
        smooth_request.process.methods = smooth_methods;
        smooth_request.process.method_count = 1;

        ASSERT_STATUS(dg_generate(&base_request, &base_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&smooth_request, &smooth_map), DG_STATUS_OK);

        if (!maps_have_same_tiles(&base_map, &smooth_map)) {
            found_difference = true;
        }

        dg_map_destroy(&base_map);
        dg_map_destroy(&smooth_map);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_post_process_path_smoothing_outer_trim_effect(void)
{
    uint64_t seed;
    bool found_outer_trim;

    found_outer_trim = false;
    for (seed = 5200u; seed < 5280u; ++seed) {
        dg_generate_request_t inner_only_request;
        dg_generate_request_t inner_outer_request;
        dg_map_t inner_only_map = {0};
        dg_map_t inner_outer_map = {0};
        dg_process_method_t inner_method[1];
        dg_process_method_t inner_outer_method[1];

        dg_default_generate_request(&inner_only_request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        inner_only_request.params.rooms_and_mazes.min_rooms = 10;
        inner_only_request.params.rooms_and_mazes.max_rooms = 16;
        inner_only_request.params.rooms_and_mazes.room_min_size = 4;
        inner_only_request.params.rooms_and_mazes.room_max_size = 10;
        inner_only_request.params.rooms_and_mazes.dead_end_prune_steps = 0;

        inner_outer_request = inner_only_request;
        dg_default_process_method(&inner_method[0], DG_PROCESS_METHOD_PATH_SMOOTH);
        inner_method[0].params.path_smooth.strength = 2;
        inner_method[0].params.path_smooth.inner_enabled = 1;
        inner_method[0].params.path_smooth.outer_enabled = 0;
        inner_only_request.process.methods = inner_method;
        inner_only_request.process.method_count = 1;

        dg_default_process_method(&inner_outer_method[0], DG_PROCESS_METHOD_PATH_SMOOTH);
        inner_outer_method[0].params.path_smooth.strength = 2;
        inner_outer_method[0].params.path_smooth.inner_enabled = 1;
        inner_outer_method[0].params.path_smooth.outer_enabled = 1;
        inner_outer_request.process.methods = inner_outer_method;
        inner_outer_request.process.method_count = 1;

        ASSERT_STATUS(dg_generate(&inner_only_request, &inner_only_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&inner_outer_request, &inner_outer_map), DG_STATUS_OK);

        ASSERT_TRUE(
            inner_outer_map.metadata.connected_component_count <=
            inner_only_map.metadata.connected_component_count
        );
        if (inner_outer_map.metadata.walkable_tile_count <
            inner_only_map.metadata.walkable_tile_count) {
            found_outer_trim = true;
        }

        dg_map_destroy(&inner_only_map);
        dg_map_destroy(&inner_outer_map);

        if (found_outer_trim) {
            break;
        }
    }

    ASSERT_TRUE(found_outer_trim);
    return 0;
}

static int test_post_process_path_smoothing_combined_modes(void)
{
    uint64_t seed;
    bool found_combined_behavior;

    found_combined_behavior = false;
    for (seed = 5400u; seed < 5500u; ++seed) {
        dg_generate_request_t inner_request;
        dg_generate_request_t outer_request;
        dg_generate_request_t combined_request;
        dg_map_t inner_map = {0};
        dg_map_t outer_map = {0};
        dg_map_t combined_map = {0};
        dg_process_method_t inner_method[1];
        dg_process_method_t outer_method[1];
        dg_process_method_t combined_method[1];

        dg_default_generate_request(&inner_request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        inner_request.params.rooms_and_mazes.min_rooms = 10;
        inner_request.params.rooms_and_mazes.max_rooms = 16;
        inner_request.params.rooms_and_mazes.room_min_size = 4;
        inner_request.params.rooms_and_mazes.room_max_size = 10;
        inner_request.params.rooms_and_mazes.dead_end_prune_steps = 0;

        outer_request = inner_request;
        combined_request = inner_request;

        dg_default_process_method(&inner_method[0], DG_PROCESS_METHOD_PATH_SMOOTH);
        inner_method[0].params.path_smooth.strength = 2;
        inner_method[0].params.path_smooth.inner_enabled = 1;
        inner_method[0].params.path_smooth.outer_enabled = 0;
        inner_request.process.methods = inner_method;
        inner_request.process.method_count = 1;

        dg_default_process_method(&outer_method[0], DG_PROCESS_METHOD_PATH_SMOOTH);
        outer_method[0].params.path_smooth.strength = 2;
        outer_method[0].params.path_smooth.inner_enabled = 0;
        outer_method[0].params.path_smooth.outer_enabled = 1;
        outer_request.process.methods = outer_method;
        outer_request.process.method_count = 1;

        dg_default_process_method(&combined_method[0], DG_PROCESS_METHOD_PATH_SMOOTH);
        combined_method[0].params.path_smooth.strength = 2;
        combined_method[0].params.path_smooth.inner_enabled = 1;
        combined_method[0].params.path_smooth.outer_enabled = 1;
        combined_request.process.methods = combined_method;
        combined_request.process.method_count = 1;

        ASSERT_STATUS(dg_generate(&inner_request, &inner_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&outer_request, &outer_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&combined_request, &combined_map), DG_STATUS_OK);

        ASSERT_TRUE(
            combined_map.metadata.connected_component_count <=
            inner_map.metadata.connected_component_count
        );
        ASSERT_TRUE(
            combined_map.metadata.connected_component_count <=
            outer_map.metadata.connected_component_count
        );

        if (!maps_have_same_tiles(&combined_map, &inner_map) &&
            !maps_have_same_tiles(&combined_map, &outer_map)) {
            found_combined_behavior = true;
        }

        dg_map_destroy(&inner_map);
        dg_map_destroy(&outer_map);
        dg_map_destroy(&combined_map);

        if (found_combined_behavior) {
            break;
        }
    }

    ASSERT_TRUE(found_combined_behavior);
    return 0;
}

static int test_post_process_path_smoothing_outer_strength_effect(void)
{
    dg_generate_request_t weak_request;
    dg_generate_request_t strong_request;
    dg_map_t weak_map = {0};
    dg_map_t strong_map = {0};
    dg_process_method_t weak_methods[2];
    dg_process_method_t strong_methods[2];

    dg_default_generate_request(&weak_request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, 5619u);
    weak_request.params.rooms_and_mazes.min_rooms = 10;
    weak_request.params.rooms_and_mazes.max_rooms = 16;
    weak_request.params.rooms_and_mazes.room_min_size = 4;
    weak_request.params.rooms_and_mazes.room_max_size = 10;
    weak_request.params.rooms_and_mazes.dead_end_prune_steps = 0;
    strong_request = weak_request;

    /*
     * Stage 1 establishes shared inner bridges.
     * Stage 2 isolates outer trim strength behavior.
     */
    dg_default_process_method(&weak_methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
    weak_methods[0].params.path_smooth.strength = 2;
    weak_methods[0].params.path_smooth.inner_enabled = 1;
    weak_methods[0].params.path_smooth.outer_enabled = 0;
    dg_default_process_method(&weak_methods[1], DG_PROCESS_METHOD_PATH_SMOOTH);
    weak_methods[1].params.path_smooth.strength = 1;
    weak_methods[1].params.path_smooth.inner_enabled = 0;
    weak_methods[1].params.path_smooth.outer_enabled = 1;
    weak_request.process.methods = weak_methods;
    weak_request.process.method_count = 2;

    dg_default_process_method(&strong_methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
    strong_methods[0].params.path_smooth.strength = 2;
    strong_methods[0].params.path_smooth.inner_enabled = 1;
    strong_methods[0].params.path_smooth.outer_enabled = 0;
    dg_default_process_method(&strong_methods[1], DG_PROCESS_METHOD_PATH_SMOOTH);
    strong_methods[1].params.path_smooth.strength = 4;
    strong_methods[1].params.path_smooth.inner_enabled = 0;
    strong_methods[1].params.path_smooth.outer_enabled = 1;
    strong_request.process.methods = strong_methods;
    strong_request.process.method_count = 2;

    ASSERT_STATUS(dg_generate(&weak_request, &weak_map), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&strong_request, &strong_map), DG_STATUS_OK);

    ASSERT_TRUE(
        strong_map.metadata.connected_component_count <=
        weak_map.metadata.connected_component_count
    );
    ASSERT_TRUE(
        strong_map.metadata.walkable_tile_count <=
        weak_map.metadata.walkable_tile_count
    );

    dg_map_destroy(&weak_map);
    dg_map_destroy(&strong_map);
    return 0;
}

static int test_post_process_path_smoothing_skips_room_connected_ends(void)
{
    uint64_t seed;
    bool found_protected_candidate;

    found_protected_candidate = false;
    for (seed = 6200u; seed < 6400u; ++seed) {
        dg_generate_request_t base_request;
        dg_generate_request_t smooth_request;
        dg_map_t base_map = {0};
        dg_map_t smooth_map = {0};
        dg_process_method_t smooth_method[1];
        int protected_candidates;
        int x;
        int y;

        dg_default_generate_request(&base_request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        base_request.params.rooms_and_mazes.min_rooms = 10;
        base_request.params.rooms_and_mazes.max_rooms = 16;
        base_request.params.rooms_and_mazes.room_min_size = 4;
        base_request.params.rooms_and_mazes.room_max_size = 10;
        base_request.params.rooms_and_mazes.dead_end_prune_steps = 0;

        smooth_request = base_request;
        dg_default_process_method(&smooth_method[0], DG_PROCESS_METHOD_PATH_SMOOTH);
        smooth_method[0].params.path_smooth.strength = 4;
        smooth_method[0].params.path_smooth.inner_enabled = 1;
        smooth_method[0].params.path_smooth.outer_enabled = 1;
        smooth_request.process.methods = smooth_method;
        smooth_request.process.method_count = 1;

        ASSERT_STATUS(dg_generate(&base_request, &base_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&smooth_request, &smooth_map), DG_STATUS_OK);

        protected_candidates = 0;
        for (y = 1; y < base_map.height - 1; ++y) {
            for (x = 1; x < base_map.width - 1; ++x) {
                int leg_a_x;
                int leg_a_y;
                int leg_b_x;
                int leg_b_y;
                bool n;
                bool e;
                bool s;
                bool w;
                bool is_corner;
                size_t index;

                index = (size_t)y * (size_t)base_map.width + (size_t)x;
                n = corridor_floor(&base_map, x, y - 1);
                e = corridor_floor(&base_map, x + 1, y);
                s = corridor_floor(&base_map, x, y + 1);
                w = corridor_floor(&base_map, x - 1, y);

                /*
                 * Inner smoothing candidate: wall corner fill.
                 */
                if (base_map.tiles[index] == DG_TILE_WALL &&
                    !point_in_any_room(&base_map, x, y)) {
                    is_corner = true;
                    if (n && e && !s && !w) {
                        leg_a_x = x;
                        leg_a_y = y - 1;
                        leg_b_x = x + 1;
                        leg_b_y = y;
                    } else if (e && s && !n && !w) {
                        leg_a_x = x + 1;
                        leg_a_y = y;
                        leg_b_x = x;
                        leg_b_y = y + 1;
                    } else if (s && w && !n && !e) {
                        leg_a_x = x;
                        leg_a_y = y + 1;
                        leg_b_x = x - 1;
                        leg_b_y = y;
                    } else if (w && n && !s && !e) {
                        leg_a_x = x - 1;
                        leg_a_y = y;
                        leg_b_x = x;
                        leg_b_y = y - 1;
                    } else {
                        is_corner = false;
                    }

                    if (is_corner &&
                        (corridor_touches_room(&base_map, leg_a_x, leg_a_y) ||
                         corridor_touches_room(&base_map, leg_b_x, leg_b_y))) {
                        protected_candidates += 1;
                        ASSERT_TRUE(smooth_map.tiles[index] == DG_TILE_WALL);
                    }
                }

                /*
                 * Outer smoothing candidate: corridor corner trim.
                 */
                if (!corridor_floor(&base_map, x, y)) {
                    continue;
                }

                is_corner = true;
                if (n && e && !s && !w) {
                    leg_a_x = x;
                    leg_a_y = y - 1;
                    leg_b_x = x + 1;
                    leg_b_y = y;
                } else if (e && s && !n && !w) {
                    leg_a_x = x + 1;
                    leg_a_y = y;
                    leg_b_x = x;
                    leg_b_y = y + 1;
                } else if (s && w && !n && !e) {
                    leg_a_x = x;
                    leg_a_y = y + 1;
                    leg_b_x = x - 1;
                    leg_b_y = y;
                } else if (w && n && !s && !e) {
                    leg_a_x = x - 1;
                    leg_a_y = y;
                    leg_b_x = x;
                    leg_b_y = y - 1;
                } else {
                    is_corner = false;
                }

                if (is_corner &&
                    (corridor_touches_room(&base_map, x, y) ||
                     corridor_touches_room(&base_map, leg_a_x, leg_a_y) ||
                     corridor_touches_room(&base_map, leg_b_x, leg_b_y))) {
                    protected_candidates += 1;
                    ASSERT_TRUE(is_walkable(smooth_map.tiles[index]));
                }
            }
        }

        dg_map_destroy(&base_map);
        dg_map_destroy(&smooth_map);

        if (protected_candidates > 0) {
            found_protected_candidate = true;
            break;
        }
    }

    ASSERT_TRUE(found_protected_candidate);
    return 0;
}

static int test_post_process_corridor_roughen_changes_layout(void)
{
    uint64_t seed;
    bool found_difference;

    found_difference = false;
    for (seed = 7300u; seed < 7380u; ++seed) {
        dg_generate_request_t base_request;
        dg_generate_request_t rough_request;
        dg_map_t base_map = {0};
        dg_map_t rough_map = {0};
        dg_process_method_t rough_methods[1];

        dg_default_generate_request(&base_request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        base_request.params.rooms_and_mazes.min_rooms = 10;
        base_request.params.rooms_and_mazes.max_rooms = 16;
        base_request.params.rooms_and_mazes.room_min_size = 4;
        base_request.params.rooms_and_mazes.room_max_size = 10;
        base_request.params.rooms_and_mazes.dead_end_prune_steps = 0;

        rough_request = base_request;
        dg_default_process_method(&rough_methods[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
        rough_methods[0].params.corridor_roughen.strength = 55;
        rough_methods[0].params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_ORGANIC;
        rough_request.process.methods = rough_methods;
        rough_request.process.method_count = 1;

        ASSERT_STATUS(dg_generate(&base_request, &base_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&rough_request, &rough_map), DG_STATUS_OK);

        ASSERT_TRUE(rough_map.metadata.walkable_tile_count >= base_map.metadata.walkable_tile_count);

        if (!maps_have_same_tiles(&base_map, &rough_map)) {
            found_difference = true;
        }

        dg_map_destroy(&base_map);
        dg_map_destroy(&rough_map);

        if (found_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_difference);
    return 0;
}

static int test_post_process_corridor_roughen_mode_affects_layout(void)
{
    uint64_t seed;
    bool found_mode_difference;

    found_mode_difference = false;
    for (seed = 7400u; seed < 7480u; ++seed) {
        dg_generate_request_t uniform_request;
        dg_generate_request_t organic_request;
        dg_map_t uniform_map = {0};
        dg_map_t organic_map = {0};
        dg_process_method_t uniform_methods[1];
        dg_process_method_t organic_methods[1];

        dg_default_generate_request(&uniform_request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        uniform_request.params.rooms_and_mazes.min_rooms = 10;
        uniform_request.params.rooms_and_mazes.max_rooms = 16;
        uniform_request.params.rooms_and_mazes.room_min_size = 4;
        uniform_request.params.rooms_and_mazes.room_max_size = 10;
        uniform_request.params.rooms_and_mazes.dead_end_prune_steps = 0;
        organic_request = uniform_request;

        dg_default_process_method(&uniform_methods[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
        uniform_methods[0].params.corridor_roughen.strength = 45;
        uniform_methods[0].params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_UNIFORM;
        uniform_request.process.methods = uniform_methods;
        uniform_request.process.method_count = 1;

        dg_default_process_method(&organic_methods[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
        organic_methods[0].params.corridor_roughen.strength = 45;
        organic_methods[0].params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_ORGANIC;
        organic_request.process.methods = organic_methods;
        organic_request.process.method_count = 1;

        ASSERT_STATUS(dg_generate(&uniform_request, &uniform_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&organic_request, &organic_map), DG_STATUS_OK);

        if (!maps_have_same_tiles(&uniform_map, &organic_map)) {
            found_mode_difference = true;
        }

        dg_map_destroy(&uniform_map);
        dg_map_destroy(&organic_map);

        if (found_mode_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_mode_difference);
    return 0;
}

static int test_post_process_corridor_roughen_depth_affects_layout(void)
{
    uint64_t seed;
    bool found_depth_difference;

    found_depth_difference = false;
    for (seed = 7500u; seed < 7600u; ++seed) {
        dg_generate_request_t shallow_request;
        dg_generate_request_t deep_request;
        dg_map_t shallow_map = {0};
        dg_map_t deep_map = {0};
        dg_process_method_t shallow_methods[1];
        dg_process_method_t deep_methods[1];

        dg_default_generate_request(&shallow_request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, seed);
        shallow_request.params.rooms_and_mazes.min_rooms = 10;
        shallow_request.params.rooms_and_mazes.max_rooms = 16;
        shallow_request.params.rooms_and_mazes.room_min_size = 4;
        shallow_request.params.rooms_and_mazes.room_max_size = 10;
        shallow_request.params.rooms_and_mazes.dead_end_prune_steps = 0;
        deep_request = shallow_request;

        dg_default_process_method(&shallow_methods[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
        shallow_methods[0].params.corridor_roughen.strength = 35;
        shallow_methods[0].params.corridor_roughen.max_depth = 1;
        shallow_methods[0].params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_ORGANIC;
        shallow_request.process.methods = shallow_methods;
        shallow_request.process.method_count = 1;

        dg_default_process_method(&deep_methods[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
        deep_methods[0].params.corridor_roughen.strength = 35;
        deep_methods[0].params.corridor_roughen.max_depth = 4;
        deep_methods[0].params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_ORGANIC;
        deep_request.process.methods = deep_methods;
        deep_request.process.method_count = 1;

        ASSERT_STATUS(dg_generate(&shallow_request, &shallow_map), DG_STATUS_OK);
        ASSERT_STATUS(dg_generate(&deep_request, &deep_map), DG_STATUS_OK);

        ASSERT_TRUE(deep_map.metadata.walkable_tile_count >= shallow_map.metadata.walkable_tile_count);
        if (!maps_have_same_tiles(&shallow_map, &deep_map)) {
            found_depth_difference = true;
        }

        dg_map_destroy(&shallow_map);
        dg_map_destroy(&deep_map);

        if (found_depth_difference) {
            break;
        }
    }

    ASSERT_TRUE(found_depth_difference);
    return 0;
}

static int test_generation_diagnostics_populated(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_process_method_t methods[2];
    dg_room_type_definition_t definitions[2];
    size_t assigned_from_quotas;
    size_t i;

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, 424242u);
    request.params.rooms_and_mazes.min_rooms = 10;
    request.params.rooms_and_mazes.max_rooms = 18;
    request.params.rooms_and_mazes.room_min_size = 4;
    request.params.rooms_and_mazes.room_max_size = 10;
    request.params.rooms_and_mazes.dead_end_prune_steps = 0;

    dg_default_process_method(&methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
    methods[0].params.path_smooth.strength = 2;
    methods[0].params.path_smooth.inner_enabled = 1;
    methods[0].params.path_smooth.outer_enabled = 1;
    dg_default_process_method(&methods[1], DG_PROCESS_METHOD_SCALE);
    methods[1].params.scale.factor = 2;
    request.process.methods = methods;
    request.process.method_count = 2;

    dg_default_room_type_definition(&definitions[0], 100u);
    definitions[0].enabled = 1;
    definitions[0].min_count = 1;
    definitions[0].preferences.larger_room_bias = 30;
    dg_default_room_type_definition(&definitions[1], 200u);
    definitions[1].enabled = 1;
    definitions[1].min_count = 1;
    definitions[1].preferences.higher_degree_bias = 30;
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.allow_untyped_rooms = 1;
    request.room_types.policy.default_type_id = 100u;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    ASSERT_TRUE(map.metadata.diagnostics.process_step_count == request.process.method_count);
    for (i = 0; i < map.metadata.diagnostics.process_step_count; ++i) {
        const dg_process_step_diagnostics_t *step = &map.metadata.diagnostics.process_steps[i];
        ASSERT_TRUE(step->method_type == (int)request.process.methods[i].type);
        ASSERT_TRUE(step->walkable_before > 0);
        ASSERT_TRUE(step->walkable_after > 0);
        ASSERT_TRUE(step->connected_before == 0 || step->connected_before == 1);
        ASSERT_TRUE(step->connected_after == 0 || step->connected_after == 1);
    }

    ASSERT_TRUE(
        map.metadata.diagnostics.typed_room_count + map.metadata.diagnostics.untyped_room_count ==
        map.metadata.room_count
    );
    ASSERT_TRUE(map.metadata.diagnostics.room_type_count == request.room_types.definition_count);

    assigned_from_quotas = 0;
    for (i = 0; i < map.metadata.diagnostics.room_type_count; ++i) {
        const dg_room_type_quota_diagnostics_t *quota = &map.metadata.diagnostics.room_type_quotas[i];
        assigned_from_quotas += quota->assigned_count;
        ASSERT_TRUE(quota->type_id == definitions[i].type_id);
    }
    ASSERT_TRUE(assigned_from_quotas == map.metadata.diagnostics.typed_room_count);

    dg_map_destroy(&map);
    return 0;
}

static int test_generation_request_snapshot_populated(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definitions[2];
    dg_edge_opening_spec_t edge_openings[2];
    dg_process_method_t process_methods[3];
    const dg_generation_request_snapshot_t *snapshot;

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, 515151u);
    request.params.rooms_and_mazes.min_rooms = 11;
    request.params.rooms_and_mazes.max_rooms = 16;
    request.params.rooms_and_mazes.room_min_size = 5;
    request.params.rooms_and_mazes.room_max_size = 9;
    request.params.rooms_and_mazes.maze_wiggle_percent = 25;
    request.params.rooms_and_mazes.min_room_connections = 1;
    request.params.rooms_and_mazes.max_room_connections = 2;
    request.params.rooms_and_mazes.ensure_full_connectivity = 0;
    request.params.rooms_and_mazes.dead_end_prune_steps = 8;
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_SCALE);
    process_methods[0].params.scale.factor = 2;
    dg_default_process_method(&process_methods[1], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[1].params.path_smooth.strength = 3;
    process_methods[1].params.path_smooth.inner_enabled = 1;
    process_methods[1].params.path_smooth.outer_enabled = 1;
    dg_default_process_method(&process_methods[2], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
    process_methods[2].params.corridor_roughen.strength = 42;
    process_methods[2].params.corridor_roughen.max_depth = 3;
    process_methods[2].params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_ORGANIC;
    request.process.methods = process_methods;
    request.process.method_count = 3;

    dg_default_room_type_definition(&definitions[0], 701u);
    definitions[0].min_count = 2;
    definitions[0].preferences.higher_degree_bias = 20;
    definitions[0].template_map_path[0] = '\0';
    definitions[0].template_opening_query.side_mask = DG_MAP_EDGE_MASK_TOP | DG_MAP_EDGE_MASK_BOTTOM;
    definitions[0].template_opening_query.role_mask = DG_MAP_EDGE_OPENING_ROLE_MASK_ENTRANCE;
    definitions[0].template_opening_query.edge_coord_min = 0;
    definitions[0].template_opening_query.edge_coord_max = 32;
    definitions[0].template_opening_query.min_length = 1;
    definitions[0].template_opening_query.max_length = -1;
    definitions[0].template_opening_query.require_component = -1;
    definitions[0].template_required_opening_matches = 1;
    dg_default_room_type_definition(&definitions[1], 702u);
    definitions[1].min_count = 1;
    definitions[1].preferences.border_distance_bias = 35;
    definitions[1].template_map_path[0] = '\0';
    definitions[1].template_opening_query.side_mask = DG_MAP_EDGE_MASK_ALL;
    definitions[1].template_opening_query.role_mask = DG_MAP_EDGE_OPENING_ROLE_MASK_ANY;
    definitions[1].template_opening_query.edge_coord_min = 0;
    definitions[1].template_opening_query.edge_coord_max = 999;
    definitions[1].template_opening_query.min_length = 1;
    definitions[1].template_opening_query.max_length = -1;
    definitions[1].template_opening_query.require_component = -1;
    definitions[1].template_required_opening_matches = 2;

    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.strict_mode = 1;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 701u;
    edge_openings[0].side = DG_MAP_EDGE_TOP;
    edge_openings[0].start = 4;
    edge_openings[0].end = 8;
    edge_openings[0].role = DG_MAP_EDGE_OPENING_ROLE_ENTRANCE;
    edge_openings[1].side = DG_MAP_EDGE_BOTTOM;
    edge_openings[1].start = 12;
    edge_openings[1].end = 14;
    edge_openings[1].role = DG_MAP_EDGE_OPENING_ROLE_EXIT;
    request.edge_openings.openings = edge_openings;
    request.edge_openings.opening_count = 2;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    snapshot = &map.metadata.generation_request;
    ASSERT_TRUE(snapshot->present == 1);
    ASSERT_TRUE(snapshot->width == request.width);
    ASSERT_TRUE(snapshot->height == request.height);
    ASSERT_TRUE(snapshot->seed == request.seed);
    ASSERT_TRUE(snapshot->algorithm_id == (int)request.algorithm);
    ASSERT_TRUE(snapshot->process.enabled == request.process.enabled);
    ASSERT_TRUE(snapshot->process.method_count == request.process.method_count);
    ASSERT_TRUE(snapshot->process.methods != NULL);
    ASSERT_TRUE(snapshot->process.methods[0].type == (int)DG_PROCESS_METHOD_SCALE);
    ASSERT_TRUE(snapshot->process.methods[0].params.scale.factor == 2);
    ASSERT_TRUE(snapshot->process.methods[1].type == (int)DG_PROCESS_METHOD_PATH_SMOOTH);
    ASSERT_TRUE(snapshot->process.methods[1].params.path_smooth.strength == 3);
    ASSERT_TRUE(snapshot->process.methods[1].params.path_smooth.inner_enabled == 1);
    ASSERT_TRUE(snapshot->process.methods[1].params.path_smooth.outer_enabled == 1);
    ASSERT_TRUE(snapshot->process.methods[2].type == (int)DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
    ASSERT_TRUE(snapshot->process.methods[2].params.corridor_roughen.strength == 42);
    ASSERT_TRUE(snapshot->process.methods[2].params.corridor_roughen.max_depth == 3);
    ASSERT_TRUE(snapshot->process.methods[2].params.corridor_roughen.mode ==
                (int)DG_CORRIDOR_ROUGHEN_ORGANIC);
    ASSERT_TRUE(snapshot->edge_openings.opening_count == 2u);
    ASSERT_TRUE(snapshot->edge_openings.openings != NULL);
    ASSERT_TRUE(snapshot->edge_openings.openings[0].side == (int)DG_MAP_EDGE_TOP);
    ASSERT_TRUE(snapshot->edge_openings.openings[0].start == 4);
    ASSERT_TRUE(snapshot->edge_openings.openings[0].end == 8);
    ASSERT_TRUE(snapshot->edge_openings.openings[0].role == (int)DG_MAP_EDGE_OPENING_ROLE_ENTRANCE);
    ASSERT_TRUE(snapshot->edge_openings.openings[1].side == (int)DG_MAP_EDGE_BOTTOM);
    ASSERT_TRUE(snapshot->edge_openings.openings[1].start == 12);
    ASSERT_TRUE(snapshot->edge_openings.openings[1].end == 14);
    ASSERT_TRUE(snapshot->edge_openings.openings[1].role == (int)DG_MAP_EDGE_OPENING_ROLE_EXIT);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.min_rooms == request.params.rooms_and_mazes.min_rooms);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.max_rooms == request.params.rooms_and_mazes.max_rooms);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.room_min_size ==
                request.params.rooms_and_mazes.room_min_size);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.room_max_size ==
                request.params.rooms_and_mazes.room_max_size);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.maze_wiggle_percent ==
                request.params.rooms_and_mazes.maze_wiggle_percent);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.min_room_connections ==
                request.params.rooms_and_mazes.min_room_connections);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.max_room_connections ==
                request.params.rooms_and_mazes.max_room_connections);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.ensure_full_connectivity ==
                request.params.rooms_and_mazes.ensure_full_connectivity);
    ASSERT_TRUE(snapshot->params.rooms_and_mazes.dead_end_prune_steps ==
                request.params.rooms_and_mazes.dead_end_prune_steps);

    ASSERT_TRUE(snapshot->room_types.definition_count == request.room_types.definition_count);
    ASSERT_TRUE(snapshot->room_types.policy.strict_mode == request.room_types.policy.strict_mode);
    ASSERT_TRUE(snapshot->room_types.policy.allow_untyped_rooms ==
                request.room_types.policy.allow_untyped_rooms);
    ASSERT_TRUE(snapshot->room_types.policy.default_type_id ==
                request.room_types.policy.default_type_id);
    ASSERT_TRUE(snapshot->room_types.definitions != NULL);
    ASSERT_TRUE(snapshot->room_types.definitions[0].type_id == definitions[0].type_id);
    ASSERT_TRUE(snapshot->room_types.definitions[0].min_count == definitions[0].min_count);
    ASSERT_TRUE(strcmp(
        snapshot->room_types.definitions[0].template_map_path,
        definitions[0].template_map_path
    ) == 0);
    ASSERT_TRUE(
        snapshot->room_types.definitions[0].template_required_opening_matches ==
        definitions[0].template_required_opening_matches
    );
    ASSERT_TRUE(snapshot->room_types.definitions[1].type_id == definitions[1].type_id);
    ASSERT_TRUE(snapshot->room_types.definitions[1].min_count == definitions[1].min_count);
    ASSERT_TRUE(strcmp(
        snapshot->room_types.definitions[1].template_map_path,
        definitions[1].template_map_path
    ) == 0);

    dg_map_destroy(&map);
    return 0;
}

static int test_generation_request_snapshot_populated_value_noise(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    const dg_generation_request_snapshot_t *snapshot;

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 84, 52, 454545u);
    request.params.value_noise.feature_size = 9;
    request.params.value_noise.octaves = 4;
    request.params.value_noise.persistence_percent = 58;
    request.params.value_noise.floor_threshold_percent = 44;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);

    snapshot = &map.metadata.generation_request;
    ASSERT_TRUE(snapshot->present == 1);
    ASSERT_TRUE(snapshot->algorithm_id == (int)DG_ALGORITHM_VALUE_NOISE);
    ASSERT_TRUE(snapshot->process.enabled == request.process.enabled);
    ASSERT_TRUE(snapshot->params.value_noise.feature_size == request.params.value_noise.feature_size);
    ASSERT_TRUE(snapshot->params.value_noise.octaves == request.params.value_noise.octaves);
    ASSERT_TRUE(snapshot->params.value_noise.persistence_percent ==
                request.params.value_noise.persistence_percent);
    ASSERT_TRUE(snapshot->params.value_noise.floor_threshold_percent ==
                request.params.value_noise.floor_threshold_percent);

    dg_map_destroy(&map);
    return 0;
}

static int test_map_serialization_roundtrip(void)
{
    const char *path;
    dg_generate_request_t request;
    dg_map_t original = {0};
    dg_map_t loaded = {0};
    dg_room_type_definition_t definitions[2];
    dg_edge_opening_spec_t edge_openings[2];
    dg_process_method_t process_methods[3];

    path = "dungeoneer_test_roundtrip.dgmap";

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 6060u);
    request.params.bsp.min_rooms = 9;
    request.params.bsp.max_rooms = 12;
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_SCALE);
    process_methods[0].params.scale.factor = 2;
    dg_default_process_method(&process_methods[1], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[1].params.path_smooth.strength = 2;
    process_methods[1].params.path_smooth.inner_enabled = 1;
    process_methods[1].params.path_smooth.outer_enabled = 1;
    dg_default_process_method(&process_methods[2], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
    process_methods[2].params.corridor_roughen.strength = 38;
    process_methods[2].params.corridor_roughen.max_depth = 3;
    process_methods[2].params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_ORGANIC;
    request.process.methods = process_methods;
    request.process.method_count = 3;
    dg_default_room_type_definition(&definitions[0], 51u);
    definitions[0].min_count = 2;
    definitions[0].template_map_path[0] = '\0';
    definitions[0].template_required_opening_matches = 1;
    dg_default_room_type_definition(&definitions[1], 52u);
    definitions[1].min_count = 2;
    definitions[1].template_map_path[0] = '\0';
    definitions[1].template_required_opening_matches = 2;
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.strict_mode = 1;
    edge_openings[0].side = DG_MAP_EDGE_TOP;
    edge_openings[0].start = 6;
    edge_openings[0].end = 8;
    edge_openings[0].role = DG_MAP_EDGE_OPENING_ROLE_ENTRANCE;
    edge_openings[1].side = DG_MAP_EDGE_BOTTOM;
    edge_openings[1].start = 20;
    edge_openings[1].end = 22;
    edge_openings[1].role = DG_MAP_EDGE_OPENING_ROLE_EXIT;
    request.edge_openings.openings = edge_openings;
    request.edge_openings.opening_count = 2u;

    ASSERT_STATUS(dg_generate(&request, &original), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_save_file(&original, path), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_load_file(path, &loaded), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&original, &loaded));
    ASSERT_TRUE(maps_have_same_metadata(&original, &loaded));

    dg_map_destroy(&original);
    dg_map_destroy(&loaded);
    (void)remove(path);
    return 0;
}

static int test_map_serialization_roundtrip_post_process_disabled(void)
{
    const char *path;
    dg_generate_request_t request;
    dg_map_t original = {0};
    dg_map_t loaded = {0};
    dg_process_method_t process_methods[2];

    path = "dungeoneer_test_roundtrip_post_disabled.dgmap";

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 6262u);
    request.params.bsp.min_rooms = 9;
    request.params.bsp.max_rooms = 12;
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_SCALE);
    process_methods[0].params.scale.factor = 3;
    dg_default_process_method(&process_methods[1], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[1].params.path_smooth.strength = 2;
    process_methods[1].params.path_smooth.inner_enabled = 1;
    process_methods[1].params.path_smooth.outer_enabled = 1;
    request.process.enabled = 0;
    request.process.methods = process_methods;
    request.process.method_count = 2;

    ASSERT_STATUS(dg_generate(&request, &original), DG_STATUS_OK);
    ASSERT_TRUE(original.metadata.generation_request.process.enabled == 0);
    ASSERT_TRUE(original.metadata.generation_request.process.method_count == 2);
    ASSERT_TRUE(original.metadata.diagnostics.process_step_count == 0);

    ASSERT_STATUS(dg_map_save_file(&original, path), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_load_file(path, &loaded), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&original, &loaded));
    ASSERT_TRUE(maps_have_same_metadata(&original, &loaded));
    ASSERT_TRUE(loaded.metadata.generation_request.process.enabled == 0);
    ASSERT_TRUE(loaded.metadata.generation_request.process.method_count == 2);
    ASSERT_TRUE(loaded.metadata.diagnostics.process_step_count == 0);

    dg_map_destroy(&original);
    dg_map_destroy(&loaded);
    (void)remove(path);
    return 0;
}

static int test_map_serialization_roundtrip_value_noise(void)
{
    const char *path;
    dg_generate_request_t request;
    dg_map_t original = {0};
    dg_map_t loaded = {0};
    dg_process_method_t process_methods[2];

    path = "dungeoneer_test_roundtrip_value_noise.dgmap";

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 88, 48, 6161u);
    request.params.value_noise.feature_size = 11;
    request.params.value_noise.octaves = 4;
    request.params.value_noise.persistence_percent = 60;
    request.params.value_noise.floor_threshold_percent = 46;
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_SCALE);
    process_methods[0].params.scale.factor = 2;
    dg_default_process_method(&process_methods[1], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
    process_methods[1].params.corridor_roughen.strength = 35;
    process_methods[1].params.corridor_roughen.max_depth = 2;
    process_methods[1].params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_UNIFORM;
    request.process.methods = process_methods;
    request.process.method_count = 2;

    ASSERT_STATUS(dg_generate(&request, &original), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_save_file(&original, path), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_load_file(path, &loaded), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&original, &loaded));
    ASSERT_TRUE(maps_have_same_metadata(&original, &loaded));

    dg_map_destroy(&original);
    dg_map_destroy(&loaded);
    (void)remove(path);
    return 0;
}

static int test_map_load_rejects_invalid_magic(void)
{
    const char *path;
    FILE *file;
    dg_map_t loaded = {0};
    const char bad_data[] = "NOT_DGMP";

    path = "dungeoneer_test_bad_magic.dgmap";
    file = fopen(path, "wb");
    ASSERT_TRUE(file != NULL);
    ASSERT_TRUE(fwrite(bad_data, 1, sizeof(bad_data), file) == sizeof(bad_data));
    ASSERT_TRUE(fclose(file) == 0);

    ASSERT_STATUS(dg_map_load_file(path, &loaded), DG_STATUS_UNSUPPORTED_FORMAT);
    (void)remove(path);
    return 0;
}

static int test_map_export_png_json(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definitions[3];
    const char *png_path = "dungeoneer_test_export.png";
    const char *json_path = "dungeoneer_test_export.json";
    FILE *file;
    unsigned char signature[8];
    size_t read_count;
    char *json_text;
    long file_size;

    static const unsigned char expected_png_signature[8] = {
        137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u
    };

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 88, 48, 424200u);
    request.params.rooms_and_mazes.min_rooms = 10;
    request.params.rooms_and_mazes.max_rooms = 14;
    request.params.rooms_and_mazes.room_min_size = 4;
    request.params.rooms_and_mazes.room_max_size = 10;
    request.params.rooms_and_mazes.dead_end_prune_steps = 6;
    dg_default_room_type_definition(&definitions[0], 610u);
    definitions[0].min_count = 1;
    definitions[0].preferences.weight = 3;
    dg_default_room_type_definition(&definitions[1], 620u);
    definitions[1].min_count = 1;
    definitions[1].preferences.weight = 2;
    definitions[1].preferences.higher_degree_bias = 30;
    dg_default_room_type_definition(&definitions[2], 630u);
    definitions[2].min_count = 1;
    definitions[2].preferences.weight = 2;
    definitions[2].preferences.border_distance_bias = 40;
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 3;
    request.room_types.policy.strict_mode = 0;
    request.room_types.policy.allow_untyped_rooms = 1;
    request.room_types.policy.default_type_id = 610u;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_export_png_json(&map, png_path, json_path), DG_STATUS_OK);

    file = fopen(png_path, "rb");
    ASSERT_TRUE(file != NULL);
    read_count = fread(signature, 1, sizeof(signature), file);
    ASSERT_TRUE(fclose(file) == 0);
    ASSERT_TRUE(read_count == sizeof(signature));
    ASSERT_TRUE(memcmp(signature, expected_png_signature, sizeof(signature)) == 0);

    file = fopen(json_path, "rb");
    ASSERT_TRUE(file != NULL);
    ASSERT_TRUE(fseek(file, 0, SEEK_END) == 0);
    file_size = ftell(file);
    ASSERT_TRUE(file_size >= 0);
    ASSERT_TRUE(fseek(file, 0, SEEK_SET) == 0);

    json_text = (char *)malloc((size_t)file_size + 1u);
    ASSERT_TRUE(json_text != NULL);
    read_count = fread(json_text, 1, (size_t)file_size, file);
    ASSERT_TRUE(fclose(file) == 0);
    ASSERT_TRUE(read_count == (size_t)file_size);
    json_text[file_size] = '\0';

    ASSERT_TRUE(strstr(json_text, "\"format\": \"dungeoneer_png_json_v1\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"legend\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"room_type_palette\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"configured_room_types\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"metadata\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"rooms\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"corridors\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"edge_openings\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"generation_request\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"typed_room_count\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"edge_opening_count\"") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"type_id\": 610") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"type_id\": 620") != NULL);
    ASSERT_TRUE(strstr(json_text, "\"type_id\": 630") != NULL);

    free(json_text);
    dg_map_destroy(&map);
    (void)remove(png_path);
    (void)remove(json_path);
    return 0;
}

static int test_room_type_config_scaffold(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definitions[2];

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 8080u);
    ASSERT_TRUE(request.room_types.definitions == NULL);
    ASSERT_TRUE(request.room_types.definition_count == 0);
    ASSERT_TRUE(request.room_types.policy.strict_mode == 0);
    ASSERT_TRUE(request.room_types.policy.allow_untyped_rooms == 1);
    ASSERT_TRUE(request.room_types.policy.default_type_id == 0u);

    dg_default_room_type_definition(&definitions[0], 10u);
    definitions[0].min_count = 1;
    definitions[0].target_count = 2;

    dg_default_room_type_definition(&definitions[1], 20u);
    definitions[1].preferences.weight = 3;
    definitions[1].constraints.area_min = 12;
    definitions[1].constraints.area_max = 150;

    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 10u;
    request.params.bsp.min_rooms = 8;
    request.params.bsp.max_rooms = 12;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
    ASSERT_TRUE(map.metadata.room_count > 0);
    ASSERT_TRUE(count_rooms_with_assigned_type(&map) == map.metadata.room_count);
    ASSERT_TRUE(count_rooms_with_type_id(&map, 10u) >= 1);

    dg_map_destroy(&map);
    return 0;
}

static int test_room_type_assignment_determinism(void)
{
    dg_generate_request_t request;
    dg_map_t a = {0};
    dg_map_t b = {0};
    dg_room_type_definition_t definitions[2];

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 7007u);
    request.params.bsp.min_rooms = 10;
    request.params.bsp.max_rooms = 10;

    dg_default_room_type_definition(&definitions[0], 100u);
    definitions[0].preferences.weight = 3;
    definitions[0].preferences.higher_degree_bias = 40;
    definitions[0].min_count = 2;

    dg_default_room_type_definition(&definitions[1], 200u);
    definitions[1].preferences.weight = 2;
    definitions[1].preferences.border_distance_bias = 35;
    definitions[1].min_count = 2;

    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.strict_mode = 1;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 100u;

    ASSERT_STATUS(dg_generate(&request, &a), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&request, &b), DG_STATUS_OK);

    ASSERT_TRUE(maps_have_same_tiles(&a, &b));
    ASSERT_TRUE(maps_have_same_metadata(&a, &b));

    dg_map_destroy(&a);
    dg_map_destroy(&b);
    return 0;
}

static int test_room_type_assignment_stable_across_post_process(void)
{
    dg_generate_request_t base_request;
    dg_generate_request_t no_roughen_request;
    dg_generate_request_t roughen_request;
    dg_map_t no_roughen_map = {0};
    dg_map_t roughen_map = {0};
    dg_room_type_definition_t definitions[2];
    dg_process_method_t roughen_method[1];

    dg_default_generate_request(&base_request, DG_ALGORITHM_BSP_TREE, 88, 48, 700701u);
    base_request.params.bsp.min_rooms = 12;
    base_request.params.bsp.max_rooms = 12;
    base_request.params.bsp.room_min_size = 4;
    base_request.params.bsp.room_max_size = 10;

    dg_default_room_type_definition(&definitions[0], 901u);
    dg_default_room_type_definition(&definitions[1], 902u);

    definitions[0].min_count = 0;
    definitions[0].max_count = -1;
    definitions[0].target_count = -1;
    definitions[0].preferences.weight = 1;
    definitions[0].preferences.larger_room_bias = 0;
    definitions[0].preferences.higher_degree_bias = 0;
    definitions[0].preferences.border_distance_bias = 0;

    definitions[1].min_count = 0;
    definitions[1].max_count = -1;
    definitions[1].target_count = -1;
    definitions[1].preferences.weight = 1;
    definitions[1].preferences.larger_room_bias = 0;
    definitions[1].preferences.higher_degree_bias = 0;
    definitions[1].preferences.border_distance_bias = 0;

    base_request.room_types.definitions = definitions;
    base_request.room_types.definition_count = 2;
    base_request.room_types.policy.strict_mode = 0;
    base_request.room_types.policy.allow_untyped_rooms = 0;
    base_request.room_types.policy.default_type_id = 901u;

    no_roughen_request = base_request;

    roughen_request = base_request;
    dg_default_process_method(&roughen_method[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
    roughen_method[0].params.corridor_roughen.mode = DG_CORRIDOR_ROUGHEN_UNIFORM;
    roughen_method[0].params.corridor_roughen.strength = 55;
    roughen_method[0].params.corridor_roughen.max_depth = 4;
    roughen_request.process.methods = roughen_method;
    roughen_request.process.method_count = 1;

    ASSERT_STATUS(dg_generate(&no_roughen_request, &no_roughen_map), DG_STATUS_OK);
    ASSERT_STATUS(dg_generate(&roughen_request, &roughen_map), DG_STATUS_OK);

    ASSERT_TRUE(no_roughen_map.metadata.room_count == roughen_map.metadata.room_count);
    ASSERT_TRUE(count_rooms_with_assigned_type(&no_roughen_map) == no_roughen_map.metadata.room_count);
    ASSERT_TRUE(count_rooms_with_assigned_type(&roughen_map) == roughen_map.metadata.room_count);
    ASSERT_TRUE(room_types_match_by_room_id(&no_roughen_map, &roughen_map));

    dg_map_destroy(&no_roughen_map);
    dg_map_destroy(&roughen_map);
    return 0;
}

static int test_room_type_template_map_application(void)
{
    const char *template_path;
    dg_generate_request_t template_request;
    dg_generate_request_t request;
    dg_map_t template_map = {0};
    dg_map_t map = {0};
    dg_room_type_definition_t definition;

    template_path = "dungeoneer_test_room_template.dgmap";

    dg_default_generate_request(&template_request, DG_ALGORITHM_VALUE_NOISE, 40, 28, 424201u);
    template_request.params.value_noise.feature_size = 8;
    template_request.params.value_noise.octaves = 3;
    template_request.params.value_noise.persistence_percent = 55;
    template_request.params.value_noise.floor_threshold_percent = 35;
    ASSERT_STATUS(dg_generate(&template_request, &template_map), DG_STATUS_OK);
    ASSERT_STATUS(dg_map_save_file(&template_map, template_path), DG_STATUS_OK);
    dg_map_destroy(&template_map);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 424202u);
    request.params.bsp.min_rooms = 10;
    request.params.bsp.max_rooms = 12;
    request.params.bsp.room_min_size = 4;
    request.params.bsp.room_max_size = 10;

    dg_default_room_type_definition(&definition, 501u);
    definition.min_count = 1;
    (void)snprintf(
        definition.template_map_path,
        sizeof(definition.template_map_path),
        "%s",
        template_path
    );
    definition.template_required_opening_matches = 0;

    request.room_types.definitions = &definition;
    request.room_types.definition_count = 1u;
    request.room_types.policy.strict_mode = 0;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 501u;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
    ASSERT_TRUE(count_rooms_with_type_id(&map, 501u) > 0u);
    ASSERT_TRUE(count_wall_tiles_inside_rooms(&map) > 0u);

    dg_map_destroy(&map);
    (void)remove(template_path);
    return 0;
}

static int test_room_type_assignment_minimums(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definitions[2];

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 88, 48, 8123u);
    request.params.bsp.min_rooms = 12;
    request.params.bsp.max_rooms = 12;

    dg_default_room_type_definition(&definitions[0], 31u);
    definitions[0].min_count = 3;
    definitions[0].preferences.larger_room_bias = 30;

    dg_default_room_type_definition(&definitions[1], 32u);
    definitions[1].min_count = 4;
    definitions[1].preferences.higher_degree_bias = 25;

    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    request.room_types.policy.strict_mode = 1;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_OK);
    ASSERT_TRUE(count_rooms_with_type_id(&map, 31u) >= 3);
    ASSERT_TRUE(count_rooms_with_type_id(&map, 32u) >= 4);

    dg_map_destroy(&map);
    return 0;
}

static int test_room_type_strict_minimum_infeasible(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definition;

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 9001u);
    dg_default_room_type_definition(&definition, 7u);
    definition.min_count = 1;
    definition.constraints.area_min = 1000000;
    definition.constraints.area_max = -1;

    request.room_types.definitions = &definition;
    request.room_types.definition_count = 1;
    request.room_types.policy.strict_mode = 1;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_GENERATION_FAILED);
    return 0;
}

static int test_room_type_strict_requires_full_coverage(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definition;

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 9002u);
    dg_default_room_type_definition(&definition, 11u);
    definition.min_count = 0;
    definition.constraints.degree_min = 100;
    definition.constraints.degree_max = -1;

    request.room_types.definitions = &definition;
    request.room_types.definition_count = 1;
    request.room_types.policy.strict_mode = 1;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 11u;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_GENERATION_FAILED);
    return 0;
}

static int test_invalid_generate_request(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};
    dg_room_type_definition_t definitions[2];
    dg_process_method_t process_methods[4];

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 7, 48, 1u);
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.params.bsp.min_rooms = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.params.bsp.min_rooms = 10;
    request.params.bsp.max_rooms = 9;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.params.bsp.room_min_size = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.params.bsp.room_min_size = 8;
    request.params.bsp.room_max_size = 4;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_DRUNKARDS_WALK, 80, 48, 1u);
    request.params.drunkards_walk.wiggle_percent = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_DRUNKARDS_WALK, 80, 48, 1u);
    request.params.drunkards_walk.wiggle_percent = 101;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_CELLULAR_AUTOMATA, 80, 48, 1u);
    request.params.cellular_automata.initial_wall_percent = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_CELLULAR_AUTOMATA, 80, 48, 1u);
    request.params.cellular_automata.initial_wall_percent = 101;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_CELLULAR_AUTOMATA, 80, 48, 1u);
    request.params.cellular_automata.simulation_steps = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_CELLULAR_AUTOMATA, 80, 48, 1u);
    request.params.cellular_automata.simulation_steps = 13;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_CELLULAR_AUTOMATA, 80, 48, 1u);
    request.params.cellular_automata.wall_threshold = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_CELLULAR_AUTOMATA, 80, 48, 1u);
    request.params.cellular_automata.wall_threshold = 9;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 80, 48, 1u);
    request.params.value_noise.feature_size = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 80, 48, 1u);
    request.params.value_noise.feature_size = 65;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 80, 48, 1u);
    request.params.value_noise.octaves = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 80, 48, 1u);
    request.params.value_noise.octaves = 7;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 80, 48, 1u);
    request.params.value_noise.persistence_percent = 9;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 80, 48, 1u);
    request.params.value_noise.persistence_percent = 91;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 80, 48, 1u);
    request.params.value_noise.floor_threshold_percent = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_VALUE_NOISE, 80, 48, 1u);
    request.params.value_noise.floor_threshold_percent = 101;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.min_rooms = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.min_rooms = 10;
    request.params.rooms_and_mazes.max_rooms = 9;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.room_min_size = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.room_min_size = 9;
    request.params.rooms_and_mazes.room_max_size = 8;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.maze_wiggle_percent = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.maze_wiggle_percent = 101;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.min_room_connections = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.min_room_connections = 3;
    request.params.rooms_and_mazes.max_room_connections = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.ensure_full_connectivity = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOMS_AND_MAZES, 80, 48, 1u);
    request.params.rooms_and_mazes.dead_end_prune_steps = -2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOM_GRAPH, 80, 48, 1u);
    request.params.room_graph.min_rooms = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOM_GRAPH, 80, 48, 1u);
    request.params.room_graph.min_rooms = 10;
    request.params.room_graph.max_rooms = 9;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOM_GRAPH, 80, 48, 1u);
    request.params.room_graph.room_min_size = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOM_GRAPH, 80, 48, 1u);
    request.params.room_graph.room_min_size = 9;
    request.params.room_graph.room_max_size = 8;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOM_GRAPH, 80, 48, 1u);
    request.params.room_graph.neighbor_candidates = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOM_GRAPH, 80, 48, 1u);
    request.params.room_graph.neighbor_candidates = 9;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOM_GRAPH, 80, 48, 1u);
    request.params.room_graph.extra_connection_chance_percent = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_ROOM_GRAPH, 80, 48, 1u);
    request.params.room_graph.extra_connection_chance_percent = 101;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.worm_count = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.worm_count = 129;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.wiggle_percent = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.wiggle_percent = 101;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.branch_chance_percent = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.branch_chance_percent = 101;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.target_floor_percent = 4;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.target_floor_percent = 91;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.brush_radius = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.brush_radius = 4;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.max_steps_per_worm = 7;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.max_steps_per_worm = 20001;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_WORM_CAVES, 80, 48, 1u);
    request.params.worm_caves.ensure_connected = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 80, 48, 1u);
    request.params.simplex_noise.feature_size = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 80, 48, 1u);
    request.params.simplex_noise.feature_size = 129;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 80, 48, 1u);
    request.params.simplex_noise.octaves = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 80, 48, 1u);
    request.params.simplex_noise.octaves = 9;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 80, 48, 1u);
    request.params.simplex_noise.persistence_percent = 9;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 80, 48, 1u);
    request.params.simplex_noise.persistence_percent = 91;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 80, 48, 1u);
    request.params.simplex_noise.floor_threshold_percent = -1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 80, 48, 1u);
    request.params.simplex_noise.floor_threshold_percent = 101;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_SIMPLEX_NOISE, 80, 48, 1u);
    request.params.simplex_noise.ensure_connected = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.room_types.definition_count = 1;
    request.room_types.definitions = NULL;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.process.enabled = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.process.method_count = 1;
    request.process.methods = NULL;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_SCALE);
    process_methods[0].params.scale.factor = 0;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[0].params.path_smooth.strength = -1;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[0].params.path_smooth.strength = 13;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[0].params.path_smooth.inner_enabled = 2;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_PATH_SMOOTH);
    process_methods[0].params.path_smooth.outer_enabled = 2;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
    process_methods[0].params.corridor_roughen.strength = -1;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
    process_methods[0].params.corridor_roughen.strength = 101;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
    process_methods[0].params.corridor_roughen.mode = (dg_corridor_roughen_mode_t)99;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
    process_methods[0].params.corridor_roughen.max_depth = 0;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_process_method(&process_methods[0], DG_PROCESS_METHOD_CORRIDOR_ROUGHEN);
    process_methods[0].params.corridor_roughen.max_depth = 33;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    process_methods[0].type = (dg_process_method_type_t)99;
    request.process.methods = process_methods;
    request.process.method_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.room_types.policy.strict_mode = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.room_types.policy.allow_untyped_rooms = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.room_types.policy.allow_untyped_rooms = 0;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_room_type_definition(&definitions[0], 3u);
    dg_default_room_type_definition(&definitions[1], 3u);
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 2;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_room_type_definition(&definitions[0], 3u);
    definitions[0].enabled = 2;
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_room_type_definition(&definitions[0], 3u);
    definitions[0].constraints.area_min = 20;
    definitions[0].constraints.area_max = 10;
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 1;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_room_type_definition(&definitions[0], 3u);
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 1;
    request.room_types.policy.allow_untyped_rooms = 0;
    request.room_types.policy.default_type_id = 42u;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    request.edge_openings.opening_count = 1u;
    request.edge_openings.openings = NULL;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    {
        dg_edge_opening_spec_t opening = {
            (dg_map_edge_side_t)99,
            0,
            0,
            DG_MAP_EDGE_OPENING_ROLE_NONE
        };
        request.edge_openings.openings = &opening;
        request.edge_openings.opening_count = 1u;
        ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);
    }

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    {
        dg_edge_opening_spec_t opening = {
            DG_MAP_EDGE_TOP,
            0,
            200,
            DG_MAP_EDGE_OPENING_ROLE_NONE
        };
        request.edge_openings.openings = &opening;
        request.edge_openings.opening_count = 1u;
        ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);
    }

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_room_type_definition(&definitions[0], 88u);
    definitions[0].template_required_opening_matches = -1;
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 1u;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 80, 48, 1u);
    dg_default_room_type_definition(&definitions[0], 89u);
    definitions[0].template_opening_query.edge_coord_min = 10;
    definitions[0].template_opening_query.edge_coord_max = 2;
    request.room_types.definitions = definitions;
    request.room_types.definition_count = 1u;
    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_INVALID_ARGUMENT);

    return 0;
}

static int test_bsp_generation_failure_for_tiny_map(void)
{
    dg_generate_request_t request;
    dg_map_t map = {0};

    dg_default_generate_request(&request, DG_ALGORITHM_BSP_TREE, 16, 16, 99u);
    request.params.bsp.min_rooms = 6;
    request.params.bsp.max_rooms = 8;
    request.params.bsp.room_min_size = 10;
    request.params.bsp.room_max_size = 12;

    ASSERT_STATUS(dg_generate(&request, &map), DG_STATUS_GENERATION_FAILED);
    return 0;
}

int main(void)
{
    size_t i;
    int failures;

    struct test_case {
        const char *name;
        int (*run)(void);
    };

    const struct test_case tests[] = {
        {"map_basics", test_map_basics},
        {"map_edge_opening_query_filters", test_map_edge_opening_query_filters},
        {"rng_reproducibility", test_rng_reproducibility},
        {"bsp_generation", test_bsp_generation},
        {"explicit_edge_openings_are_applied", test_explicit_edge_openings_are_applied},
        {"bsp_determinism", test_bsp_determinism},
        {"drunkards_walk_generation", test_drunkards_walk_generation},
        {"drunkards_walk_determinism", test_drunkards_walk_determinism},
        {"drunkards_wiggle_affects_layout", test_drunkards_wiggle_affects_layout},
        {"cellular_automata_generation", test_cellular_automata_generation},
        {"cellular_automata_determinism", test_cellular_automata_determinism},
        {"cellular_automata_threshold_affects_layout",
         test_cellular_automata_threshold_affects_layout},
        {"value_noise_generation", test_value_noise_generation},
        {"value_noise_determinism", test_value_noise_determinism},
        {"value_noise_threshold_affects_layout", test_value_noise_threshold_affects_layout},
        {"room_graph_generation", test_room_graph_generation},
        {"room_graph_determinism", test_room_graph_determinism},
        {"room_graph_loop_chance_affects_layout", test_room_graph_loop_chance_affects_layout},
        {"worm_caves_generation", test_worm_caves_generation},
        {"worm_caves_determinism", test_worm_caves_determinism},
        {"worm_caves_branch_affects_layout", test_worm_caves_branch_affects_layout},
        {"simplex_noise_generation", test_simplex_noise_generation},
        {"simplex_noise_determinism", test_simplex_noise_determinism},
        {"simplex_noise_threshold_affects_layout", test_simplex_noise_threshold_affects_layout},
        {"rooms_and_mazes_generation", test_rooms_and_mazes_generation},
        {"rooms_and_mazes_determinism", test_rooms_and_mazes_determinism},
        {"rooms_and_mazes_pruning_control", test_rooms_and_mazes_pruning_control},
        {"rooms_and_mazes_wiggle_affects_layout", test_rooms_and_mazes_wiggle_affects_layout},
        {"rooms_and_mazes_unpruned_has_no_isolated_seed_tiles",
         test_rooms_and_mazes_unpruned_has_no_isolated_seed_tiles},
        {"rooms_and_mazes_pruned_has_no_room_nub_dead_ends",
         test_rooms_and_mazes_pruned_has_no_room_nub_dead_ends},
        {"post_process_scaling", test_post_process_scaling},
        {"post_process_disabled_bypasses_pipeline", test_post_process_disabled_bypasses_pipeline},
        {"post_process_path_smoothing_changes_layout", test_post_process_path_smoothing_changes_layout},
        {"post_process_path_smoothing_outer_trim_effect",
         test_post_process_path_smoothing_outer_trim_effect},
        {"post_process_path_smoothing_combined_modes",
         test_post_process_path_smoothing_combined_modes},
        {"post_process_path_smoothing_outer_strength_effect",
         test_post_process_path_smoothing_outer_strength_effect},
        {"post_process_path_smoothing_skips_room_connected_ends",
         test_post_process_path_smoothing_skips_room_connected_ends},
        {"post_process_corridor_roughen_changes_layout",
         test_post_process_corridor_roughen_changes_layout},
        {"post_process_corridor_roughen_mode_affects_layout",
         test_post_process_corridor_roughen_mode_affects_layout},
        {"post_process_corridor_roughen_depth_affects_layout",
         test_post_process_corridor_roughen_depth_affects_layout},
        {"generation_diagnostics_populated", test_generation_diagnostics_populated},
        {"generation_request_snapshot_populated", test_generation_request_snapshot_populated},
        {"generation_request_snapshot_populated_value_noise",
         test_generation_request_snapshot_populated_value_noise},
        {"map_serialization_roundtrip", test_map_serialization_roundtrip},
        {"map_serialization_roundtrip_post_process_disabled",
         test_map_serialization_roundtrip_post_process_disabled},
        {"map_serialization_roundtrip_value_noise",
         test_map_serialization_roundtrip_value_noise},
        {"map_load_rejects_invalid_magic", test_map_load_rejects_invalid_magic},
        {"map_export_png_json", test_map_export_png_json},
        {"room_type_config_scaffold", test_room_type_config_scaffold},
        {"room_type_assignment_determinism", test_room_type_assignment_determinism},
        {"room_type_assignment_stable_across_post_process",
         test_room_type_assignment_stable_across_post_process},
        {"room_type_template_map_application", test_room_type_template_map_application},
        {"room_type_assignment_minimums", test_room_type_assignment_minimums},
        {"room_type_strict_minimum_infeasible", test_room_type_strict_minimum_infeasible},
        {"room_type_strict_requires_full_coverage", test_room_type_strict_requires_full_coverage},
        {"invalid_generate_request", test_invalid_generate_request},
        {"bsp_generation_failure_for_tiny_map", test_bsp_generation_failure_for_tiny_map},
    };

    failures = 0;
    for (i = 0; i < (sizeof(tests) / sizeof(tests[0])); ++i) {
        int result = tests[i].run();
        if (result == 0) {
            fprintf(stdout, "[PASS] %s\n", tests[i].name);
        } else {
            fprintf(stderr, "[FAIL] %s\n", tests[i].name);
            failures += 1;
        }
    }

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    fprintf(stdout, "%zu test(s) passed\n", sizeof(tests) / sizeof(tests[0]));
    return 0;
}
