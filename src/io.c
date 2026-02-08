#include "dungeoneer/io.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char DG_MAP_MAGIC[4] = {'D', 'G', 'M', 'P'};
static const uint32_t DG_MAP_FORMAT_VERSION = 1u;

static bool dg_mul_size_would_overflow(size_t a, size_t b, size_t *out)
{
    if (out == NULL) {
        return true;
    }

    if (a == 0 || b == 0) {
        *out = 0;
        return false;
    }

    if (a > (SIZE_MAX / b)) {
        return true;
    }

    *out = a * b;
    return false;
}

static bool dg_map_is_empty(const dg_map_t *map)
{
    if (map == NULL) {
        return false;
    }

    return map->tiles == NULL &&
           map->metadata.rooms == NULL &&
           map->metadata.corridors == NULL &&
           map->metadata.room_adjacency == NULL &&
           map->metadata.room_neighbors == NULL;
}

static dg_status_t dg_write_exact(FILE *file, const void *data, size_t byte_count)
{
    if (file == NULL || (byte_count > 0 && data == NULL)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (byte_count == 0) {
        return DG_STATUS_OK;
    }

    if (fwrite(data, 1, byte_count, file) != byte_count) {
        return DG_STATUS_IO_ERROR;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_read_exact(FILE *file, void *data, size_t byte_count)
{
    if (file == NULL || (byte_count > 0 && data == NULL)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (byte_count == 0) {
        return DG_STATUS_OK;
    }

    if (fread(data, 1, byte_count, file) != byte_count) {
        return DG_STATUS_IO_ERROR;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_write_u8(FILE *file, uint8_t value)
{
    return dg_write_exact(file, &value, sizeof(value));
}

static dg_status_t dg_write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24) & 0xFFu);
    return dg_write_exact(file, bytes, sizeof(bytes));
}

static dg_status_t dg_write_i32(FILE *file, int32_t value)
{
    return dg_write_u32(file, (uint32_t)value);
}

static dg_status_t dg_write_u64(FILE *file, uint64_t value)
{
    unsigned char bytes[8];

    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24) & 0xFFu);
    bytes[4] = (unsigned char)((value >> 32) & 0xFFu);
    bytes[5] = (unsigned char)((value >> 40) & 0xFFu);
    bytes[6] = (unsigned char)((value >> 48) & 0xFFu);
    bytes[7] = (unsigned char)((value >> 56) & 0xFFu);
    return dg_write_exact(file, bytes, sizeof(bytes));
}

static dg_status_t dg_read_u8(FILE *file, uint8_t *out_value)
{
    return dg_read_exact(file, out_value, sizeof(*out_value));
}

static dg_status_t dg_read_u32(FILE *file, uint32_t *out_value)
{
    unsigned char bytes[4];
    dg_status_t status;

    status = dg_read_exact(file, bytes, sizeof(bytes));
    if (status != DG_STATUS_OK) {
        return status;
    }

    *out_value = ((uint32_t)bytes[0]) |
                 (((uint32_t)bytes[1]) << 8) |
                 (((uint32_t)bytes[2]) << 16) |
                 (((uint32_t)bytes[3]) << 24);
    return DG_STATUS_OK;
}

static dg_status_t dg_read_i32(FILE *file, int32_t *out_value)
{
    uint32_t raw;
    dg_status_t status;

    status = dg_read_u32(file, &raw);
    if (status != DG_STATUS_OK) {
        return status;
    }

    *out_value = (int32_t)raw;
    return DG_STATUS_OK;
}

static dg_status_t dg_read_u64(FILE *file, uint64_t *out_value)
{
    unsigned char bytes[8];
    dg_status_t status;

    status = dg_read_exact(file, bytes, sizeof(bytes));
    if (status != DG_STATUS_OK) {
        return status;
    }

    *out_value = ((uint64_t)bytes[0]) |
                 (((uint64_t)bytes[1]) << 8) |
                 (((uint64_t)bytes[2]) << 16) |
                 (((uint64_t)bytes[3]) << 24) |
                 (((uint64_t)bytes[4]) << 32) |
                 (((uint64_t)bytes[5]) << 40) |
                 (((uint64_t)bytes[6]) << 48) |
                 (((uint64_t)bytes[7]) << 56);
    return DG_STATUS_OK;
}

static bool dg_tile_value_is_valid(uint8_t value)
{
    return value == (uint8_t)DG_TILE_VOID ||
           value == (uint8_t)DG_TILE_WALL ||
           value == (uint8_t)DG_TILE_FLOOR ||
           value == (uint8_t)DG_TILE_DOOR;
}

static bool dg_room_role_value_is_valid(int32_t value)
{
    return value >= (int32_t)DG_ROOM_ROLE_NONE &&
           value <= (int32_t)DG_ROOM_ROLE_SHOP;
}

static bool dg_u64_to_size_checked(uint64_t value, size_t *out_value)
{
    if (out_value == NULL) {
        return false;
    }

    if (value > (uint64_t)SIZE_MAX) {
        return false;
    }

    *out_value = (size_t)value;
    return true;
}

static bool dg_i32_to_int_checked(int32_t value, int *out_value)
{
    if (out_value == NULL) {
        return false;
    }

    if (value > INT_MAX || value < INT_MIN) {
        return false;
    }

    *out_value = (int)value;
    return true;
}

static dg_status_t dg_read_size_from_u64(FILE *file, size_t *out_value)
{
    uint64_t raw_value;
    dg_status_t status;

    if (out_value == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_read_u64(file, &raw_value);
    if (status != DG_STATUS_OK) {
        return status;
    }

    if (!dg_u64_to_size_checked(raw_value, out_value)) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_read_int_from_i32(FILE *file, int *out_value)
{
    int32_t raw_value;
    dg_status_t status;

    if (out_value == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_read_i32(file, &raw_value);
    if (status != DG_STATUS_OK) {
        return status;
    }

    if (!dg_i32_to_int_checked(raw_value, out_value)) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_validate_map_for_save(const dg_map_t *map)
{
    if (map == NULL || map->tiles == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->width <= 0 || map->height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->width > INT32_MAX || map->height > INT32_MAX) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if ((long long)map->width * (long long)map->height <= 0) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (
        map->metadata.room_count > 0 && map->metadata.rooms == NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (
        map->metadata.corridor_count > 0 && map->metadata.corridors == NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (
        map->metadata.room_adjacency_count > 0 && map->metadata.room_adjacency == NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (
        map->metadata.room_neighbor_count > 0 && map->metadata.room_neighbors == NULL
    ) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

dg_status_t dg_map_save_file(const dg_map_t *map, const char *path)
{
    FILE *file;
    dg_status_t status;
    size_t tile_count;
    size_t i;

    if (path == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    status = dg_validate_map_for_save(map);
    if (status != DG_STATUS_OK) {
        return status;
    }

    if (dg_mul_size_would_overflow((size_t)map->width, (size_t)map->height, &tile_count)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        return DG_STATUS_IO_ERROR;
    }

    status = dg_write_exact(file, DG_MAP_MAGIC, sizeof(DG_MAP_MAGIC));
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_write_u32(file, DG_MAP_FORMAT_VERSION);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_write_u32(file, (uint32_t)map->width);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_write_u32(file, (uint32_t)map->height);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_write_u64(file, (uint64_t)tile_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_write_u64(file, map->metadata.seed);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_write_i32(file, (int32_t)map->metadata.algorithm_id);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_write_u64(file, (uint64_t)map->metadata.generation_attempts);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_write_u8(file, map->metadata.connected_floor ? 1u : 0u);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_write_u64(file, (uint64_t)map->metadata.room_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.corridor_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.room_adjacency_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.room_neighbor_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_write_u64(file, (uint64_t)map->metadata.walkable_tile_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.wall_tile_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.special_room_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.entrance_room_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.exit_room_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.boss_room_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.treasure_room_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.shop_room_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.leaf_room_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.corridor_total_length);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_i32(file, (int32_t)map->metadata.entrance_exit_distance);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.connected_component_count);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_write_u64(file, (uint64_t)map->metadata.largest_component_size);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    for (i = 0; i < tile_count; ++i) {
        status = dg_write_u8(file, (uint8_t)map->tiles[i]);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map->metadata.rooms[i];
        status = dg_write_i32(file, (int32_t)room->id);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_i32(file, (int32_t)room->bounds.x);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_i32(file, (int32_t)room->bounds.y);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_i32(file, (int32_t)room->bounds.width);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_i32(file, (int32_t)room->bounds.height);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_u32(file, (uint32_t)room->flags);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_i32(file, (int32_t)room->role);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
    }

    for (i = 0; i < map->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];
        status = dg_write_i32(file, (int32_t)corridor->from_room_id);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_i32(file, (int32_t)corridor->to_room_id);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_i32(file, (int32_t)corridor->width);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_i32(file, (int32_t)corridor->length);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
    }

    for (i = 0; i < map->metadata.room_adjacency_count; ++i) {
        const dg_room_adjacency_span_t *span = &map->metadata.room_adjacency[i];
        status = dg_write_u64(file, (uint64_t)span->start_index);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_u64(file, (uint64_t)span->count);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
    }

    for (i = 0; i < map->metadata.room_neighbor_count; ++i) {
        const dg_room_neighbor_t *neighbor = &map->metadata.room_neighbors[i];
        status = dg_write_i32(file, (int32_t)neighbor->room_id);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
        status = dg_write_i32(file, (int32_t)neighbor->corridor_index);
        if (status != DG_STATUS_OK) {
            fclose(file);
            return status;
        }
    }

    if (fclose(file) != 0) {
        return DG_STATUS_IO_ERROR;
    }
    return DG_STATUS_OK;
}

dg_status_t dg_map_load_file(const char *path, dg_map_t *out_map)
{
    FILE *file;
    unsigned char magic[sizeof(DG_MAP_MAGIC)];
    uint32_t version;
    uint32_t width_u32;
    uint32_t height_u32;
    uint64_t tile_count_u64;
    size_t tile_count;
    uint64_t room_count_u64;
    uint64_t corridor_count_u64;
    uint64_t room_adjacency_count_u64;
    uint64_t room_neighbor_count_u64;
    dg_map_t loaded;
    dg_status_t status;
    size_t i;

    if (path == NULL || out_map == NULL || !dg_map_is_empty(out_map)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return DG_STATUS_IO_ERROR;
    }

    memset(&loaded, 0, sizeof(loaded));

    status = dg_read_exact(file, magic, sizeof(magic));
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    if (memcmp(magic, DG_MAP_MAGIC, sizeof(DG_MAP_MAGIC)) != 0) {
        fclose(file);
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    status = dg_read_u32(file, &version);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    if (version != DG_MAP_FORMAT_VERSION) {
        fclose(file);
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    status = dg_read_u32(file, &width_u32);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_read_u32(file, &height_u32);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }
    status = dg_read_u64(file, &tile_count_u64);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    if (width_u32 < 1u || height_u32 < 1u || width_u32 > (uint32_t)INT_MAX || height_u32 > (uint32_t)INT_MAX) {
        fclose(file);
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    if (dg_mul_size_would_overflow((size_t)width_u32, (size_t)height_u32, &tile_count)) {
        fclose(file);
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    if ((uint64_t)tile_count != tile_count_u64) {
        fclose(file);
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    status = dg_map_init(&loaded, (int)width_u32, (int)height_u32, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        fclose(file);
        return status;
    }

    status = dg_read_u64(file, &loaded.metadata.seed);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    {
        int32_t algorithm_id_i32;
        status = dg_read_i32(file, &algorithm_id_i32);
        if (status != DG_STATUS_OK) {
            dg_map_destroy(&loaded);
            fclose(file);
            return status;
        }
        if (!dg_i32_to_int_checked(algorithm_id_i32, &loaded.metadata.algorithm_id)) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
    }

    {
        uint64_t generation_attempts_u64;
        uint8_t connected_floor_u8;
        status = dg_read_u64(file, &generation_attempts_u64);
        if (status != DG_STATUS_OK) {
            dg_map_destroy(&loaded);
            fclose(file);
            return status;
        }
        status = dg_read_u8(file, &connected_floor_u8);
        if (status != DG_STATUS_OK) {
            dg_map_destroy(&loaded);
            fclose(file);
            return status;
        }
        if (!dg_u64_to_size_checked(generation_attempts_u64, &loaded.metadata.generation_attempts)) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
        loaded.metadata.connected_floor = connected_floor_u8 != 0u;
    }

    status = dg_read_u64(file, &room_count_u64);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_u64(file, &corridor_count_u64);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_u64(file, &room_adjacency_count_u64);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_u64(file, &room_neighbor_count_u64);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }

    if (
        room_count_u64 > (uint64_t)SIZE_MAX ||
        corridor_count_u64 > (uint64_t)SIZE_MAX ||
        room_adjacency_count_u64 > (uint64_t)SIZE_MAX ||
        room_neighbor_count_u64 > (uint64_t)SIZE_MAX
    ) {
        dg_map_destroy(&loaded);
        fclose(file);
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    loaded.metadata.room_count = (size_t)room_count_u64;
    loaded.metadata.room_capacity = loaded.metadata.room_count;
    loaded.metadata.corridor_count = (size_t)corridor_count_u64;
    loaded.metadata.corridor_capacity = loaded.metadata.corridor_count;
    loaded.metadata.room_adjacency_count = (size_t)room_adjacency_count_u64;
    loaded.metadata.room_neighbor_count = (size_t)room_neighbor_count_u64;

    status = dg_read_size_from_u64(file, &loaded.metadata.walkable_tile_count);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_size_from_u64(file, &loaded.metadata.wall_tile_count);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_size_from_u64(file, &loaded.metadata.special_room_count);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_size_from_u64(file, &loaded.metadata.entrance_room_count);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_size_from_u64(file, &loaded.metadata.exit_room_count);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_size_from_u64(file, &loaded.metadata.boss_room_count);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_size_from_u64(file, &loaded.metadata.treasure_room_count);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_size_from_u64(file, &loaded.metadata.shop_room_count);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_size_from_u64(file, &loaded.metadata.leaf_room_count);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_size_from_u64(file, &loaded.metadata.corridor_total_length);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }

    {
        int32_t entrance_exit_distance_i32;
        status = dg_read_i32(file, &entrance_exit_distance_i32);
        if (status != DG_STATUS_OK) {
            dg_map_destroy(&loaded);
            fclose(file);
            return status;
        }
        if (!dg_i32_to_int_checked(
                entrance_exit_distance_i32,
                &loaded.metadata.entrance_exit_distance
            )) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
    }

    status = dg_read_size_from_u64(file, &loaded.metadata.connected_component_count);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }
    status = dg_read_size_from_u64(file, &loaded.metadata.largest_component_size);
    if (status != DG_STATUS_OK) {
        dg_map_destroy(&loaded);
        fclose(file);
        return status;
    }

    for (i = 0; i < tile_count; ++i) {
        uint8_t tile_value;
        status = dg_read_u8(file, &tile_value);
        if (status != DG_STATUS_OK) {
            dg_map_destroy(&loaded);
            fclose(file);
            return status;
        }
        if (!dg_tile_value_is_valid(tile_value)) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
        loaded.tiles[i] = (dg_tile_t)tile_value;
    }

    if (loaded.metadata.room_count > 0) {
        size_t room_bytes;
        if (dg_mul_size_would_overflow(loaded.metadata.room_count, sizeof(dg_room_metadata_t), &room_bytes)) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
        loaded.metadata.rooms = (dg_room_metadata_t *)malloc(room_bytes);
        if (loaded.metadata.rooms == NULL) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_ALLOCATION_FAILED;
        }

        for (i = 0; i < loaded.metadata.room_count; ++i) {
            dg_room_metadata_t *room = &loaded.metadata.rooms[i];
            int32_t role_i32;
            status = dg_read_int_from_i32(file, &room->id);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_int_from_i32(file, &room->bounds.x);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_int_from_i32(file, &room->bounds.y);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_int_from_i32(file, &room->bounds.width);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_int_from_i32(file, &room->bounds.height);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_u32(file, &room->flags);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_i32(file, &role_i32);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            if (!dg_room_role_value_is_valid(role_i32)) {
                dg_map_destroy(&loaded);
                fclose(file);
                return DG_STATUS_UNSUPPORTED_FORMAT;
            }
            room->role = (dg_room_role_t)role_i32;
            if (room->bounds.width <= 0 || room->bounds.height <= 0) {
                dg_map_destroy(&loaded);
                fclose(file);
                return DG_STATUS_UNSUPPORTED_FORMAT;
            }
        }
    }

    if (loaded.metadata.corridor_count > 0) {
        size_t corridor_bytes;
        if (dg_mul_size_would_overflow(
                loaded.metadata.corridor_count,
                sizeof(dg_corridor_metadata_t),
                &corridor_bytes
            )) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
        loaded.metadata.corridors = (dg_corridor_metadata_t *)malloc(corridor_bytes);
        if (loaded.metadata.corridors == NULL) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_ALLOCATION_FAILED;
        }

        for (i = 0; i < loaded.metadata.corridor_count; ++i) {
            dg_corridor_metadata_t *corridor = &loaded.metadata.corridors[i];
            status = dg_read_int_from_i32(file, &corridor->from_room_id);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_int_from_i32(file, &corridor->to_room_id);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_int_from_i32(file, &corridor->width);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_int_from_i32(file, &corridor->length);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            if (corridor->width <= 0 || corridor->length <= 0) {
                dg_map_destroy(&loaded);
                fclose(file);
                return DG_STATUS_UNSUPPORTED_FORMAT;
            }
        }
    }

    if (loaded.metadata.room_adjacency_count > 0) {
        size_t adjacency_bytes;
        if (dg_mul_size_would_overflow(
                loaded.metadata.room_adjacency_count,
                sizeof(dg_room_adjacency_span_t),
                &adjacency_bytes
            )) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
        loaded.metadata.room_adjacency =
            (dg_room_adjacency_span_t *)malloc(adjacency_bytes);
        if (loaded.metadata.room_adjacency == NULL) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_ALLOCATION_FAILED;
        }

        for (i = 0; i < loaded.metadata.room_adjacency_count; ++i) {
            uint64_t start_u64;
            uint64_t count_u64;
            dg_room_adjacency_span_t *span = &loaded.metadata.room_adjacency[i];

            status = dg_read_u64(file, &start_u64);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_u64(file, &count_u64);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            if (start_u64 > (uint64_t)SIZE_MAX || count_u64 > (uint64_t)SIZE_MAX) {
                dg_map_destroy(&loaded);
                fclose(file);
                return DG_STATUS_UNSUPPORTED_FORMAT;
            }
            span->start_index = (size_t)start_u64;
            span->count = (size_t)count_u64;
            if (span->start_index > loaded.metadata.room_neighbor_count ||
                span->count > loaded.metadata.room_neighbor_count ||
                span->count > loaded.metadata.room_neighbor_count - span->start_index) {
                dg_map_destroy(&loaded);
                fclose(file);
                return DG_STATUS_UNSUPPORTED_FORMAT;
            }
        }
    }

    if (loaded.metadata.room_neighbor_count > 0) {
        size_t neighbor_bytes;
        if (dg_mul_size_would_overflow(
                loaded.metadata.room_neighbor_count,
                sizeof(dg_room_neighbor_t),
                &neighbor_bytes
            )) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
        loaded.metadata.room_neighbors = (dg_room_neighbor_t *)malloc(neighbor_bytes);
        if (loaded.metadata.room_neighbors == NULL) {
            dg_map_destroy(&loaded);
            fclose(file);
            return DG_STATUS_ALLOCATION_FAILED;
        }

        for (i = 0; i < loaded.metadata.room_neighbor_count; ++i) {
            dg_room_neighbor_t *neighbor = &loaded.metadata.room_neighbors[i];
            status = dg_read_int_from_i32(file, &neighbor->room_id);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            status = dg_read_int_from_i32(file, &neighbor->corridor_index);
            if (status != DG_STATUS_OK) {
                dg_map_destroy(&loaded);
                fclose(file);
                return status;
            }
            if (neighbor->room_id < 0 || (size_t)neighbor->room_id >= loaded.metadata.room_count) {
                dg_map_destroy(&loaded);
                fclose(file);
                return DG_STATUS_UNSUPPORTED_FORMAT;
            }
            if (neighbor->corridor_index < 0 ||
                (size_t)neighbor->corridor_index >= loaded.metadata.corridor_count) {
                dg_map_destroy(&loaded);
                fclose(file);
                return DG_STATUS_UNSUPPORTED_FORMAT;
            }
        }
    }

    if (fclose(file) != 0) {
        dg_map_destroy(&loaded);
        return DG_STATUS_IO_ERROR;
    }

    *out_map = loaded;
    return DG_STATUS_OK;
}
