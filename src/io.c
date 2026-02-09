#include "dungeoneer/io.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char DG_MAP_MAGIC[4] = {'D', 'G', 'M', 'P'};
static const uint32_t DG_MAP_FORMAT_VERSION = 2u;

typedef struct dg_io_writer {
    FILE *file;
    dg_status_t status;
} dg_io_writer_t;

typedef struct dg_io_reader {
    FILE *file;
    dg_status_t status;
} dg_io_reader_t;

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

static bool dg_map_generation_class_value_is_valid(int32_t value)
{
    return value == (int32_t)DG_MAP_GENERATION_CLASS_UNKNOWN ||
           value == (int32_t)DG_MAP_GENERATION_CLASS_ROOM_LIKE ||
           value == (int32_t)DG_MAP_GENERATION_CLASS_CAVE_LIKE;
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

static dg_io_writer_t dg_io_writer_begin(FILE *file)
{
    dg_io_writer_t writer;

    writer.file = file;
    writer.status = DG_STATUS_OK;
    return writer;
}

static void dg_io_writer_write_exact(dg_io_writer_t *writer, const void *data, size_t byte_count)
{
    if (writer == NULL || writer->status != DG_STATUS_OK) {
        return;
    }

    writer->status = dg_write_exact(writer->file, data, byte_count);
}

static void dg_io_writer_write_u8(dg_io_writer_t *writer, uint8_t value)
{
    if (writer == NULL || writer->status != DG_STATUS_OK) {
        return;
    }

    writer->status = dg_write_u8(writer->file, value);
}

static void dg_io_writer_write_u32(dg_io_writer_t *writer, uint32_t value)
{
    if (writer == NULL || writer->status != DG_STATUS_OK) {
        return;
    }

    writer->status = dg_write_u32(writer->file, value);
}

static void dg_io_writer_write_i32(dg_io_writer_t *writer, int32_t value)
{
    if (writer == NULL || writer->status != DG_STATUS_OK) {
        return;
    }

    writer->status = dg_write_i32(writer->file, value);
}

static void dg_io_writer_write_u64(dg_io_writer_t *writer, uint64_t value)
{
    if (writer == NULL || writer->status != DG_STATUS_OK) {
        return;
    }

    writer->status = dg_write_u64(writer->file, value);
}

static void dg_io_writer_write_size(dg_io_writer_t *writer, size_t value)
{
    dg_io_writer_write_u64(writer, (uint64_t)value);
}

static dg_io_reader_t dg_io_reader_begin(FILE *file)
{
    dg_io_reader_t reader;

    reader.file = file;
    reader.status = DG_STATUS_OK;
    return reader;
}

static void dg_io_reader_read_exact(dg_io_reader_t *reader, void *data, size_t byte_count)
{
    if (reader == NULL || reader->status != DG_STATUS_OK) {
        return;
    }

    reader->status = dg_read_exact(reader->file, data, byte_count);
}

static void dg_io_reader_read_u8(dg_io_reader_t *reader, uint8_t *out_value)
{
    if (reader == NULL || reader->status != DG_STATUS_OK) {
        return;
    }

    reader->status = dg_read_u8(reader->file, out_value);
}

static void dg_io_reader_read_u32(dg_io_reader_t *reader, uint32_t *out_value)
{
    if (reader == NULL || reader->status != DG_STATUS_OK) {
        return;
    }

    reader->status = dg_read_u32(reader->file, out_value);
}

static void dg_io_reader_read_i32(dg_io_reader_t *reader, int32_t *out_value)
{
    if (reader == NULL || reader->status != DG_STATUS_OK) {
        return;
    }

    reader->status = dg_read_i32(reader->file, out_value);
}

static void dg_io_reader_read_u64(dg_io_reader_t *reader, uint64_t *out_value)
{
    if (reader == NULL || reader->status != DG_STATUS_OK) {
        return;
    }

    reader->status = dg_read_u64(reader->file, out_value);
}

static void dg_io_reader_read_size(dg_io_reader_t *reader, size_t *out_value)
{
    uint64_t raw_value = 0;

    if (reader == NULL || out_value == NULL) {
        if (reader != NULL && reader->status == DG_STATUS_OK) {
            reader->status = DG_STATUS_INVALID_ARGUMENT;
        }
        return;
    }

    dg_io_reader_read_u64(reader, &raw_value);
    if (reader->status != DG_STATUS_OK) {
        return;
    }

    if (!dg_u64_to_size_checked(raw_value, out_value)) {
        reader->status = DG_STATUS_UNSUPPORTED_FORMAT;
    }
}

static void dg_io_reader_read_int(dg_io_reader_t *reader, int *out_value)
{
    int32_t raw_value = 0;

    if (reader == NULL || out_value == NULL) {
        if (reader != NULL && reader->status == DG_STATUS_OK) {
            reader->status = DG_STATUS_INVALID_ARGUMENT;
        }
        return;
    }

    dg_io_reader_read_i32(reader, &raw_value);
    if (reader->status != DG_STATUS_OK) {
        return;
    }

    if (!dg_i32_to_int_checked(raw_value, out_value)) {
        reader->status = DG_STATUS_UNSUPPORTED_FORMAT;
    }
}

static dg_status_t dg_allocate_array(void **out_ptr, size_t count, size_t element_size)
{
    size_t byte_count;
    void *allocated;

    if (out_ptr == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    *out_ptr = NULL;
    if (count == 0) {
        return DG_STATUS_OK;
    }

    if (dg_mul_size_would_overflow(count, element_size, &byte_count)) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    allocated = malloc(byte_count);
    if (allocated == NULL) {
        return DG_STATUS_ALLOCATION_FAILED;
    }

    *out_ptr = allocated;
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

    if (!dg_map_generation_class_value_is_valid((int32_t)map->metadata.generation_class)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.room_count > 0 && map->metadata.rooms == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.corridor_count > 0 && map->metadata.corridors == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.room_adjacency_count > 0 && map->metadata.room_adjacency == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.room_neighbor_count > 0 && map->metadata.room_neighbors == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    return DG_STATUS_OK;
}

static void dg_write_metadata_counts(dg_io_writer_t *writer, const dg_map_metadata_t *metadata)
{
    dg_io_writer_write_size(writer, metadata->room_count);
    dg_io_writer_write_size(writer, metadata->corridor_count);
    dg_io_writer_write_size(writer, metadata->room_adjacency_count);
    dg_io_writer_write_size(writer, metadata->room_neighbor_count);
}

static void dg_write_metadata_metrics(dg_io_writer_t *writer, const dg_map_metadata_t *metadata)
{
    const size_t *leading_size_fields[] = {
        &metadata->walkable_tile_count,
        &metadata->wall_tile_count,
        &metadata->special_room_count,
        &metadata->entrance_room_count,
        &metadata->exit_room_count,
        &metadata->boss_room_count,
        &metadata->treasure_room_count,
        &metadata->shop_room_count,
        &metadata->leaf_room_count,
        &metadata->corridor_total_length
    };
    size_t i;

    for (i = 0; i < sizeof(leading_size_fields) / sizeof(leading_size_fields[0]); ++i) {
        dg_io_writer_write_size(writer, *leading_size_fields[i]);
    }

    dg_io_writer_write_i32(writer, (int32_t)metadata->entrance_exit_distance);
    dg_io_writer_write_size(writer, metadata->connected_component_count);
    dg_io_writer_write_size(writer, metadata->largest_component_size);
}

static void dg_write_tiles(dg_io_writer_t *writer, const dg_tile_t *tiles, size_t tile_count)
{
    size_t i;

    for (i = 0; i < tile_count; ++i) {
        dg_io_writer_write_u8(writer, (uint8_t)tiles[i]);
    }
}

static void dg_write_rooms(dg_io_writer_t *writer, const dg_map_t *map)
{
    size_t i;

    for (i = 0; i < map->metadata.room_count; ++i) {
        const dg_room_metadata_t *room = &map->metadata.rooms[i];

        dg_io_writer_write_i32(writer, (int32_t)room->id);
        dg_io_writer_write_i32(writer, (int32_t)room->bounds.x);
        dg_io_writer_write_i32(writer, (int32_t)room->bounds.y);
        dg_io_writer_write_i32(writer, (int32_t)room->bounds.width);
        dg_io_writer_write_i32(writer, (int32_t)room->bounds.height);
        dg_io_writer_write_u32(writer, (uint32_t)room->flags);
        dg_io_writer_write_i32(writer, (int32_t)room->role);
    }
}

static void dg_write_corridors(dg_io_writer_t *writer, const dg_map_t *map)
{
    size_t i;

    for (i = 0; i < map->metadata.corridor_count; ++i) {
        const dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];

        dg_io_writer_write_i32(writer, (int32_t)corridor->from_room_id);
        dg_io_writer_write_i32(writer, (int32_t)corridor->to_room_id);
        dg_io_writer_write_i32(writer, (int32_t)corridor->width);
        dg_io_writer_write_i32(writer, (int32_t)corridor->length);
    }
}

static void dg_write_room_adjacency(dg_io_writer_t *writer, const dg_map_t *map)
{
    size_t i;

    for (i = 0; i < map->metadata.room_adjacency_count; ++i) {
        const dg_room_adjacency_span_t *span = &map->metadata.room_adjacency[i];

        dg_io_writer_write_size(writer, span->start_index);
        dg_io_writer_write_size(writer, span->count);
    }
}

static void dg_write_room_neighbors(dg_io_writer_t *writer, const dg_map_t *map)
{
    size_t i;

    for (i = 0; i < map->metadata.room_neighbor_count; ++i) {
        const dg_room_neighbor_t *neighbor = &map->metadata.room_neighbors[i];

        dg_io_writer_write_i32(writer, (int32_t)neighbor->room_id);
        dg_io_writer_write_i32(writer, (int32_t)neighbor->corridor_index);
    }
}

static dg_status_t dg_finish_write(FILE *file, dg_status_t status)
{
    if (file == NULL) {
        return status == DG_STATUS_OK ? DG_STATUS_INVALID_ARGUMENT : status;
    }

    if (status != DG_STATUS_OK) {
        (void)fclose(file);
        return status;
    }

    if (fclose(file) != 0) {
        return DG_STATUS_IO_ERROR;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_fail_load(FILE *file, dg_map_t *loaded, dg_status_t status)
{
    if (loaded != NULL) {
        dg_map_destroy(loaded);
    }

    if (file != NULL) {
        (void)fclose(file);
    }

    return status;
}

static dg_status_t dg_load_header(
    dg_io_reader_t *reader,
    uint32_t *out_version,
    int *out_width,
    int *out_height,
    size_t *out_tile_count
)
{
    unsigned char magic[sizeof(DG_MAP_MAGIC)];
    uint32_t version = 0;
    uint32_t width_u32 = 0;
    uint32_t height_u32 = 0;
    uint64_t tile_count_u64 = 0;
    size_t tile_count = 0;

    if (reader == NULL || out_version == NULL || out_width == NULL ||
        out_height == NULL || out_tile_count == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_io_reader_read_exact(reader, magic, sizeof(magic));
    if (reader->status != DG_STATUS_OK) {
        return reader->status;
    }
    if (memcmp(magic, DG_MAP_MAGIC, sizeof(DG_MAP_MAGIC)) != 0) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    dg_io_reader_read_u32(reader, &version);
    if (reader->status != DG_STATUS_OK) {
        return reader->status;
    }
    if (version != 1u && version != DG_MAP_FORMAT_VERSION) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    dg_io_reader_read_u32(reader, &width_u32);
    dg_io_reader_read_u32(reader, &height_u32);
    dg_io_reader_read_u64(reader, &tile_count_u64);
    if (reader->status != DG_STATUS_OK) {
        return reader->status;
    }

    if (width_u32 < 1u || height_u32 < 1u ||
        width_u32 > (uint32_t)INT_MAX ||
        height_u32 > (uint32_t)INT_MAX) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    if (dg_mul_size_would_overflow((size_t)width_u32, (size_t)height_u32, &tile_count)) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    if ((uint64_t)tile_count != tile_count_u64) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    *out_version = version;
    *out_width = (int)width_u32;
    *out_height = (int)height_u32;
    *out_tile_count = tile_count;
    return DG_STATUS_OK;
}

static dg_status_t dg_load_metadata_core(dg_io_reader_t *reader, uint32_t version, dg_map_t *map)
{
    int32_t algorithm_id_i32 = 0;
    int32_t generation_class_i32 = 0;
    uint8_t connected_floor_u8 = 0;

    if (reader == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_io_reader_read_u64(reader, &map->metadata.seed);
    dg_io_reader_read_i32(reader, &algorithm_id_i32);

    if (version >= 2u) {
        dg_io_reader_read_i32(reader, &generation_class_i32);
    }

    dg_io_reader_read_size(reader, &map->metadata.generation_attempts);
    dg_io_reader_read_u8(reader, &connected_floor_u8);
    if (reader->status != DG_STATUS_OK) {
        return reader->status;
    }

    if (!dg_i32_to_int_checked(algorithm_id_i32, &map->metadata.algorithm_id)) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    if (version >= 2u) {
        if (!dg_map_generation_class_value_is_valid(generation_class_i32)) {
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
        map->metadata.generation_class = (dg_map_generation_class_t)generation_class_i32;
    } else {
        map->metadata.generation_class = DG_MAP_GENERATION_CLASS_UNKNOWN;
    }

    map->metadata.connected_floor = connected_floor_u8 != 0u;
    return DG_STATUS_OK;
}

static dg_status_t dg_load_metadata_counts(dg_io_reader_t *reader, dg_map_t *map)
{
    uint64_t room_count_u64 = 0;
    uint64_t corridor_count_u64 = 0;
    uint64_t room_adjacency_count_u64 = 0;
    uint64_t room_neighbor_count_u64 = 0;

    if (reader == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    dg_io_reader_read_u64(reader, &room_count_u64);
    dg_io_reader_read_u64(reader, &corridor_count_u64);
    dg_io_reader_read_u64(reader, &room_adjacency_count_u64);
    dg_io_reader_read_u64(reader, &room_neighbor_count_u64);
    if (reader->status != DG_STATUS_OK) {
        return reader->status;
    }

    if (room_count_u64 > (uint64_t)SIZE_MAX ||
        corridor_count_u64 > (uint64_t)SIZE_MAX ||
        room_adjacency_count_u64 > (uint64_t)SIZE_MAX ||
        room_neighbor_count_u64 > (uint64_t)SIZE_MAX) {
        return DG_STATUS_UNSUPPORTED_FORMAT;
    }

    map->metadata.room_count = (size_t)room_count_u64;
    map->metadata.room_capacity = map->metadata.room_count;
    map->metadata.corridor_count = (size_t)corridor_count_u64;
    map->metadata.corridor_capacity = map->metadata.corridor_count;
    map->metadata.room_adjacency_count = (size_t)room_adjacency_count_u64;
    map->metadata.room_neighbor_count = (size_t)room_neighbor_count_u64;

    if (map->metadata.generation_class == DG_MAP_GENERATION_CLASS_UNKNOWN) {
        if (map->metadata.room_count > 0 || map->metadata.corridor_count > 0) {
            map->metadata.generation_class = DG_MAP_GENERATION_CLASS_ROOM_LIKE;
        } else {
            map->metadata.generation_class = DG_MAP_GENERATION_CLASS_CAVE_LIKE;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_load_metadata_metrics(dg_io_reader_t *reader, dg_map_t *map)
{
    size_t *size_fields[] = {
        &map->metadata.walkable_tile_count,
        &map->metadata.wall_tile_count,
        &map->metadata.special_room_count,
        &map->metadata.entrance_room_count,
        &map->metadata.exit_room_count,
        &map->metadata.boss_room_count,
        &map->metadata.treasure_room_count,
        &map->metadata.shop_room_count,
        &map->metadata.leaf_room_count,
        &map->metadata.corridor_total_length
    };
    size_t i;

    if (reader == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (i = 0; i < sizeof(size_fields) / sizeof(size_fields[0]); ++i) {
        dg_io_reader_read_size(reader, size_fields[i]);
    }

    dg_io_reader_read_int(reader, &map->metadata.entrance_exit_distance);
    dg_io_reader_read_size(reader, &map->metadata.connected_component_count);
    dg_io_reader_read_size(reader, &map->metadata.largest_component_size);

    if (reader->status != DG_STATUS_OK) {
        return reader->status;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_load_tiles(dg_io_reader_t *reader, dg_map_t *map, size_t tile_count)
{
    size_t i;

    if (reader == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    for (i = 0; i < tile_count; ++i) {
        uint8_t tile_value = 0;

        dg_io_reader_read_u8(reader, &tile_value);
        if (reader->status != DG_STATUS_OK) {
            return reader->status;
        }

        if (!dg_tile_value_is_valid(tile_value)) {
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }

        map->tiles[i] = (dg_tile_t)tile_value;
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_load_rooms(dg_io_reader_t *reader, dg_map_t *map)
{
    size_t i;
    dg_status_t status;

    if (reader == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.room_count == 0) {
        return DG_STATUS_OK;
    }

    status = dg_allocate_array(
        (void **)&map->metadata.rooms,
        map->metadata.room_count,
        sizeof(dg_room_metadata_t)
    );
    if (status != DG_STATUS_OK) {
        return status;
    }

    for (i = 0; i < map->metadata.room_count; ++i) {
        dg_room_metadata_t *room = &map->metadata.rooms[i];
        int32_t role_i32 = 0;

        dg_io_reader_read_int(reader, &room->id);
        dg_io_reader_read_int(reader, &room->bounds.x);
        dg_io_reader_read_int(reader, &room->bounds.y);
        dg_io_reader_read_int(reader, &room->bounds.width);
        dg_io_reader_read_int(reader, &room->bounds.height);
        dg_io_reader_read_u32(reader, &room->flags);
        dg_io_reader_read_i32(reader, &role_i32);

        if (reader->status != DG_STATUS_OK) {
            return reader->status;
        }

        if (!dg_room_role_value_is_valid(role_i32)) {
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }

        room->role = (dg_room_role_t)role_i32;
        if (room->bounds.width <= 0 || room->bounds.height <= 0) {
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_load_corridors(dg_io_reader_t *reader, dg_map_t *map)
{
    size_t i;
    dg_status_t status;

    if (reader == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.corridor_count == 0) {
        return DG_STATUS_OK;
    }

    status = dg_allocate_array(
        (void **)&map->metadata.corridors,
        map->metadata.corridor_count,
        sizeof(dg_corridor_metadata_t)
    );
    if (status != DG_STATUS_OK) {
        return status;
    }

    for (i = 0; i < map->metadata.corridor_count; ++i) {
        dg_corridor_metadata_t *corridor = &map->metadata.corridors[i];

        dg_io_reader_read_int(reader, &corridor->from_room_id);
        dg_io_reader_read_int(reader, &corridor->to_room_id);
        dg_io_reader_read_int(reader, &corridor->width);
        dg_io_reader_read_int(reader, &corridor->length);

        if (reader->status != DG_STATUS_OK) {
            return reader->status;
        }

        if (corridor->width <= 0 || corridor->length <= 0) {
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_load_room_adjacency(dg_io_reader_t *reader, dg_map_t *map)
{
    size_t i;
    dg_status_t status;

    if (reader == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.room_adjacency_count == 0) {
        return DG_STATUS_OK;
    }

    status = dg_allocate_array(
        (void **)&map->metadata.room_adjacency,
        map->metadata.room_adjacency_count,
        sizeof(dg_room_adjacency_span_t)
    );
    if (status != DG_STATUS_OK) {
        return status;
    }

    for (i = 0; i < map->metadata.room_adjacency_count; ++i) {
        dg_room_adjacency_span_t *span = &map->metadata.room_adjacency[i];

        dg_io_reader_read_size(reader, &span->start_index);
        dg_io_reader_read_size(reader, &span->count);

        if (reader->status != DG_STATUS_OK) {
            return reader->status;
        }

        if (span->start_index > map->metadata.room_neighbor_count ||
            span->count > map->metadata.room_neighbor_count ||
            span->count > map->metadata.room_neighbor_count - span->start_index) {
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
    }

    return DG_STATUS_OK;
}

static dg_status_t dg_load_room_neighbors(dg_io_reader_t *reader, dg_map_t *map)
{
    size_t i;
    dg_status_t status;

    if (reader == NULL || map == NULL) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    if (map->metadata.room_neighbor_count == 0) {
        return DG_STATUS_OK;
    }

    status = dg_allocate_array(
        (void **)&map->metadata.room_neighbors,
        map->metadata.room_neighbor_count,
        sizeof(dg_room_neighbor_t)
    );
    if (status != DG_STATUS_OK) {
        return status;
    }

    for (i = 0; i < map->metadata.room_neighbor_count; ++i) {
        dg_room_neighbor_t *neighbor = &map->metadata.room_neighbors[i];

        dg_io_reader_read_int(reader, &neighbor->room_id);
        dg_io_reader_read_int(reader, &neighbor->corridor_index);

        if (reader->status != DG_STATUS_OK) {
            return reader->status;
        }

        if (neighbor->room_id < 0 || (size_t)neighbor->room_id >= map->metadata.room_count) {
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }

        if (neighbor->corridor_index < 0 ||
            (size_t)neighbor->corridor_index >= map->metadata.corridor_count) {
            return DG_STATUS_UNSUPPORTED_FORMAT;
        }
    }

    return DG_STATUS_OK;
}

dg_status_t dg_map_save_file(const dg_map_t *map, const char *path)
{
    FILE *file;
    dg_status_t status;
    size_t tile_count;
    dg_io_writer_t writer;

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

    writer = dg_io_writer_begin(file);

    dg_io_writer_write_exact(&writer, DG_MAP_MAGIC, sizeof(DG_MAP_MAGIC));
    dg_io_writer_write_u32(&writer, DG_MAP_FORMAT_VERSION);
    dg_io_writer_write_u32(&writer, (uint32_t)map->width);
    dg_io_writer_write_u32(&writer, (uint32_t)map->height);
    dg_io_writer_write_size(&writer, tile_count);

    dg_io_writer_write_u64(&writer, map->metadata.seed);
    dg_io_writer_write_i32(&writer, (int32_t)map->metadata.algorithm_id);
    dg_io_writer_write_i32(&writer, (int32_t)map->metadata.generation_class);
    dg_io_writer_write_size(&writer, map->metadata.generation_attempts);
    dg_io_writer_write_u8(&writer, map->metadata.connected_floor ? 1u : 0u);

    dg_write_metadata_counts(&writer, &map->metadata);
    dg_write_metadata_metrics(&writer, &map->metadata);

    dg_write_tiles(&writer, map->tiles, tile_count);
    dg_write_rooms(&writer, map);
    dg_write_corridors(&writer, map);
    dg_write_room_adjacency(&writer, map);
    dg_write_room_neighbors(&writer, map);

    return dg_finish_write(file, writer.status);
}

dg_status_t dg_map_load_file(const char *path, dg_map_t *out_map)
{
    FILE *file;
    uint32_t version = 0;
    int width = 0;
    int height = 0;
    size_t tile_count = 0;
    dg_map_t loaded;
    dg_status_t status;
    dg_io_reader_t reader;

    if (path == NULL || out_map == NULL || !dg_map_is_empty(out_map)) {
        return DG_STATUS_INVALID_ARGUMENT;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return DG_STATUS_IO_ERROR;
    }

    memset(&loaded, 0, sizeof(loaded));
    reader = dg_io_reader_begin(file);

    status = dg_load_header(&reader, &version, &width, &height, &tile_count);
    if (status != DG_STATUS_OK) {
        return dg_fail_load(file, &loaded, status);
    }

    status = dg_map_init(&loaded, width, height, DG_TILE_WALL);
    if (status != DG_STATUS_OK) {
        return dg_fail_load(file, &loaded, status);
    }

    status = dg_load_metadata_core(&reader, version, &loaded);
    if (status != DG_STATUS_OK) {
        return dg_fail_load(file, &loaded, status);
    }

    status = dg_load_metadata_counts(&reader, &loaded);
    if (status != DG_STATUS_OK) {
        return dg_fail_load(file, &loaded, status);
    }

    status = dg_load_metadata_metrics(&reader, &loaded);
    if (status != DG_STATUS_OK) {
        return dg_fail_load(file, &loaded, status);
    }

    status = dg_load_tiles(&reader, &loaded, tile_count);
    if (status != DG_STATUS_OK) {
        return dg_fail_load(file, &loaded, status);
    }

    status = dg_load_rooms(&reader, &loaded);
    if (status != DG_STATUS_OK) {
        return dg_fail_load(file, &loaded, status);
    }

    status = dg_load_corridors(&reader, &loaded);
    if (status != DG_STATUS_OK) {
        return dg_fail_load(file, &loaded, status);
    }

    status = dg_load_room_adjacency(&reader, &loaded);
    if (status != DG_STATUS_OK) {
        return dg_fail_load(file, &loaded, status);
    }

    status = dg_load_room_neighbors(&reader, &loaded);
    if (status != DG_STATUS_OK) {
        return dg_fail_load(file, &loaded, status);
    }

    if (fclose(file) != 0) {
        dg_map_destroy(&loaded);
        return DG_STATUS_IO_ERROR;
    }

    *out_map = loaded;
    return DG_STATUS_OK;
}
