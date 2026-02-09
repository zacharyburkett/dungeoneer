# dungeoneer

`dungeoneer` is a standalone C library for generating 2D procedural dungeon maps.

The generation pipeline is currently focused on two baseline algorithms:
- **BSP tree dungeon generation**
  - `min_rooms` / `max_rooms`
  - `room_min_size` / `room_max_size`
- **Drunkard's Walk generation**
  - `wiggle_percent` (0..100)

The library still includes:
- Deterministic seed-based generation
- Room/corridor metadata and room adjacency graph metadata
- Connectivity/coverage diagnostics in map metadata
- Binary map save/load for editor workflows
- Unit tests, an ASCII demo, and a Nuklear editor app

## Build

```sh
cmake -S . -B build
cmake --build build
```

To build the optional Nuklear app + GLFW presenter:

```sh
cmake -S . -B build -DDUNGEONEER_BUILD_NUKLEAR_APP=ON
cmake --build build
```

By default (`DUNGEONEER_NUKLEAR_AUTO_FETCH=ON`), CMake auto-fetches Nuklear and GLFW
via `FetchContent` during configure (network access required), then builds
`dungeoneer_editor`.

For offline or fully pinned dependencies, disable auto-fetch:

```sh
cmake -S . -B build -DDUNGEONEER_BUILD_NUKLEAR_APP=ON -DDUNGEONEER_NUKLEAR_AUTO_FETCH=OFF
cmake --build build
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Demo

```sh
./build/dungeoneer_ascii_demo bsp 80 40 42
./build/dungeoneer_ascii_demo drunkards 80 40 42 75
# if built with -DDUNGEONEER_BUILD_NUKLEAR_APP=ON
./build/dungeoneer_editor
```

The ASCII demo prints the map and metadata diagnostics.

## Map Serialization

Binary map snapshots can be saved and loaded through:
- `dg_map_save_file(const dg_map_t *map, const char *path)`
- `dg_map_load_file(const char *path, dg_map_t *out_map)`

## Project docs

- Project roadmap: `docs/PROJECT_PLAN.md`
- Technical layout: `docs/ARCHITECTURE.md`
