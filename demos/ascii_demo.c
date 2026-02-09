#include "dungeoneer/dungeoneer.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(const char *program_name)
{
    fprintf(stderr, "Usage: %s [width] [height] [seed]\n", program_name);
}

static bool point_in_room(const dg_room_metadata_t *room, int x, int y)
{
    if (room == NULL) {
        return false;
    }

    return x >= room->bounds.x &&
           y >= room->bounds.y &&
           x < room->bounds.x + room->bounds.width &&
           y < room->bounds.y + room->bounds.height;
}

static const dg_room_metadata_t *find_room_at(const dg_map_t *map, int x, int y)
{
    size_t i;

    if (map == NULL) {
        return NULL;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map->metadata.rooms[i];
        if (point_in_room(room, x, y)) {
            return room;
        }
    }

    return NULL;
}

static char room_glyph(const dg_room_metadata_t *room)
{
    static const char glyphs[] =
        "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    size_t glyph_count;

    if (room == NULL) {
        return '.';
    }

    glyph_count = sizeof(glyphs) - 1;
    return glyphs[(size_t)room->id % glyph_count];
}

static char map_glyph_at(const dg_map_t *map, int x, int y)
{
    dg_tile_t tile;
    const dg_room_metadata_t *room;

    tile = dg_map_get_tile(map, x, y);
    if (tile == DG_TILE_FLOOR) {
        room = find_room_at(map, x, y);
        if (room != NULL) {
            return room_glyph(room);
        }
        return '.';
    }

    return ' ';
}

static void print_map(const dg_map_t *map)
{
    int x;
    int y;

    for (y = 0; y < map->height; ++y) {
        for (x = 0; x < map->width; ++x) {
            putchar(map_glyph_at(map, x, y));
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    int width;
    int height;
    uint64_t seed;
    dg_generate_request_t request;
    dg_map_t map;
    dg_status_t status;
    size_t total_tiles;
    float floor_coverage;
    double average_room_degree;

    width = (argc > 1) ? atoi(argv[1]) : 80;
    height = (argc > 2) ? atoi(argv[2]) : 40;
    seed = (argc > 3) ? (uint64_t)strtoull(argv[3], NULL, 10) : 1337u;

    if (width < 8 || height < 8) {
        print_usage(argv[0]);
        fprintf(stderr, "width and height must both be >= 8\n");
        return 2;
    }

    dg_default_generate_request(&request, width, height, seed);

    map = (dg_map_t){0};
    status = dg_generate(&request, &map);
    if (status != DG_STATUS_OK) {
        fprintf(stderr, "generation failed: %s\n", dg_status_string(status));
        return 1;
    }

    print_map(&map);

    total_tiles = (size_t)map.width * (size_t)map.height;
    floor_coverage = (float)map.metadata.walkable_tile_count / (float)total_tiles;
    average_room_degree = 0.0;
    if (map.metadata.room_count > 0) {
        average_room_degree = (double)map.metadata.room_neighbor_count / (double)map.metadata.room_count;
    }

    fprintf(stdout, "\n");
    fprintf(stdout, "algorithm: bsp_tree\n");
    fprintf(stdout, "size: %dx%d\n", map.width, map.height);
    fprintf(stdout, "seed: %" PRIu64 "\n", map.metadata.seed);
    fprintf(stdout, "rooms: %zu\n", map.metadata.room_count);
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
