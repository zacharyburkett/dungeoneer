#include "dungeoneer/dungeoneer.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program_name)
{
    fprintf(
        stderr,
        "Usage: %s [rooms|organic] [width] [height] [seed]\n",
        program_name
    );
}

static char tile_glyph(dg_tile_t tile)
{
    switch (tile) {
    case DG_TILE_WALL:
        return '#';
    case DG_TILE_FLOOR:
        return '.';
    case DG_TILE_DOOR:
        return '+';
    case DG_TILE_VOID:
    default:
        return ' ';
    }
}

static void print_map(const dg_map_t *map)
{
    int x;
    int y;

    for (y = 0; y < map->height; ++y) {
        for (x = 0; x < map->width; ++x) {
            putchar(tile_glyph(dg_map_get_tile(map, x, y)));
        }
        putchar('\n');
    }
}

static dg_room_flags_t demo_classify_room(int room_index, const dg_rect_t *bounds, void *user_data)
{
    int interval;
    (void)bounds;

    interval = 4;
    if (user_data != NULL) {
        interval = *((const int *)user_data);
    }

    return ((room_index + 1) % interval == 0) ? DG_ROOM_FLAG_SPECIAL : DG_ROOM_FLAG_NONE;
}

int main(int argc, char **argv)
{
    const char *mode;
    int width;
    int height;
    uint64_t seed;
    dg_algorithm_t algorithm;
    dg_generate_request_t request;
    dg_map_t map;
    dg_status_t status;
    size_t i;
    size_t special_rooms;
    int special_interval;
    size_t total_tiles;
    float floor_coverage;
    double average_room_degree;

    mode = (argc > 1) ? argv[1] : "rooms";
    width = (argc > 2) ? atoi(argv[2]) : 80;
    height = (argc > 3) ? atoi(argv[3]) : 40;
    seed = (argc > 4) ? (uint64_t)strtoull(argv[4], NULL, 10) : 1337u;

    if (strcmp(mode, "rooms") == 0) {
        algorithm = DG_ALGORITHM_ROOMS_AND_CORRIDORS;
    } else if (strcmp(mode, "organic") == 0) {
        algorithm = DG_ALGORITHM_ORGANIC_CAVE;
    } else {
        print_usage(argv[0]);
        return 2;
    }

    if (width < 5 || height < 5) {
        fprintf(stderr, "width and height must both be >= 5\n");
        return 2;
    }

    dg_default_generate_request(&request, algorithm, width, height, seed);
    request.constraints.max_generation_attempts = 3;
    if (algorithm == DG_ALGORITHM_ROOMS_AND_CORRIDORS) {
        special_interval = 4;
        request.params.rooms.classify_room = demo_classify_room;
        request.params.rooms.classify_room_user_data = &special_interval;
        request.constraints.min_room_count = 4;
    }

    map = (dg_map_t){0};

    status = dg_generate(&request, &map);
    if (status != DG_STATUS_OK) {
        fprintf(stderr, "generation failed: %s\n", dg_status_string(status));
        return 1;
    }

    print_map(&map);

    special_rooms = 0;
    for (i = 0; i < map.metadata.room_count; ++i) {
        if ((map.metadata.rooms[i].flags & DG_ROOM_FLAG_SPECIAL) != 0) {
            special_rooms += 1;
        }
    }

    total_tiles = (size_t)map.width * (size_t)map.height;
    floor_coverage = (float)map.metadata.walkable_tile_count / (float)total_tiles;
    average_room_degree = 0.0;
    if (map.metadata.room_count > 0) {
        average_room_degree = (double)map.metadata.room_neighbor_count / (double)map.metadata.room_count;
    }

    fprintf(stdout, "\n");
    fprintf(stdout, "algorithm: %s\n", mode);
    fprintf(stdout, "size: %dx%d\n", map.width, map.height);
    fprintf(stdout, "seed: %" PRIu64 " (actual: %" PRIu64 ")\n", seed, map.metadata.seed);
    fprintf(stdout, "attempts: %zu\n", map.metadata.generation_attempts);
    fprintf(stdout, "rooms: %zu (special: %zu)\n", map.metadata.room_count, special_rooms);
    fprintf(stdout, "rooms (leaf): %zu\n", map.metadata.leaf_room_count);
    fprintf(stdout, "corridors: %zu (total length: %zu)\n",
            map.metadata.corridor_count,
            map.metadata.corridor_total_length);
    fprintf(stdout, "room adjacency entries: %zu (avg degree: %.2f)\n",
            map.metadata.room_neighbor_count,
            average_room_degree);
    fprintf(stdout, "walkable tiles: %zu (coverage: %.2f%%)\n",
            map.metadata.walkable_tile_count, (double)(floor_coverage * 100.0f));
    fprintf(stdout, "components: %zu (largest: %zu, connected: %s)\n",
            map.metadata.connected_component_count,
            map.metadata.largest_component_size,
            map.metadata.connected_floor ? "yes" : "no");

    dg_map_destroy(&map);
    return 0;
}
